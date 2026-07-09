/**
 * 净水机刷卡控制系统 - 配置文件
 *
 * 硬件平台: Arduino Uno / Mega 2560
 * 功能: 刷卡控制出净化水或热水
 *
 * 修改说明:
 *   - 修改出水量: 改 DEFAULT_PURE_WATER_ML / DEFAULT_HOT_WATER_ML
 *   - 添加/删除用户卡: 修改 USER_CARDS 和 USER_CARD_TYPES
 *   - 修改热水温度: 改 HOT_WATER_TARGET_TEMP / HOT_WATER_MIN_TEMP
 *   - 修改流量计系数: 不同流量计脉冲数不同，改 PULSES_PER_LITER
 *   - LCD地址不对: 改 LCD_I2C_ADDR (常见: 0x27 或 0x3F)
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================
// 引脚定义
// ============================

// RFID-RC522 模块 (SPI)
#define RFID_SS_PIN      10    // SDA / SS
#define RFID_RST_PIN     9     // RST
// MOSI=11, MISO=12, SCK=13 (硬件SPI固定)

// 继电器控制 (LOW=关闭, HIGH=开启)
#define RELAY_PURE_WATER  5    // 净化水电磁阀/水泵
#define RELAY_HOT_WATER   6    // 热水电磁阀/水泵
#define RELAY_HEATER      7    // 加热器继电器

// DS18B20 温度传感器 (OneWire)
#define TEMP_SENSOR_PIN   3    // 需 4.7kΩ 上拉电阻至 VCC

// 流量计 (霍尔传感器脉冲)
#define FLOW_METER_PIN    2    // 中断引脚 (Uno: 0/1, Mega: 2/3)

// LED 指示灯
#define LED_READY         13   // 就绪指示灯 (Uno板载LED)
#define LED_PURE_DISPENSE A0   // 净化水出水指示
#define LED_HOT_DISPENSE  A1   // 热水出水指示
#define LED_ERROR         A2   // 故障指示灯

// 蜂鸣器
#define BUZZER_PIN        8

// I2C LCD1602 地址
#define LCD_I2C_ADDR      0x27 // 常见: 0x27 或 0x3F (用I2C扫描器确认)

// ============================
// 水量参数
// ============================

// 流量计: YF-S201, 450脉冲 = 1升 = 1000毫升
// 其他型号: YF-B1≈660, YF-B2≈600, OF05ZAT≈2170
#define PULSES_PER_LITER      450

// 默认出水量 (单位: 毫升)
#define DEFAULT_PURE_WATER_ML  500   // 净化水默认 500ml
#define DEFAULT_HOT_WATER_ML   300   // 热水默认 300ml
#define MAX_WATER_ML           2000  // 单次最大出水量 2000ml

// ============================
// 温度参数
// ============================

#define HOT_WATER_TARGET_TEMP  92.0  // 热水目标温度 (℃)
#define HOT_WATER_MIN_TEMP     85.0  // 热水最低允许出水温度 (℃)
#define HEATER_HYSTERESIS      3.0   // 加热回差 (℃) — 低于目标3度重新加热
#define MAX_HEAT_TIME_SEC      300   // 最大连续加热时间 (秒), 超时报警
#define TEMP_READ_INTERVAL     2000  // 温度读取间隔 (毫秒)

// ============================
// 时间参数 (单位: 毫秒)
// ============================

#define CARD_SCAN_INTERVAL     200   // 刷卡扫描间隔
#define DISPENSE_TIMEOUT       60000 // 出水超时 (60秒)
#define NO_FLOW_TIMEOUT        2000  // 断流检测超时 (2秒无脉冲=水桶空)
#define DISPLAY_REFRESH        1000  // 屏幕刷新间隔
#define ERROR_RECOVER_MS       10000 // 错误自动恢复时间 (10秒)
#define BUZZER_SHORT_BEEP      100   // 短蜂鸣 (毫秒)
#define BUZZER_LONG_BEEP       500   // 长蜂鸣 (毫秒)

// ============================
// 卡号定义 (预存授权卡号)
// ============================

// 管理员卡 (可配置系统参数)
const byte ADMIN_CARD[4] = {0xAB, 0xCD, 0xEF, 0x01};

// 普通用户卡 (只能出水)
const byte USER_CARDS[][4] = {
  {0x12, 0x34, 0x56, 0x01},  // 用户卡1
  {0x12, 0x34, 0x56, 0x02},  // 用户卡2
  {0x12, 0x34, 0x56, 0x03},  // 用户卡3
};
#define USER_CARD_COUNT (sizeof(USER_CARDS) / sizeof(USER_CARDS[0]))

// 用户卡出水类型: 0=净化水, 1=热水, 2=均可
const int USER_CARD_TYPES[] = {0, 1, 2};

#endif // CONFIG_H
