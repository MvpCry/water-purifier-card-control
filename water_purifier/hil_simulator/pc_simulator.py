#!/usr/bin/env python3
"""
净水机 PC 端传感器模拟器
========================

通过 Serial/USB 与 Arduino 通信，模拟：
  - DS18B20 温度传感器 (热力学模型)
  - YF-S201 流量计 (脉冲计数)
  - 继电器输出 (净化水阀、热水阀、加热器) — 仅记录状态

协议 (文本行，以 \\n 结尾):
  Arduino → PC:  TEMP?         → 请求当前温度
                  FLOW?         → 请求累计流量脉冲 (读取后清零)
                  RELAY:<pin>:<0|1> → 继电器状态变更通知
  PC → Arduino:  TEMP:<float>  → 温度值
                  FLOW:<int>    → 脉冲数
                  OK            → 确认

用法:
  python pc_simulator.py COM3
  python pc_simulator.py COM3 115200
"""

import sys
import time
import threading
import os

# ============================================================
# 物理模型参数
# ============================================================
AMBIENT_TEMP       = 25.0    # 环境温度 (℃)
HEAT_RATE          = 0.3     # 加热速率 (℃/s)
COOL_DECAY         = 0.05    # 降温衰减系数
MAX_TEMP           = 100.0   # 物理上限 (℃)
PULSES_PER_LITER   = 450     # YF-S201 脉冲/升
FLOW_RATE_LPM      = 1.0     # 模拟流速 (升/分钟)
TARGET_PURE_ML     = 500     # 净化水目标量
TARGET_HOT_ML      = 300     # 热水目标量

# ============================================================
# 模拟器状态
# ============================================================
class Simulator:
    def __init__(self):
        self.temperature    = 25.0
        self.heater_on      = False
        self.pure_valve     = False
        self.hot_valve      = False
        self.flow_pulses    = 0.0   # 浮点累积，查询时取整
        self.last_update    = time.time()

        # 出水进度
        self.dispensing_type = None  # 'pure' | 'hot' | None
        self.dispensed_ml    = 0
        self.target_ml       = 0

        # 统计
        self.total_pure_ml   = 0
        self.total_hot_ml    = 0
        self.heater_runtime  = 0.0

    def update(self):
        """更新物理模型 (每帧调用)"""
        now = time.time()
        dt  = now - self.last_update
        self.last_update = now

        if dt > 0.5:   # 防止卡顿导致跳跃
            dt = 0.5
        if dt <= 0:
            return

        # ---- 温度模型 ----
        if self.heater_on:
            self.temperature += HEAT_RATE * dt
            self.heater_runtime += dt
            if self.temperature > MAX_TEMP:
                self.temperature = MAX_TEMP
        else:
            decay = COOL_DECAY * (self.temperature - AMBIENT_TEMP) * dt
            self.temperature -= decay
            if self.temperature < AMBIENT_TEMP:
                self.temperature = AMBIENT_TEMP

        # ---- 流量模型 ----
        if self.pure_valve or self.hot_valve:
            pulses_per_sec = PULSES_PER_LITER * FLOW_RATE_LPM / 60.0
            added = pulses_per_sec * dt
            self.flow_pulses += added

            # 跟踪出水量
            if self.dispensing_type:
                ml_added = (added / PULSES_PER_LITER) * 1000
                self.dispensed_ml += ml_added

                # 达到目标量 → 自动关阀 (反馈给用户状态变化)
                if self.dispensed_ml >= self.target_ml:
                    if self.pure_valve:
                        self.pure_valve = False
                        self.total_pure_ml += self.dispensed_ml
                    if self.hot_valve:
                        self.hot_valve = False
                        self.total_hot_ml += self.dispensed_ml
                    self.dispensing_type = None

    def handle_command(self, cmd):
        """处理 Arduino 发来的命令，返回响应字符串"""
        cmd = cmd.strip()
        if not cmd:
            return None

        if cmd == 'TEMP?':
            return f'TEMP:{self.temperature:.1f}'

        elif cmd == 'FLOW?':
            pulses = int(self.flow_pulses)
            self.flow_pulses = 0.0  # 读取后清零
            return f'FLOW:{pulses}'

        elif cmd.startswith('RELAY:'):
            parts = cmd.split(':')
            if len(parts) == 3:
                pin = int(parts[1])
                val = int(parts[2])
                self._set_relay(pin, val)
            return 'OK'

        return None  # 不识别的命令

    def _set_relay(self, pin, val):
        """设置继电器状态，并触发相应逻辑"""
        on = (val == 1)
        labels = {5: '净化水阀', 6: '热水阀', 7: '加热器'}
        label  = labels.get(pin, f'引脚{pin}')
        icon   = '🔥' if pin == 7 else '🚰'
        state  = '开' if on else '关'

        if pin == 5:  # 净化水
            if on and not self.pure_valve:
                self.dispensing_type = 'pure'
                self.target_ml = TARGET_PURE_ML
                self.dispensed_ml = 0
            elif not on:
                if self.pure_valve:
                    self.total_pure_ml += self.dispensed_ml
                self.pure_valve = False
                if self.dispensing_type == 'pure':
                    self.dispensing_type = None
            self.pure_valve = on

        elif pin == 6:  # 热水
            if on and not self.hot_valve:
                self.dispensing_type = 'hot'
                self.target_ml = TARGET_HOT_ML
                self.dispensed_ml = 0
            elif not on:
                if self.hot_valve:
                    self.total_hot_ml += self.dispensed_ml
                self.hot_valve = False
                if self.dispensing_type == 'hot':
                    self.dispensing_type = None
            self.hot_valve = on

        elif pin == 7:  # 加热器
            self.heater_on = on

        print(f'  {icon} [{label}] → {state}')


# ============================================================
# 控制台仪表盘
# ============================================================

def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def draw_dashboard(sim):
    """绘制仪表盘"""
    clear_screen()
    print()
    print('╔══════════════════════════════════════╗')
    print('║   净水机 PC 端传感器模拟器           ║')
    print('╠══════════════════════════════════════╣')

    # 温度
    temp = sim.temperature
    bar_len = int((temp - 25) / (92 - 25) * 28)
    bar_len = max(0, min(28, bar_len))
    bar = '█' * bar_len + '░' * (28 - bar_len)
    ready = '✅ 热水就绪' if temp >= 85 else '⏳ 加热中...' if sim.heater_on else '❄️  待加热'
    print(f'║ 🌡️  水温: {temp:5.1f}℃  [{bar}] {ready} ║')

    # 阀门状态
    pv = '🚰 开' if sim.pure_valve else '-- 关'
    hv = '🚰 开' if sim.hot_valve else '-- 关'
    ht = '🔥 开' if sim.heater_on else '-- 关'
    print(f'║ 净化水阀: {pv}   热水阀: {hv}   加热器: {ht}      ║')

    # 出水进度
    if sim.dispensing_type:
        dtype = '净化水' if sim.dispensing_type == 'pure' else '热水'
        done  = int(sim.dispensed_ml)
        goal  = int(sim.target_ml)
        pct   = min(100, int(done / goal * 100)) if goal > 0 else 0
        pbar  = '█' * (pct // 5) + '░' * (20 - pct // 5)
        print(f'║ 出水: {dtype} {done}/{goal}ml [{pbar}] {pct:3d}% ║')
    else:
        print(f'║ 出水: 待机中                               ║')

    # 累计统计
    print(f'╠══════════════════════════════════════╣')
    print(f'║ 📊 累计净化水: {sim.total_pure_ml:6.0f}ml'
          f'   累计热水: {sim.total_hot_ml:6.0f}ml  ║')
    print(f'║ ⏱️  加热累计运行: {sim.heater_runtime:6.0f}s                    ║')

    print(f'╠══════════════════════════════════════╣')
    print(f'║ 操作: 在 Arduino 端刷卡控制          ║')
    print(f'║ 退出: Ctrl+C                         ║')
    print(f'╚══════════════════════════════════════╝')
    print()


# ============================================================
# 主程序
# ============================================================

def get_serial(port, baudrate):
    """尝试导入 serial 并打开端口"""
    try:
        import serial
    except ImportError:
        print('错误: 需要安装 pyserial')
        print('  pip install pyserial')
        sys.exit(1)
    try:
        ser = serial.Serial(port, baudrate, timeout=0.1)
        return ser
    except Exception as e:
        print(f'错误: 无法打开串口 {port}')
        print(f'  {e}')
        print()
        # 列出可用端口
        try:
            from serial.tools import list_ports
            ports = list_ports.comports()
            if ports:
                print('可用端口:')
                for p in ports:
                    print(f'  {p.device} - {p.description}')
            else:
                print('未检测到串口设备')
        except:
            pass
        sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print('用法: python pc_simulator.py <COM端口> [波特率]')
        print('示例: python pc_simulator.py COM3')
        print('      python pc_simulator.py COM3 115200')
        sys.exit(1)

    port     = sys.argv[1]
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    print(f'正在连接 Arduino ({port} @ {baudrate} bps)...')
    ser = get_serial(port, baudrate)
    print(f'已连接!')
    time.sleep(1)  # 等待 Arduino 复位

    sim = Simulator()
    last_display = 0
    buffer = ''

    try:
        while True:
            # ---- 读串口 ----
            try:
                while ser.in_waiting > 0:
                    ch = ser.read().decode('ascii', errors='ignore')
                    if ch == '\n':
                        response = sim.handle_command(buffer)
                        if response is not None:
                            ser.write((response + '\n').encode('ascii'))
                        buffer = ''
                    else:
                        buffer += ch
            except Exception as e:
                print(f'串口读取错误: {e}')

            # ---- 更新物理模型 ----
            sim.update()

            # ---- 刷新仪表盘 (~200ms) ----
            now = time.time()
            if now - last_display > 0.2:
                last_display = now
                draw_dashboard(sim)

            # ---- 自动响应 (物理事件触发) ----
            # 如果阀门已关但 Arduino 还不知道，下次它查询 FLOW? 或 TEMP? 时会发现
            # 出水量达到目标时，PC 端自动关阀模拟
            # (Arduino 端也会在检测到达到目标后主动关阀)

            time.sleep(0.02)  # ~50Hz

    except KeyboardInterrupt:
        print('\n模拟器已停止')
    finally:
        ser.close()
        print('串口已关闭')


if __name__ == '__main__':
    main()
