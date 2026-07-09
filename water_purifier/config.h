/**
 * 净水机刷卡控制系统 - 配置文件 (ST7789 TFT 彩屏版)
 *
 * 硬件平台: Arduino Uno / Mega 2560
 * 屏幕:    ST7789 240x240 IPS TFT (SPI)
 * 读卡器:  RFID-RC522 (SPI 共享总线)
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================
// 引脚定义
// ============================

// ---- RFID-RC522 模块 (SPI) ----
#define RFID_SS_PIN      10    // SDA / SS
#define RFID_RST_PIN     4     // RST (从 D9 移来, D9 已被 TFT DC 占用)
// SPI 共享: SCK=D13, MOSI=D11, MISO=D12

// ---- ST7789 彩屏 (SPI, 与 RFID 共享总线) ----
#define TFT_CS           -1    // 7针模块无 CS 引脚(模块内部接地), -1 禁用
#define TFT_DC           9     // 数据/命令
#define TFT_RST          8     // 复位 (RFID 通信时拉低可防花屏)
// TFT 背光 → 3.3V (硬件常亮, 不占 Arduino 引脚)
// D7 空闲可用 (原来预留给 TFT_CS, 现在不需要了)

// ---- 继电器控制 (HIGH=开启, LOW=关闭) ----
#define RELAY_PURE_WATER  5    // 净化水电磁阀/水泵
#define RELAY_HOT_WATER   6    // 热水电磁阀/水泵
#define RELAY_HEATER      A3   // 加热器继电器 (从 D7 移来)

// ---- DS18B20 温度传感器 (OneWire) ----
#define TEMP_SENSOR_PIN   3    // 需 4.7kΩ 上拉电阻至 VCC

// ---- 流量计 (霍尔传感器脉冲) ----
#define FLOW_METER_PIN    2    // 中断引脚

// ---- LED 指示灯 ----
#define LED_READY         13   // 就绪灯 (与 SPI-SCK 共用, 通信时闪烁属正常)
#define LED_PURE_DISPENSE A0   // 净化水出水指示
#define LED_HOT_DISPENSE  A1   // 热水出水指示
#define LED_ERROR         A2   // 故障指示

// ---- 蜂鸣器 ----
#define BUZZER_PIN        A4   // (从 D8 移来)

// ---- 温度传感器开关 (有 DS18B20 时改为 1) ----
#define USE_TEMP_SENSOR  0   // 0=禁用(省 Flash), 1=启用

// ============================
// 水量参数
// ============================

#define PULSES_PER_LITER      450   // YF-S201: 450脉冲/升
#define DEFAULT_PURE_WATER_ML  500   // 净化水默认 500ml
#define DEFAULT_HOT_WATER_ML   300   // 热水默认 300ml
#define MAX_WATER_ML           2000  // 单次最大 2000ml

// ============================
// 温度参数
// ============================

#define HOT_WATER_TARGET_TEMP  92.0  // 热水目标温度
#define HOT_WATER_MIN_TEMP     85.0  // 热水最低允许出水温度
#define HEATER_HYSTERESIS      3.0   // 加热回差
#define MAX_HEAT_TIME_SEC      300   // 加热超时 (秒)
#define TEMP_READ_INTERVAL     2000  // 温度读取间隔 (ms)

// ============================
// 时间参数 (毫秒)
// ============================

#define CARD_SCAN_INTERVAL     200   // 刷卡扫描间隔
#define DISPENSE_TIMEOUT       60000 // 出水超时 (60秒)
#define NO_FLOW_TIMEOUT        2000  // 断流检测 (2秒)
#define DISPLAY_REFRESH        500   // 屏幕刷新间隔
#define ERROR_RECOVER_MS       10000 // 错误自动恢复 (10秒)
#define BUZZER_SHORT_BEEP      100
#define BUZZER_LONG_BEEP       500

// ============================
// 卡号定义
// ============================

const byte ADMIN_CARD[4] = {0xAB, 0xCD, 0xEF, 0x01};

const byte USER_CARDS[][4] = {
  {0x12, 0x34, 0x56, 0x01},
  {0x12, 0x34, 0x56, 0x02},
  {0x12, 0x34, 0x56, 0x03},
};
#define USER_CARD_COUNT (sizeof(USER_CARDS) / sizeof(USER_CARDS[0]))

const int USER_CARD_TYPES[] = {0, 1, 2};  // 0=净化水, 1=热水, 2=均可

#endif // CONFIG_H
