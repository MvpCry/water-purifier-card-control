/**
 * 净水机刷卡控制系统 - 配置文件 (HIL + ST7789 彩屏版)
 *
 * 硬件平台: Arduino Uno / Mega 2560
 * 屏幕:    ST7789 1.3" 240x240 IPS TFT (SPI)
 * 读卡器:  RFID-RC522 (SPI 共享总线)
 * 传感器:  PC 模拟 (温度/流量)
 * 继电器:  PC 模拟
 */

#ifndef CONFIG_HIL_H
#define CONFIG_HIL_H

// ============================
// 引脚定义
// ============================

// ---- RFID-RC522 模块 (SPI) ----
#define RFID_SS_PIN     10    // SDA / SS
#define RFID_RST_PIN    9     // RST
// SPI 共享: SCK=D13, MOSI=D11, MISO=D12

// ---- ST7789 1.3" 彩屏 (SPI, 与 RFID 共享总线) ----
#define TFT_CS          7     // 片选
#define TFT_DC          4     // 数据/命令
#define TFT_RST         6     // 复位
#define TFT_BL          5     // 背光 (PWM 可控, 也可接 3.3V)
// SPI 共享: SCK=D13, MOSI=D11

// LED 指示灯已移除 — 状态通过 ST7789 彩屏颜色/图标显示

// ---- 蜂鸣器 ----
#define BUZZER_PIN       8

// ============================
// 水量参数
// ============================

#define PULSES_PER_LITER    450
#define DEFAULT_PURE_WATER_ML   500
#define DEFAULT_HOT_WATER_ML    300
#define MAX_WATER_ML            2000

// ============================
// 温度参数
// ============================

#define HOT_WATER_TARGET_TEMP   92.0
#define HOT_WATER_MIN_TEMP      85.0
#define HEATER_HYSTERESIS       3.0
#define MAX_HEAT_TIME_SEC       300

// ============================
// 时间参数 (毫秒)
// ============================

#define CARD_SCAN_INTERVAL      200
#define DISPENSE_TIMEOUT        60000
#define DISPLAY_REFRESH         500   // 彩屏刷新更快
#define BUZZER_SHORT_BEEP       100
#define BUZZER_LONG_BEEP        500

// ============================
// 卡号定义
// ============================

const byte ADMIN_CARD[4]   = {0xAB, 0xCD, 0xEF, 0x01};

const byte USER_CARDS[3][4] = {
  {0x12, 0x34, 0x56, 0x01},
  {0x12, 0x34, 0x56, 0x02},
  {0x12, 0x34, 0x56, 0x03},
};

const int USER_CARD_TYPES[] = {0, 1, 2};

#define USER_CARD_COUNT (sizeof(USER_CARDS) / sizeof(USER_CARDS[0]))

#endif // CONFIG_HIL_H
