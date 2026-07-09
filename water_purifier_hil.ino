/**
 * 净水机刷卡控制系统 - 硬件在环 (HIL) + ST7789 彩屏版
 * =====================================================
 *
 * 屏幕: ST7789 1.3" 240x240 IPS TFT (SPI)
 *       库: Adafruit ST7789 + Adafruit GFX
 *
 * 硬件接线:
 *   Arduino      →  外设
 *   ──────────────────────────
 *   D10 (SS)     →  RFID-RC522 SDA
 *   D9           →  RFID-RC522 RST
 *   D11 (MOSI)   →  RFID-RC522 MOSI  +  ST7789 SDA
 *   D12 (MISO)   →  RFID-RC522 MISO
 *   D13 (SCK)    →  RFID-RC522 SCK   +  ST7789 SCL
 *   D7           →  ST7789 CS
 *   D4           →  ST7789 DC
 *   D6           →  ST7789 RST
 *   D5           →  ST7789 BLK (背光, 可选)
 *   D8           →  蜂鸣器 (+)
 *   (LED 已移除 — 状态通过彩屏显示)
 *
 * 传感器/继电器: PC 模拟器通过 Serial/USB 提供
 */

#include "config_hil.h"
#include "card_manager.h"
#include "water_control_hil.h"

// 显示库
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// RFID 库
#include <MFRC522.h>

// ============================
// 全局对象
// ============================

CardManager  cardMgr;
WaterControl waterCtrl;

// ST7789 显示 (CS=D7, DC=D4, RST=D6)
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// RFID 读卡器
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// 当前刷卡结果
CardType     currentCard = CARD_NONE;
String       lastCardUid = "";

// 时间变量
unsigned long lastCardScanMs  = 0;
unsigned long lastDisplayMs   = 0;
unsigned long lastTempReadMs  = 0;

// 显示状态 (用于减少重复绘制)
int           lastDispensedMl = -1;
SystemState   lastState       = STATE_IDLE;
float         lastTemp        = -1;
bool          lastHotReady    = false;

// ============================
// UI 颜色定义 (RGB565)
// ============================

#define C_BG          0x0841   // 深蓝灰背景
#define C_PANEL       0x10A2   // 面板背景
#define C_TITLE       0xFFFF   // 白色标题
#define C_TEXT        0xCE59   // 浅灰文字
#define C_DIM         0x6B4D   // 暗灰
#define C_TEMP        0x07FF   // 青色温度
#define C_HOT         0xF9A0   // 橙红 (热水/加热)
#define C_PURE        0x2F3F   // 蓝绿 (净化水)
#define C_OK          0x2727   // 绿色 (就绪)
#define C_WARN        0xFD20   // 橙黄 (警告)
#define C_ERR         0xF800   // 红色 (故障)
#define C_PROGRESS_BG 0x39E7   // 进度条背景灰
#define C_WHITE       0xFFFF
#define C_BLACK       0x0000

// ============================
// 初始化
// ============================

void setup() {
  // 串口 (与 PC 模拟器通信)
  Serial.begin(115200);
  delay(1000);

  // ---- 1. ST7789 屏幕 ----
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);   // 背光开

  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(0);           // 0=竖屏, 2=倒竖屏 (按实际调整)
  tft.fillScreen(C_BG);
  drawSplash();
  delay(1500);

  // ---- 2. 出水控制 (LED + 蜂鸣器) ----
  waterCtrl.begin();

  // ---- 3. RFID ----
  SPI.begin();
  mfrc522.PCD_Init(RFID_SS_PIN, RFID_RST_PIN);
  mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_max);

  byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  if (version == 0x00 || version == 0xFF) {
    tft.fillScreen(C_ERR);
    tft.setTextColor(C_WHITE);
    tft.setTextSize(2);
    tft.setCursor(30, 80);
    tft.print("RFID ERROR!");
    tft.setCursor(20, 120);
    tft.print("Check Wiring");
    while (1) {
      delay(500);  // 死锁: RFID 故障, 检查接线后复位
    }
  }

  // ---- 4. 初始化完成 ----
  Serial.println(F("系统就绪 (HIL + ST7789)"));
  beep(BUZZER_SHORT_BEEP);
  delay(80);
  beep(BUZZER_SHORT_BEEP);

  tft.fillScreen(C_BG);
  lastState = STATE_IDLE;
  updateDisplay();
}

// ---- 启动画面 ----
void drawSplash() {
  tft.fillScreen(0x000F);  // 深蓝
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(40, 70);
  tft.print("Water");
  tft.setCursor(55, 100);
  tft.print("Purifier");
  tft.setTextSize(1);
  tft.setTextColor(C_DIM);
  tft.setCursor(45, 160);
  tft.print("HIL + ST7789");
  tft.setCursor(55, 180);
  tft.print("Starting...");
}

// ============================
// 主循环
// ============================

void loop() {
  unsigned long now = millis();

  // ---- 1. 更新系统状态 ----
  waterCtrl.update();

  // ---- 2. 刷卡扫描 ----
  if (now - lastCardScanMs >= CARD_SCAN_INTERVAL) {
    lastCardScanMs = now;

    if (waterCtrl.getState() == STATE_IDLE) {
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        byte uid[4];
        for (byte i = 0; i < 4; i++) uid[i] = mfrc522.uid.uidByte[i];
        mfrc522.PICC_HaltA();

        char buf[13];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X",
                 uid[0], uid[1], uid[2], uid[3]);
        handleCardSwiped(String(buf), uid);
      }
    }
  }

  // ---- 3. 自动加热触发 ----
  if (now - lastTempReadMs >= 2000) {
    lastTempReadMs = now;
    float temp = waterCtrl.getTemperature();
    if (waterCtrl.getState() == STATE_IDLE &&
        temp < HOT_WATER_TARGET_TEMP - HEATER_HYSTERESIS) {
      waterCtrl.setHeater(true);
    }
  }

  // ---- 4. 刷新显示屏 ----
  if (now - lastDisplayMs >= DISPLAY_REFRESH) {
    lastDisplayMs = now;
    updateDisplay();
  }

  // ---- 5. 错误恢复 ----
  if (waterCtrl.getState() == STATE_ERROR) {
    static unsigned long errorStart = 0;
    if (errorStart == 0) errorStart = now;
    if (now - errorStart > 10000) {
      errorStart = 0;
      waterCtrl.stopDispensing();
    }
  }
}

// ============================
// 刷卡处理
// ============================

void handleCardSwiped(String cardUid, byte uid[4]) {
  Serial.print(F("Card: "));
  Serial.println(cardUid);

  CardType type = cardMgr.identifyCard(uid);
  lastCardUid  = cardUid;
  currentCard  = type;

  switch (type) {

    case CARD_ADMIN:
      beep(BUZZER_SHORT_BEEP);
      delay(50);
      beep(BUZZER_SHORT_BEEP);
      drawCardPopup("管理员卡", "Admin Card", C_WARN);
      delay(1200);
      break;

    case CARD_USER_PURE:
      beep(BUZZER_SHORT_BEEP);
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        drawCardPopup("净化水卡", "Pure Water", C_PURE);
        delay(600);
      }
      break;

    case CARD_USER_HOT:
      if (waterCtrl.isHotWaterReady()) {
        beep(BUZZER_SHORT_BEEP);
        if (waterCtrl.startHotWater(DEFAULT_HOT_WATER_ML)) {
          drawCardPopup("热水卡", "Hot Water", C_HOT);
          delay(600);
        }
      } else {
        beep(BUZZER_LONG_BEEP);
        drawCardPopup("热水未就绪", "Wait Heating", C_ERR);
        delay(1500);
      }
      break;

    case CARD_USER_BOTH:
      beep(BUZZER_SHORT_BEEP);
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        drawCardPopup("通用卡", "Universal", C_OK);
        delay(600);
      }
      break;

    case CARD_NONE:
    default:
      beep(BUZZER_LONG_BEEP);
      beep(BUZZER_LONG_BEEP);
      drawCardPopup("未授权卡!", cardUid.c_str(), C_ERR);
      delay(1500);
      break;
  }

  tft.fillScreen(C_BG);
  lastState = waterCtrl.getState();  // 强制刷新
}

// ---- 刷卡弹窗 ----
void drawCardPopup(const char* title, const char* sub, uint16_t color) {
  tft.fillRoundRect(20, 60, 200, 120, 12, color);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  int tw = strlen(title) * 18;
  tft.setCursor((240 - tw) / 2, 85);
  tft.print(title);
  tft.setTextSize(2);
  tw = strlen(sub) * 12;
  tft.setCursor((240 - tw) / 2, 130);
  tft.print(sub);
}

// ============================
// 显示屏更新 (ST7789 240x240)
// ============================

void updateDisplay() {
  SystemState state = waterCtrl.getState();
  float temp = waterCtrl.getTemperature();
  int done = waterCtrl.getDispensedMl();
  int goal = waterCtrl.getTargetMl();
  bool hotReady = waterCtrl.isHotWaterReady();

  // 状态没变且数据没变时跳过 (省刷新)
  if (state == lastState && state == STATE_IDLE &&
      abs(temp - lastTemp) < 0.5 && hotReady == lastHotReady) return;
  if (state == STATE_DISPENSING && done == lastDispensedMl) return;
  if (state == STATE_HEATING && abs(temp - lastTemp) < 0.3) return;

  lastState       = state;
  lastTemp        = temp;
  lastDispensedMl = done;
  lastHotReady    = hotReady;

  switch (state) {

    case STATE_IDLE:
      drawIdleScreen(temp, hotReady);
      break;

    case STATE_DISPENSING:
      drawDispensingScreen(done, goal);
      break;

    case STATE_HEATING:
      drawHeatingScreen(temp);
      break;

    case STATE_ERROR:
      drawErrorScreen();
      break;

    default:
      break;
  }
}

// ==========================================
// 待机画面
// ==========================================

void drawIdleScreen(float temp, bool hotReady) {
  tft.fillScreen(C_BG);

  // ---- 顶部状态栏 ----
  tft.fillRect(0, 0, 240, 40, C_PANEL);
  tft.setTextColor(C_TITLE);
  tft.setTextSize(2);
  tft.setCursor(15, 10);
  tft.print("净水机");
  // 右侧状态指示
  tft.fillCircle(210, 20, 7, hotReady ? C_OK : C_WARN);
  tft.setTextSize(1);
  tft.setCursor(180, 28);
  tft.setTextColor(hotReady ? C_OK : C_WARN);
  tft.print(hotReady ? "就绪" : "加热");

  // ---- 温度大数字 ----
  tft.setTextColor(C_TEMP);
  tft.setTextSize(5);
  char tbuf[10];
  snprintf(tbuf, sizeof(tbuf), "%.1f", temp);
  int tw = strlen(tbuf) * 30;  // size5 = ~30px/char
  tft.setCursor((240 - tw) / 2, 60);
  tft.print(tbuf);

  // ℃ 符号
  tft.setTextSize(3);
  tft.setCursor((240 - tw) / 2 + tw + 2, 70);
  tft.print("C");

  // 温度单位 (完整)
  tft.setTextSize(1);
  tft.setTextColor(C_DIM);
  tft.setCursor(105, 130);
  tft.print("摄氏度");

  // ---- 分隔线 ----
  tft.drawLine(40, 148, 200, 148, C_DIM);

  // ---- 状态文字 ----
  tft.setTextSize(2);
  if (hotReady) {
    tft.setTextColor(C_OK);
    tft.setCursor(55, 165);
    tft.print("热水就绪");
  } else {
    tft.setTextColor(C_WARN);
    tft.setCursor(55, 165);
    tft.print("等待加热");
  }

  // ---- 底部提示 ----
  tft.setTextSize(1);
  tft.setTextColor(C_DIM);
  tft.setCursor(55, 210);
  tft.print("请刷卡选水...");
}

// ==========================================
// 出水画面
// ==========================================

void drawDispensingScreen(int done, int goal) {
  tft.fillScreen(C_BG);

  // ---- 顶部 ----
  tft.fillRect(0, 0, 240, 40, C_PURE);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(60, 10);
  tft.print("出水中...");

  // ---- 水量数字 ----
  tft.setTextColor(C_WHITE);
  tft.setTextSize(4);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d/%d", done, goal);
  tft.setCursor(30, 65);
  tft.print(buf);

  tft.setTextSize(2);
  tft.setCursor(200, 75);
  tft.print("ml");

  // ---- 进度条 ----
  int barY = 120, barH = 30, barX = 20, barW = 200, barR = 8;
  tft.fillRoundRect(barX, barY, barW, barH, barR, C_PROGRESS_BG);

  int pct = (goal > 0) ? (int)((long)done * barW / goal) : 0;
  if (pct > barW) pct = barW;
  if (pct > 0) {
    tft.fillRoundRect(barX, barY, pct, barH, barR, C_PURE);
  }

  // ---- 百分比 ----
  int pp = (goal > 0) ? (int)((long)done * 100 / goal) : 0;
  if (pp > 100) pp = 100;
  tft.setTextColor(C_TEXT);
  tft.setTextSize(2);
  snprintf(buf, sizeof(buf), "%d%%", pp);
  tft.setCursor(100, 170);
  tft.print(buf);

  // ---- 底部水滴动画 (简化: 静态水滴) ----
  tft.setTextColor(C_PURE);
  tft.setTextSize(1);
  tft.setCursor(85, 210);
  tft.print("请稍候...");
}

// ==========================================
// 加热画面
// ==========================================

void drawHeatingScreen(float temp) {
  tft.fillScreen(C_BG);

  // ---- 顶部 ----
  tft.fillRect(0, 0, 240, 40, C_HOT);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(60, 10);
  tft.print("加热中...");

  // ---- 当前温度大数字 ----
  tft.setTextColor(C_HOT);
  tft.setTextSize(5);
  char buf[10];
  snprintf(buf, sizeof(buf), "%.1f", temp);
  int tw = strlen(buf) * 30;
  tft.setCursor((240 - tw) / 2, 60);
  tft.print(buf);

  tft.setTextSize(3);
  tft.setCursor((240 - tw) / 2 + tw + 2, 70);
  tft.print("C");

  // ---- 目标温度 ----
  tft.setTextColor(C_DIM);
  tft.setTextSize(1);
  tft.setCursor(75, 130);
  tft.print("目标 ");
  tft.print((int)HOT_WATER_TARGET_TEMP);
  tft.print(" C");

  // ---- 温度进度条 (25→92) ----
  int barY = 155, barH = 20, barX = 20, barW = 200, barR = 6;
  float range = HOT_WATER_TARGET_TEMP - 25.0;
  float progress = (temp - 25.0) / range;
  if (progress < 0) progress = 0;
  if (progress > 1) progress = 1;

  tft.fillRoundRect(barX, barY, barW, barH, barR, C_PROGRESS_BG);
  int fill = (int)(progress * barW);
  if (fill > 0) {
    // 渐变色: 橙→红
    uint16_t heatColor = (progress < 0.5)
      ? tft.color565(255, (int)(160 + progress*190), 0)
      : tft.color565(255, (int)(255 - (progress-0.5)*200), 0);
    tft.fillRoundRect(barX, barY, fill, barH, barR, heatColor);
  }

  // ---- 底部说明 ----
  tft.setTextColor(C_DIM);
  tft.setTextSize(1);
  tft.setCursor(40, 200);
  tft.print("热水需 ≥ ");
  tft.print((int)HOT_WATER_MIN_TEMP);
  tft.print(" C 方可出水");
}

// ==========================================
// 故障画面
// ==========================================

void drawErrorScreen() {
  tft.fillScreen(C_ERR);

  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  tft.setCursor(40, 50);
  tft.print("!! 故障 !!");

  tft.setTextSize(2);
  tft.setCursor(25, 110);
  tft.print("超时 / 断流");

  tft.setTextSize(1);
  tft.setCursor(30, 160);
  tft.print("请检查水源或管路");

  tft.setCursor(45, 195);
  tft.print("10 秒后自动恢复");

  // 倒计时进度条
  static unsigned long errStart = millis();
  int elapsed = (int)((millis() - errStart) / 1000);
  int remain = 10 - elapsed;
  if (remain < 0) remain = 0;

  tft.fillRoundRect(40, 175, 160, 12, 4, 0x4208);
  tft.fillRoundRect(40, 175, (10 - remain) * 16, 12, 4, C_WHITE);
}

// ============================
// 蜂鸣器
// ============================

void beep(int durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}
