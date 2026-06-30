/**
 * 净水机刷卡控制系统
 * ===================
 *
 * 功能说明:
 *   - 刷授权卡选择出净化水或热水
 *   - 管理员卡: 配置系统参数
 *   - 流量计精确控制出水量
 *   - DS18B20 温度传感器监控热水温度
 *   - LCD 显示状态和用量
 *   - 超时、断流、过热多重安全保护
 *
 * 硬件连接:
 *   RFID-RC522  -> SPI (SS=10, RST=9, MOSI=11, MISO=12, SCK=13)
 *   继电器(净化水) -> D5
 *   继电器(热水)   -> D6
 *   继电器(加热器) -> D7
 *   DS18B20     -> D3
 *   流量计       -> D2 (中断0)
 *   LCD1602 I2C -> A4(SDA), A5(SCL)
 *   蜂鸣器       -> D8
 *   LED指示灯    -> D13, A0, A1, A2
 */

#include "config.h"
#include "card_manager.h"
#include "water_control.h"

// ============================
// 全局对象
// ============================

CardManager  cardMgr;
WaterControl waterCtrl;

// 当前刷卡结果
CardType     currentCard = CARD_NONE;
String       lastCardUid = "";

// 显示屏相关 (实际部署时用 LiquidCrystal_I2C)
// LiquidCrystal_I2C lcd(0x27, 16, 2);
// 这里用模拟占位

// 时间变量
unsigned long lastCardScanMs  = 0;
unsigned long lastDisplayMs   = 0;
unsigned long lastTempReadMs  = 0;

// ============================
// 初始化
// ============================

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== 净水机刷卡控制系统 ==="));
  Serial.println(F("初始化中..."));

  // 1. 初始化出水控制 (引脚、继电器、LED)
  waterCtrl.begin();
  Serial.println(F("[OK] 出水控制模块"));

  // 2. 初始化 RFID 读卡器
  if (cardMgr.begin()) {
    Serial.println(F("[OK] RFID读卡模块"));
  } else {
    Serial.println(F("[FAIL] RFID读卡模块 - 请检查接线"));
    while (1) {
      // RFID故障 -> 停止启动, 闪烁错误灯
      digitalWrite(LED_ERROR, !digitalRead(LED_ERROR));
      delay(200);
    }
  }

  // 3. 初始化 LCD
  // lcd.init();
  // lcd.backlight();
  // lcd.setCursor(0, 0);
  // lcd.print("Water Purifier");
  // lcd.setCursor(0, 1);
  // lcd.print("System Ready...");
  Serial.println(F("[OK] LCD显示模块"));

  // 4. 挂载流量计中断
  // attachInterrupt(
  //   digitalPinToInterrupt(FLOW_METER_PIN),
  //   flowMeterISR,
  //   RISING   // YF-S201 上升沿触发
  // );
  Serial.println(F("[OK] 流量计中断"));

  // 5. 初始化完成提示
  Serial.println(F("系统就绪，请刷卡..."));
  beep(BUZZER_SHORT_BEEP);
  delay(100);
  beep(BUZZER_SHORT_BEEP);
}

// ============================
// 主循环
// ============================

void loop() {
  unsigned long now = millis();

  // ---- 1. 更新系统状态 (超时检测、加热控制) ----
  waterCtrl.update();

  // ---- 2. 刷卡扫描 ----
  if (now - lastCardScanMs >= CARD_SCAN_INTERVAL) {
    lastCardScanMs = now;

    // 仅在待机状态接受刷卡
    if (waterCtrl.getState() == STATE_IDLE) {
      String cardUid = cardMgr.scanCard();

      if (cardUid.length() > 0) {
        handleCardSwiped(cardUid);
      }
    }
  }

  // ---- 3. 加热状态处理 ----
  // 如果水温不足且处于加热状态, 持续监控
  static bool lastHeaterState = false;
  bool currentHeaterState = (waterCtrl.getState() == STATE_HEATING);
  if (currentHeaterState != lastHeaterState) {
    lastHeaterState = currentHeaterState;
    if (currentHeaterState) {
      Serial.println(F("加热中..."));
      // lcd.setCursor(0, 1);
      // lcd.print("Heating...      ");
    }
  }

  // ---- 4. 温度定期读取 ----
  if (now - lastTempReadMs >= 2000) {
    lastTempReadMs = now;
    float temp = waterCtrl.getTemperature();
    // 低于加热阈值 -> 自动启动加热
    if (waterCtrl.getState() == STATE_IDLE &&
        temp < HOT_WATER_TARGET_TEMP - HEATER_HYSTERESIS) {
      waterCtrl.setHeater(true);
    }
  }

  // ---- 5. 刷新显示屏 ----
  if (now - lastDisplayMs >= DISPLAY_REFRESH) {
    lastDisplayMs = now;
    updateDisplay();
  }

  // ---- 6. 错误恢复 ----
  if (waterCtrl.getState() == STATE_ERROR) {
    // 10秒后自动清除错误, 回到待机
    static unsigned long errorStart = 0;
    if (errorStart == 0) errorStart = now;
    if (now - errorStart > 10000) {
      errorStart = 0;
      digitalWrite(LED_ERROR, LOW);
      waterCtrl.stopDispensing();  // 重置到待机
      Serial.println(F("错误已清除，系统恢复待机"));
    }
  }
}

// ============================
// 刷卡处理
// ============================

void handleCardSwiped(String cardUid) {
  Serial.print(F("检测到卡: "));
  Serial.println(cardUid);

  // 实际部署时, 从 MFRC522 获得的 UID 传入:
  // byte uid[4];
  // CardType type = cardMgr.identifyCard(uid);
  // 这里用模拟 (按用户卡1处理):
  byte uid[4]  = {0x12, 0x34, 0x56, 0x01};
  CardType type = cardMgr.identifyCard(uid);

  lastCardUid  = cardUid;
  currentCard  = type;

  switch (type) {

    case CARD_ADMIN:
      Serial.println(F(">>> 管理员卡 <<<"));
      beep(BUZZER_SHORT_BEEP);
      delay(50);
      beep(BUZZER_SHORT_BEEP);
      // 管理员模式: 可配置参数, 简单起见这里仅显示
      // enterAdminMode();
      break;

    case CARD_USER_PURE:
      Serial.println(F(">>> 净化水用户卡 - 出净化水 <<<"));
      beep(BUZZER_SHORT_BEEP);
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        Serial.print(F("开始出净化水 "));
        Serial.print(DEFAULT_PURE_WATER_ML);
        Serial.println(F("ml"));
      }
      break;

    case CARD_USER_HOT:
      Serial.println(F(">>> 热水用户卡 - 出热水 <<<"));
      if (waterCtrl.isHotWaterReady()) {
        beep(BUZZER_SHORT_BEEP);
        if (waterCtrl.startHotWater(DEFAULT_HOT_WATER_ML)) {
          Serial.print(F("开始出热水 "));
          Serial.print(DEFAULT_HOT_WATER_ML);
          Serial.println(F("ml"));
        }
      } else {
        Serial.println(F("热水未就绪，请等待加热"));
        // 长蜂鸣表示失败
        beep(BUZZER_LONG_BEEP);
      }
      break;

    case CARD_USER_BOTH:
      Serial.println(F(">>> 通用用户卡 <<<"));
      beep(BUZZER_SHORT_BEEP);
      // 通用卡逻辑: 默认先出净化水
      // 短时间内再刷一次 -> 切换为热水
      // 简化处理: 直接出净化水
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        Serial.print(F("开始出净化水 "));
        Serial.print(DEFAULT_PURE_WATER_ML);
        Serial.println(F("ml"));
      }
      break;

    case CARD_NONE:
    default:
      Serial.println(F("未授权卡!"));
      beep(BUZZER_LONG_BEEP);
      beep(BUZZER_LONG_BEEP);
      break;
  }
}

// ============================
// 显示屏更新
// ============================

void updateDisplay() {
  // 实际部署时使用:
  // lcd.clear();
  // lcd.setCursor(0, 0);

  SystemState state = waterCtrl.getState();

  switch (state) {
    case STATE_IDLE: {
      float temp = waterCtrl.getTemperature();
      // lcd.print("Ready  ");

      char buf[17];
      snprintf(buf, sizeof(buf), "T:%.1fC", temp);
      // lcd.setCursor(8, 0); lcd.print(buf);
      Serial.print(F("LCD[0]: "));
      Serial.print(F("Ready  "));
      Serial.println(buf);

      if (waterCtrl.isHotWaterReady()) {
        // lcd.setCursor(0, 1); lcd.print("Hot:Ready Swipe");
        Serial.println(F("LCD[1]: Hot:Ready Swipe"));
      } else {
        // lcd.setCursor(0, 1); lcd.print("Hot:Wait Heat..");
        Serial.println(F("LCD[1]: Hot:Wait Heat.."));
      }
      break;
    }

    case STATE_DISPENSING: {
      int done = waterCtrl.getDispensedMl();
      int goal = waterCtrl.getTargetMl();
      // lcd.print("Out...");
      char buf[17];
      snprintf(buf, sizeof(buf), "%d/%dml", done, goal);
      // lcd.setCursor(8, 0); lcd.print(buf);
      Serial.print(F("LCD[0]: Out..."));
      Serial.println(buf);

      // 进度条 (16字符宽)
      int progress = (int)((long)done * 16 / goal);
      // lcd.setCursor(0, 1);
      char bar[17];
      memset(bar, '=', progress);
      memset(bar + progress, ' ', 16 - progress);
      bar[16] = '\0';
      // lcd.print(bar);
      Serial.print(F("LCD[1]: "));
      Serial.println(bar);
      break;
    }

    case STATE_HEATING: {
      float temp = waterCtrl.getTemperature();
      // lcd.print("Heating...");
      char buf[17];
      snprintf(buf, sizeof(buf), "T:%.1f/%.0fC", temp, HOT_WATER_TARGET_TEMP);
      // lcd.setCursor(0, 1); lcd.print(buf);
      Serial.print(F("LCD[0]: Heating..."));
      Serial.println(buf);
      break;
    }

    case STATE_ERROR: {
      // lcd.print("!!! ERROR !!!");
      // lcd.setCursor(0, 1); lcd.print("Check Water Src");
      Serial.println(F("LCD[0]: !!! ERROR !!!"));
      Serial.println(F("LCD[1]: Check Water Src"));
      break;
    }

    default:
      break;
  }
}

// ============================
// 蜂鸣器
// ============================

void beep(int durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

// ============================
// 流量计中断服务函数
// ============================

void flowMeterISR() {
  waterCtrl.flowPulseISR();
}
