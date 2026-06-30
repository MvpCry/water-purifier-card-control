# 净水机刷卡控制系统

基于 Arduino 的净水机刷卡控制方案。用户刷卡后自动控制净化水或热水出水，流量计精确计量。

## 功能

- **刷卡出水** — 刷授权卡自动出水，不同卡绑定不同出水类型
- **净化水 / 热水** — 两种出水模式，独立电磁阀/水泵控制
- **精确计量** — 霍尔流量计（YF-S201）计量出水量，到量自停
- **恒温控制** — DS18B20 测温 + 继电器控制加热器，带回差恒温
- **多重保护** — 出水超时、断流检测、加热超时、RFID 自检
- **状态显示** — LCD1602 显示温度、出水量、系统状态
- **声光提示** — 蜂鸣器 + LED 指示刷卡和运行状态

## 硬件清单

| 模块 | 型号 | 数量 |
|------|------|------|
| 主控板 | Arduino Uno / Mega 2560 | 1 |
| 读卡器 | RFID-RC522 (MFRC522) | 1 |
| 温度传感器 | DS18B20 防水探头 | 1 |
| 流量计 | YF-S201 霍尔流量计 | 1 |
| 显示屏 | LCD1602 I2C | 1 |
| 继电器模块 | 5V 双路/四路继电器 | 1 |
| 蜂鸣器 | 有源蜂鸣器 5V | 1 |
| LED | 5mm LED × 4 + 220Ω电阻 | 4 |
| S50 白卡/钥匙扣 | 13.56MHz Mifare | 若干 |

## 引脚接线

```
Arduino     ->  外设
=======================
D10 (SS)    ->  RFID-RC522 SDA
D9          ->  RFID-RC522 RST
D11 (MOSI)  ->  RFID-RC522 MOSI
D12 (MISO)  ->  RFID-RC522 MISO
D13 (SCK)   ->  RFID-RC522 SCK

D5          ->  继电器1 (净化水阀/泵)
D6          ->  继电器2 (热水阀/泵)
D7          ->  继电器3 (加热器)

D3          ->  DS18B20 DATA (4.7kΩ上拉)
D2          ->  流量计信号线 (中断)

A4 (SDA)    ->  LCD I2C SDA
A5 (SCL)    ->  LCD I2C SCL

D8          ->  蜂鸣器 (+)

D13         ->  LED 就绪指示灯
A0          ->  LED 净化水指示
A1          ->  LED 热水指示
A2          ->  LED 故障指示
```

## 依赖库

```
MFRC522 by GithubCommunity        (RFID读卡)
OneWire by Paul Stoffregen         (DS18B20)
DallasTemperature by Miles Burton  (DS18B20)
LiquidCrystal_I2C by Frank de Brabander (LCD)
```

## 编译和烧录

1. 用 Arduino IDE 打开 `净水机.ino`
2. 在库管理器中安装上述依赖库
3. 选择正确的开发板和端口
4. 取消 `card_manager.h` 和 `water_control.h` 中的注释
5. 编译并上传

## 使用说明

### 刷卡操作

| 卡类型 | 行为 |
|--------|------|
| 管理员卡 | 系统识别并发出双蜂鸣 (配置模式预留) |
| 净化水卡 | 出 500ml 净化水 |
| 热水卡 | 出 300ml 热水 (须水温 ≥ 85℃) |
| 通用卡 | 出 500ml 净化水 |
| 未授权卡 | 两声长蜂鸣拒绝 |

### 加热控制

- 自动加热: 水温 < 89℃ (92℃目标 - 3℃回差) 时启动加热
- 自动停止: 水温 ≥ 92℃ 时停止
- 加热超时: 超过 5 分钟报警

### 安全保护

- 出水超时 60 秒自动停止
- 断流 2 秒报警（水桶空检测）
- 加热超时 5 分钟报警
- RFID 故障时系统锁定

## 文件结构

```
净水机/
├── 净水机.ino          # 主程序 (setup/loop/刷卡处理/显示)
├── config.h            # 引脚定义和参数配置
├── card_manager.h      # RFID刷卡管理模块
├── water_control.h     # 出水控制模块 (流量/温度/阀门)
└── README.md           # 说明文档
```

## 定制修改

- **修改出水量**: 在 `config.h` 中改 `DEFAULT_PURE_WATER_ML` / `DEFAULT_HOT_WATER_ML`
- **添加/删除用户卡**: 在 `config.h` 中修改 `USER_CARDS` 和 `USER_CARD_TYPES` 数组
- **修改热水温度**: 改 `config.h` 中的 `HOT_WATER_TARGET_TEMP` 和 `HOT_WATER_MIN_TEMP`
- **修改流量计系数**: 不同流量计脉冲数不同，改 `PULSES_PER_LITER`
