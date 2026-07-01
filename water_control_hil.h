/**
 * 净水机刷卡控制系统 - 出水控制模块 (HIL 版)
 * =============================================
 *
 * 硬件在环 (Hardware-in-the-Loop) 版本:
 *   - 温度传感器 (DS18B20) → 由 PC 模拟器通过 Serial 提供
 *   - 流量计 (YF-S201)     → 由 PC 模拟器通过 Serial 提供脉冲
 *   - 继电器 (阀门/加热器)  → 通过 Serial 通知 PC，PC 端记录/模拟
 *   - LED 指示灯            → 已移除，状态通过 ST7789 彩屏显示
 *
 * 协议 (与 pc_simulator.py 通信):
 *   Arduino → PC:  TEMP?\n         请求温度
 *                   FLOW?\n        请求累计流量脉冲 (读取后清零)
 *                   RELAY:<pin>:<0|1>\n  继电器状态通知
 *   PC → Arduino:  TEMP:<float>\n  温度值
 *                   FLOW:<int>\n   脉冲数
 *                   OK\n          确认
 *
 * 与原始 water_control.h 的区别:
 *   1. 不依赖 OneWire / DallasTemperature 库
 *   2. RELAY 引脚不接物理继电器，改为 Serial 通知 PC
 *   3. 流量计中断替换为周期性 Serial 轮询
 *   4. 温度从 PC 获取
 */

#ifndef WATER_CONTROL_HIL_H
#define WATER_CONTROL_HIL_H

#include "config_hil.h"

// ============================
// 系统状态枚举
// ============================

enum SystemState {
  STATE_IDLE,          // 待机
  STATE_DISPENSING,    // 出水中
  STATE_HEATING,       // 加热中
  STATE_ERROR,         // 故障
  STATE_PAUSED,        // 暂停 (流量计无脉冲)
};

// ============================
// WaterControl 类 (HIL)
// ============================

class WaterControl {
public:
  void begin();
  bool startPureWater(int targetMl = 0);
  bool startHotWater(int targetMl = 0);
  void stopDispensing();
  float getTemperature();
  int getDispensedMl();
  int getTargetMl();
  SystemState getState();
  void update();
  bool isHotWaterReady();
  void setHeater(bool on);

  // HIL 专用: 无硬件中断，改为 update() 中轮询
  // flowPulseISR() 已移除

private:
  SystemState _state;
  bool _relayPure;
  bool _relayHot;
  bool _heaterOn;

  volatile unsigned long _pulseCount;
  int _targetMl;
  int _dispensedMl;
  unsigned long _dispenseStartMs;
  unsigned long _heaterStartedMs;

  float _currentTemp;
  unsigned long _lastTempReadMs;

  unsigned long _lastFlowPulseMs;
  unsigned long _lastFlowPollMs;    // 上次轮询 PC 流量计的时间

  void _resetCounters();
  void _openValve(int relayPin);
  void _closeAllValves();
  int _pulsesToMl(unsigned long pulses);

  // ---- Serial 通信辅助 ----
  void _sendCmd(const char* cmd);
  bool _readResponse(char* buf, int bufsize, unsigned long timeoutMs);
  float _requestTemp();
  int _requestFlow();
  void _notifyRelay(int pin, bool on);
};

// ============================
// 实现
// ============================

void WaterControl::begin() {
  // 初始化蜂鸣器 (真实硬件)
  pinMode(BUZZER_PIN, OUTPUT);

  // 初始化流量计引脚为输入 (虽然不接硬件，保持兼容)
  pinMode(FLOW_METER_PIN, INPUT_PULLUP);

  // 全部关闭
  _closeAllValves();
  setHeater(false);

  // 初始状态
  _state           = STATE_IDLE;
  _pulseCount      = 0;
  _targetMl        = 0;
  _dispensedMl     = 0;
  _dispenseStartMs = 0;
  _currentTemp     = 25.0;
  _lastTempReadMs  = 0;
  _lastFlowPulseMs = 0;
  _lastFlowPollMs  = 0;
}

bool WaterControl::startPureWater(int targetMl) {
  if (_state != STATE_IDLE) return false;

  if (targetMl <= 0)
    _targetMl = DEFAULT_PURE_WATER_ML;
  else if (targetMl > MAX_WATER_ML)
    _targetMl = MAX_WATER_ML;
  else
    _targetMl = targetMl;

  _resetCounters();
  _openValve(RELAY_PURE_WATER);

  _state = STATE_DISPENSING;
  _dispenseStartMs = millis();
  _lastFlowPulseMs = millis();

  return true;
}

bool WaterControl::startHotWater(int targetMl) {
  if (_state != STATE_IDLE) return false;

  // 检查热水是否就绪
  if (!isHotWaterReady()) return false;

  if (targetMl <= 0)
    _targetMl = DEFAULT_HOT_WATER_ML;
  else if (targetMl > MAX_WATER_ML)
    _targetMl = MAX_WATER_ML;
  else
    _targetMl = targetMl;

  _resetCounters();
  _openValve(RELAY_HOT_WATER);

  _state = STATE_DISPENSING;
  _dispenseStartMs = millis();
  _lastFlowPulseMs = millis();

  return true;
}

void WaterControl::stopDispensing() {
  _closeAllValves();
  _state = STATE_IDLE;
  _dispensedMl = _pulsesToMl(_pulseCount);
}

float WaterControl::getTemperature() {
  // 从 PC 获取温度 (带缓存: 最多 0.5 秒读一次)
  unsigned long now = millis();
  if (_lastTempReadMs == 0 || (now - _lastTempReadMs) > 500) {
    _lastTempReadMs = now;
    float t = _requestTemp();
    if (t > -100) {  // -100 表示通信失败
      _currentTemp = t;
    }
  }
  return _currentTemp;
}

int WaterControl::getDispensedMl() {
  _dispensedMl = _pulsesToMl(_pulseCount);
  return _dispensedMl;
}

int WaterControl::getTargetMl() {
  return _targetMl;
}

SystemState WaterControl::getState() {
  return _state;
}

void WaterControl::update() {
  unsigned long now = millis();

  // ---- 轮询 PC 获取流量脉冲 (每 ~100ms) ----
  if (_state == STATE_DISPENSING) {
    if (now - _lastFlowPollMs >= 100) {
      _lastFlowPollMs = now;
      int pulses = _requestFlow();
      if (pulses > 0) {
        _pulseCount += pulses;
        _lastFlowPulseMs = now;
      }
    }
  }

  // ---- 状态逻辑 (与原版一致) ----
  switch (_state) {

    case STATE_DISPENSING: {
      _dispensedMl = _pulsesToMl(_pulseCount);

      // 达到目标水量 -> 停止
      if (_dispensedMl >= _targetMl) {
        stopDispensing();
        return;
      }

      // 出水超时保护
      if (now - _dispenseStartMs > DISPENSE_TIMEOUT) {
        stopDispensing();
        _state = STATE_ERROR;

        digitalWrite(BUZZER_PIN, HIGH);
        delay(BUZZER_LONG_BEEP);
        digitalWrite(BUZZER_PIN, LOW);
        return;
      }

      // 断流检测 (2秒内无新脉冲 && 还没出水)
      if (_lastFlowPulseMs > 0 &&
          (now - _lastFlowPulseMs) > 2000 &&
          _pulseCount == 0) {
        stopDispensing();
        _state = STATE_ERROR;
      }
      break;
    }

    case STATE_HEATING: {
      float temp = getTemperature();

      if (_heaterOn) {
        // 达到目标温度 -> 停止加热
        if (temp >= HOT_WATER_TARGET_TEMP) {
          setHeater(false);
        }
        // 超时保护
        else if (now - _heaterStartedMs > MAX_HEAT_TIME_SEC * 1000UL) {
          setHeater(false);
          _state = STATE_ERROR;
        }
      } else {
        // 温度低于 (目标 - 回差) -> 重新加热
        if (temp < HOT_WATER_TARGET_TEMP - HEATER_HYSTERESIS) {
          setHeater(true);
          _heaterStartedMs = now;
        }
      }

      // 热水就绪则回到待机
      if (isHotWaterReady()) {
        _state = STATE_IDLE;
      }
      break;
    }

    case STATE_ERROR: {
      // 故障状态: 屏幕显示红色错误画面, 蜂鸣器报警
      break;
    }

    default:
      break;
  }
}

bool WaterControl::isHotWaterReady() {
  return (getTemperature() >= HOT_WATER_MIN_TEMP);
}

void WaterControl::setHeater(bool on) {
  _heaterOn = on;

  // 通知 PC (替代物理继电器)
  _notifyRelay(RELAY_HEATER, on);

  if (on) {
    _state = STATE_HEATING;
    _heaterStartedMs = millis();
  }
}

// ---- 内部辅助方法 ----

void WaterControl::_resetCounters() {
  _pulseCount  = 0;
  _dispensedMl = 0;
}

void WaterControl::_openValve(int relayPin) {
  // 通知 PC (替代物理继电器)
  _notifyRelay(relayPin, true);

  _relayPure = (relayPin == RELAY_PURE_WATER);
  _relayHot  = (relayPin == RELAY_HOT_WATER);
}

void WaterControl::_closeAllValves() {
  if (_relayPure) _notifyRelay(RELAY_PURE_WATER, false);
  if (_relayHot)  _notifyRelay(RELAY_HOT_WATER,  false);
  _relayPure = false;
  _relayHot  = false;
}

int WaterControl::_pulsesToMl(unsigned long pulses) {
  return (int)((pulses * 1000UL) / PULSES_PER_LITER);
}

// ============================================================
// Serial 通信协议实现
// ============================================================

void WaterControl::_sendCmd(const char* cmd) {
  Serial.print(cmd);
  Serial.print('\n');
}

bool WaterControl::_readResponse(char* buf, int bufsize, unsigned long timeoutMs) {
  unsigned long start = millis();
  int idx = 0;

  while (millis() - start < timeoutMs) {
    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n') {
        buf[idx] = '\0';
        return true;
      }
      if (idx < bufsize - 1) {
        buf[idx++] = c;
      }
    }
    // 短暂等待，避免忙等
    delay(1);
  }

  buf[0] = '\0';
  return false;  // 超时
}

float WaterControl::_requestTemp() {
  _sendCmd("TEMP?");

  char buf[32];
  if (_readResponse(buf, sizeof(buf), 200)) {
    // 格式: TEMP:XX.X
    if (strncmp(buf, "TEMP:", 5) == 0) {
      return atof(buf + 5);
    }
  }
  return -999.0;  // 通信失败
}

int WaterControl::_requestFlow() {
  _sendCmd("FLOW?");

  char buf[32];
  if (_readResponse(buf, sizeof(buf), 200)) {
    // 格式: FLOW:NNN
    if (strncmp(buf, "FLOW:", 5) == 0) {
      return atoi(buf + 5);
    }
  }
  return 0;  // 通信失败或无脉冲
}

void WaterControl::_notifyRelay(int pin, bool on) {
  char buf[20];
  snprintf(buf, sizeof(buf), "RELAY:%d:%d", pin, on ? 1 : 0);
  _sendCmd(buf);

  // 等待 PC 确认 (短暂，不阻塞主循环)
  char resp[8];
  _readResponse(resp, sizeof(resp), 50);
}

#endif // WATER_CONTROL_HIL_H
