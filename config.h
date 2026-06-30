/**
 * 净水机刷卡控制系统 - 配置文件
 *
 * 硬件平台: Arduino Uno / Mega 2560
 * 功能: 刷卡控制出净化水或热水
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================
// 引脚定义
// ============================

// RFID-RC522 模块 (SPI)
#define RFID_SS_PIN     10    // SDA / SS
#define RFID_RST_PIN    9     // RST

// 继电器控制
#define RELAY_PURE_WATER    5   // 净化水电磁阀/水泵
#define RELAY_HOT_WATER     6   // 热水电磁阀/水泵

// 加热器控制
#define RELAY_HEATER        7   // 加热器继电器

// DS18B20 温度传感器 (OneWire)
#define TEMP_SENSOR_PIN     3

// 流量计 (霍尔传感器脉冲)
#define FLOW_METER_PIN      2   // 中断引脚 (Uno 用 0 或 1, Mega 用 2 或 3)

// LED 指示灯
#define LED_READY           13  // 就绪指示灯 (板载LED)
#define LED_PURE_DISPENSE   A0  // 净化水出水指示
#define LED_HOT_DISPENSE    A1  // 热水出水指示
#define LED_ERROR           A2  // 故障指示灯

// 蜂鸣器
#define BUZZER_PIN          8

// I2C LCD1602
// SDA -> A4 (Uno) / 20 (Mega)
// SCL -> A5 (Uno) / 21 (Mega)

// ============================
// 水量参数 (单位: 脉冲数)
// ============================

// 流量计: YF-S201, 450脉冲 = 1升
#define PULSES_PER_LITER    450

// 默认出水量 (单位: 毫升)
#define DEFAULT_PURE_WATER_ML   500   // 净化水默认 500ml
#define DEFAULT_HOT_WATER_ML    300   // 热水默认 300ml
#define MAX_WATER_ML            2000  // 单次最大出水量 2000ml

// ============================
// 温度参数
// ============================

#define HOT_WATER_TARGET_TEMP   92.0  // 热水目标温度 (℃)
#define HOT_WATER_MIN_TEMP      85.0  // 热水最低温度 (℃)
#define HEATER_HYSTERESIS       3.0   // 加热回差 (℃)
#define MAX_HEAT_TIME_SEC       300   // 最大加热时间 (秒), 超时报警

// ============================
// 时间参数 (单位: 毫秒)
// ============================

#define CARD_SCAN_INTERVAL      200   // 刷卡扫描间隔
#define DISPENSE_TIMEOUT        60000 // 出水超时 (60秒)
#define DISPLAY_REFRESH         1000  // 屏幕刷新间隔
#define BUZZER_SHORT_BEEP       100   // 短蜂鸣
#define BUZZER_LONG_BEEP        500   // 长蜂鸣

// ============================
// 卡号定义 (预存授权卡号)
// ============================

// 管理员卡 (可配置参数)
const byte ADMIN_CARD[4]   = {0xAB, 0xCD, 0xEF, 0x01};

// 普通用户卡 (只能出水, 不能改参数)
const byte USER_CARDS[3][4] = {
  {0x12, 0x34, 0x56, 0x01},  // 用户卡1
  {0x12, 0x34, 0x56, 0x02},  // 用户卡2
  {0x12, 0x34, 0x56, 0x03},  // 用户卡3
};

// 用户卡绑定类型: 0=净化水, 1=热水, 2=均可
const int USER_CARD_TYPES[] = {0, 1, 2};

#define USER_CARD_COUNT (sizeof(USER_CARDS) / sizeof(USER_CARDS[0]))

#endif // CONFIG_H
