/**
 * 净水机刷卡控制系统 — ST7789 TFT 彩屏版
 * ========================================
 *
 * 屏幕: ST7789 240x240 IPS TFT (SPI)
 * 读卡: RFID-RC522 (SPI 共享总线)
 * 传感: DS18B20 + YF-S201 流量计 (真实硬件)
 *
 * 依赖库 (Arduino 库管理器安装):
 *   - MFRC522 by GithubCommunity
 *   - Adafruit GFX Library by Adafruit
 *   - Adafruit ST7789 Library by Adafruit  (需额外安装)
 *   - OneWire by Paul Stoffregen
 *   - DallasTemperature by Miles Burton
 */

#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
// OneWire+DallasTemperature 默认禁用, 需温控时在 config.h 启用 USE_TEMP_SENSOR

#include "config.h"
#include "card_manager.h"
#include "water_control.h"
#include "cn_font.h"

// ============================
// 全局硬件对象
// ============================

MFRC522         mfrc522(RFID_SS_PIN, RFID_RST_PIN);
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

CardManager  cardMgr;
WaterControl waterCtrl;

// ============================
// 状态变量
// ============================

unsigned long lastCardScanMs  = 0;
unsigned long lastDisplayMs   = 0;
unsigned long lastTempReadMs  = 0;
unsigned long errorStartMs    = 0;
bool          errorTiming     = false;

// 实时时钟 (开机时从 PC 串口同步, 之后用 millis 推算)
// 编译时自动获取 PC 时间 (__TIME__ + 8h → 北京時間)
// 格式 "HH:MM:SS", 例如 "14:30:05"
unsigned long bootMillis  = 0;
unsigned long syncedSec   = 0;

// 显示增量刷新
SystemState   lastDispState   = STATE_IDLE;
float         lastDispTemp    = -999;
int           lastDispensedMl = -1;
bool          lastHotReady    = false;

// ============================
// UI 颜色定义 (RGB565)
// ============================

#define C_BG          0x2104   // 深蓝背景 (比纯黑亮, 能看出是蓝色)
#define C_PANEL       0x3186   // 稍亮的面板背景
#define C_TITLE       0xFFFF   // 白色标题
#define C_TEXT        0xCE59   // 浅灰文字
#define C_DIM         0x8410   // 灰色 (更亮)
#define C_TEMP        0x07FF   // 青色温度
#define C_HOT         0xF9A0   // 橙红 (热水/加热)
#define C_PURE        0x07E0   // 绿色 (净化水, 更亮)
#define C_OK          0x07E0   // 绿色 (就绪)
#define C_WARN        0xFD20   // 橙黄 (警告)
#define C_ERR         0xF800   // 红色 (故障)
#define C_PROGRESS_BG 0x632C   // 进度条背景灰
#define C_WHITE       0xFFFF
#define C_BLACK       0x0000

// ============================
// 初始化
// ============================

void setup() {
  Serial.begin(9600);
  Serial.println(F("WaterPurifier v2"));

  // ---- 1. ST7789 屏幕 ----
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(2);
  drawSplash();

  // ---- 2. 出水控制模块 ----
  waterCtrl.begin();

  // 直接进入主界面
  tft.fillScreen(C_WHITE);
  lastDispState = (SystemState)-1;
  updateDisplay();

  // ---- 3. RFID 读卡器 ----
  if (cardMgr.begin()) {
    // OK
  } else {
    tft.fillScreen(C_ERR);
    tft.setTextColor(C_WHITE);
    tft.setTextSize(2);
    tft.setCursor(30, 80);
    tft.print("RFID ERROR!");
    tft.setCursor(15, 115);
    tft.print("Check D10(SS)");
    tft.setCursor(15, 140);
    tft.print("     D4(RST)");
    while (1) {
      digitalWrite(LED_ERROR, !digitalRead(LED_ERROR));
      delay(200);
    }
  }

  // ---- 4. 挂载流量计中断 ----
  attachInterrupt(
    digitalPinToInterrupt(FLOW_METER_PIN),
    flowMeterISR,
    RISING
  );

  // ---- 5. 自动时间同步 (编译时获取 PC 时间 +8h → 北京) ----
  // __TIME__ = "HH:MM:SS" (PC 本地时间, 编译时固定)
  int h, m, s;
  sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
  h = (h + 8) % 24;  // UTC+8 北京時間
  syncedSec = (unsigned long)h * 3600 + (unsigned long)m * 60 + s;
  bootMillis = millis();

  // ---- 6. 初始化完成 ----
  Serial.println(F("Ready"));
  beep(BUZZER_SHORT_BEEP);
  delay(80);
  beep(BUZZER_SHORT_BEEP);

  tft.fillScreen(C_WHITE);
  lastDispState = (SystemState)-1;
  updateDisplay();
}

// ============================
// 启动画面 (白底 + 中文标题 + 校名)
// ============================

void drawSplash() {
  tft.fillScreen(C_WHITE);

  // 中间: "净水器" 大字
  drawJingshuiqi(55);

  // 底部: 学校名
  drawSchoolName(175);

  // 加载提示
  tft.setTextColor(C_DIM);
  tft.setTextSize(1);
  tft.setCursor(80, 215);
  tft.print("Loading...");
}

// ============================
// 主循环
// ============================

void loop() {
  unsigned long now = millis();

  // ---- 1. 系统状态更新 ----
  waterCtrl.update();

  // ---- 2. 刷卡扫描 ----
  // 注意: 7针 TFT 模块无 CS 引脚, RFID 通信时会干扰屏幕
  // 解决: (a) 有卡时强制重绘  (b) 每 2 秒周期性全屏刷新
  if (now - lastCardScanMs >= CARD_SCAN_INTERVAL) {
    lastCardScanMs = now;

    if (waterCtrl.getState() == STATE_IDLE) {
      String cardUid = cardMgr.scanCard();
      if (cardUid.length() > 0) {
        handleCardSwiped(cardUid);
      }
    }
    // 无论有无卡, RFID 扫描都可能干扰无 CS 的 TFT, 标记刷新
    static unsigned long lastForceRefresh = 0;
    if (now - lastForceRefresh >= 2000) {
      lastForceRefresh = now;
      lastDispState = (SystemState)-1; // 强制下次 updateDisplay 全屏重绘
    }
  }

  // ---- 3. 温度读取 + 自动加热 ----
  if (now - lastTempReadMs >= TEMP_READ_INTERVAL) {
    lastTempReadMs = now;
    float temp = waterCtrl.getTemperature();

    if (waterCtrl.getState() == STATE_IDLE &&
        temp < HOT_WATER_TARGET_TEMP - HEATER_HYSTERESIS &&
        temp > -100.0) {
      waterCtrl.setHeater(true);
    }
  }

  // ---- 4. 刷新显示屏 ----
  if (now - lastDisplayMs >= DISPLAY_REFRESH) {
    lastDisplayMs = now;
    updateDisplay();
  }

  // ---- 5. 错误自动恢复 ----
  if (waterCtrl.getState() == STATE_ERROR) {
    if (!errorTiming) {
      errorStartMs = now;
      errorTiming  = true;
    }
    if (now - errorStartMs >= ERROR_RECOVER_MS) {
      errorTiming = false;
      digitalWrite(LED_ERROR, LOW);
      waterCtrl.stopDispensing();
      // error cleared
      tft.fillScreen(C_WHITE);
      lastDispState = STATE_IDLE;
    }
  } else {
    errorTiming = false;
  }
}

// ============================
// 刷卡处理
// ============================

void handleCardSwiped(String cardUidStr) {
  Serial.print(F("检测到卡: "));
  Serial.println(cardUidStr);

  CardType type = cardMgr.identifyCard();

  switch (type) {

    case CARD_ADMIN:
      Serial.println(F(">>> 管理员卡 <<<"));
      beep(BUZZER_SHORT_BEEP);
      delay(50);
      beep(BUZZER_SHORT_BEEP);
      drawCardPopup("Admin Card", "Config Mode", C_WARN);
      delay(1200);
      break;

    case CARD_USER_PURE:
      Serial.println(F(">>> 净化水用户卡 <<<"));
      beep(BUZZER_SHORT_BEEP);
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        drawCardPopup("Pure Water", "Dispensing...", C_PURE);
        delay(600);
        Serial.print(F("开始出净化水 "));
        Serial.print(DEFAULT_PURE_WATER_ML);
        Serial.println(F("ml"));
      }
      break;

    case CARD_USER_HOT:
      Serial.println(F(">>> 热水用户卡 <<<"));
      if (waterCtrl.isHotWaterReady()) {
        beep(BUZZER_SHORT_BEEP);
        if (waterCtrl.startHotWater(DEFAULT_HOT_WATER_ML)) {
          drawCardPopup("Hot Water", "Dispensing...", C_HOT);
          delay(600);
          Serial.print(F("开始出热水 "));
          Serial.print(DEFAULT_HOT_WATER_ML);
          Serial.println(F("ml"));
        }
      } else {
        Serial.println(F("热水未就绪"));
        beep(BUZZER_LONG_BEEP);
        delay(200);
        beep(BUZZER_LONG_BEEP);
        drawCardPopup("Not Ready", "Wait Heating...", C_ERR);
        delay(1500);
      }
      break;

    case CARD_USER_BOTH:
      Serial.println(F(">>> 通用用户卡 <<<"));
      beep(BUZZER_SHORT_BEEP);
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        drawCardPopup("Universal", "Pure Water", C_OK);
        delay(600);
      }
      break;

    case CARD_NONE:
    default:
      Serial.println(F("未授权卡!"));
      beep(BUZZER_LONG_BEEP);
      delay(200);
      beep(BUZZER_LONG_BEEP);
      drawCardPopup("Unauthorized!", cardUidStr.c_str(), C_ERR);
      delay(1500);
      break;
  }

  // 弹窗结束后恢复当前状态画面
  tft.fillScreen(C_WHITE);
  lastDispState = waterCtrl.getState();
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
// 显示屏更新 (增量刷新)
// ============================

void updateDisplay() {
  SystemState state = waterCtrl.getState();
  float temp = waterCtrl.getTemperature();
  int   done = waterCtrl.getDispensedMl();
  int   goal = waterCtrl.getTargetMl();
  bool  hotReady = waterCtrl.isHotWaterReady();

  // 状态/数据没变化则跳过刷新
  if (state == lastDispState &&
      state == STATE_IDLE &&
      abs(temp - lastDispTemp) < 0.5 &&
      hotReady == lastHotReady) return;

  if (state == STATE_DISPENSING && done == lastDispensedMl) return;

  if (state == STATE_HEATING &&
      abs(temp - lastDispTemp) < 0.3) return;

  lastDispState   = state;
  lastDispTemp    = temp;
  lastDispensedMl = done;
  lastHotReady    = hotReady;

  switch (state) {
    case STATE_IDLE:       drawIdleScreen(temp, hotReady); break;
    case STATE_DISPENSING: drawDispensingScreen(done, goal); break;
    case STATE_HEATING:    drawHeatingScreen(temp);        break;
    case STATE_ERROR:      drawErrorScreen();              break;
    default: break;
  }
}

// ==========================================
// 待机画面
// ==========================================

void drawIdleScreen(float temp, bool hotReady) {
  tft.fillScreen(C_WHITE);

  // ---- 顶栏 (先画, 循环画线兼容性更好) ----
  for (int16_t i = 0; i < 34; i++) {
    tft.drawFastHLine(0, i, 240, C_PANEL);
  }

  // 实时北京時間 = 同步基准 + 开机后经过的秒数
  unsigned long nowSec = syncedSec + (millis() - bootMillis) / 1000;
  int hh = (nowSec / 3600) % 24;
  int mm = (nowSec / 60) % 60;
  int ss = nowSec % 60;
  char tbuf[12];
  snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", hh, mm, ss);

  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print(tbuf);

  tft.setTextSize(1);
  tft.setCursor(170, 14);
  tft.print("T:");
  tft.print(temp, 1);
  tft.print("C");
  if (hotReady) {
    tft.fillCircle(228, 18, 5, C_OK);
  }

  // ---- 中间: "净水器" 大字 ----
  drawJingshuiqi(60);

  // ---- 底部: 学校名 ----
  drawSchoolName(175);

  // ---- 底部提示 ----
  tft.setTextSize(1);
  tft.setTextColor(C_DIM);
  tft.setCursor(65, 215);
  tft.print("Tap card...");
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
  tft.setCursor(40, 10);
  tft.print("Dispensing");

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

  // ---- 底部 ----
  tft.setTextColor(C_DIM);
  tft.setTextSize(1);
  tft.setCursor(80, 210);
  tft.print("Please wait...");
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
  tft.setCursor(55, 10);
  tft.print("Heating...");

  // ---- 当前温度 ----
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
  tft.setCursor(65, 130);
  tft.print("Target: ");
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
    tft.fillRoundRect(barX, barY, fill, barH, barR, 0xFD20);
  }

  // ---- 底部说明 ----
  tft.setTextColor(C_DIM);
  tft.setTextSize(1);
  tft.setCursor(30, 200);
  tft.print("Need >= ");
  tft.print((int)HOT_WATER_MIN_TEMP);
  tft.print(" C to dispense");
}

// ==========================================
// 故障画面
// ==========================================

void drawErrorScreen() {
  tft.fillScreen(C_ERR);
  tft.setTextColor(C_WHITE);

  tft.setTextSize(3);
  tft.setCursor(30, 50);
  tft.print("!! ERROR !!");

  tft.setTextSize(2);
  tft.setCursor(20, 110);
  tft.print("Timeout/No Flow");

  tft.setTextSize(1);
  tft.setCursor(25, 160);
  tft.print("Check water source");
  tft.setCursor(35, 195);
  tft.print("Auto-recover 10s");
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
// 流量计中断服务函数 (ISR)
// ============================

void flowMeterISR() {
  waterCtrl.flowPulseISR();
}

// ============================
// PROGMEM 位图绘制 (从闪存读取, 免占 RAM)
// ============================

void drawBitmapP(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fg, uint16_t bg) {
  int16_t byteW = (w + 7) / 8;
  tft.fillRect(x, y, w, h, bg);  // 先填背景

  for (int16_t row = 0; row < h; row++) {
    for (int16_t col = 0; col < w; col++) {
      uint8_t byte = pgm_read_byte(bitmap + row * byteW + col / 8);
      if (byte & (1 << (7 - (col % 8)))) {
        // 找到前景像素 → 扫描连续段
        int16_t runEnd = col + 1;
        while (runEnd < w) {
          uint8_t nb = pgm_read_byte(bitmap + row * byteW + runEnd / 8);
          if (!(nb & (1 << (7 - (runEnd % 8))))) break;
          runEnd++;
        }
        tft.drawFastHLine(x + col, y + row, runEnd - col, fg);
        col = runEnd - 1;
      }
    }
  }
}

// ============================
// 中文字符绘制
// ============================

void drawJingshuiqi(uint16_t y) {
  int w1 = pgm_read_byte(cn_jing);
  int w2 = pgm_read_byte(cn_shui);
  int w3 = pgm_read_byte(cn_qi);
  int totalW = w1 + w2 + w3 + 12;
  int x = (240 - totalW) / 2;

  drawBitmapP(x, y, cn_jing + 2, w1, pgm_read_byte(cn_jing + 1), C_BLACK, C_WHITE);
  x += w1 + 6;
  drawBitmapP(x, y, cn_shui + 2, w2, pgm_read_byte(cn_shui + 1), C_BLACK, C_WHITE);
  x += w2 + 6;
  drawBitmapP(x, y, cn_qi + 2, w3, pgm_read_byte(cn_qi + 1), C_BLACK, C_WHITE);
}

void drawSchoolName(uint16_t y) {
  const uint8_t* chars[] = {cn_tai, cn_shan, cn_zhi, cn_ye, cn_ji, cn_shu, cn_xue, cn_yuan};
  int count = 8, gap = 3;

  int totalW = 0;
  for (int i = 0; i < count; i++) totalW += pgm_read_byte(chars[i]);
  totalW += gap * (count - 1);

  int x = (240 - totalW) / 2;
  for (int i = 0; i < count; i++) {
    int w = pgm_read_byte(chars[i]);
    int h = pgm_read_byte(chars[i] + 1);
    drawBitmapP(x, y, chars[i] + 2, w, h, C_BLACK, C_WHITE);
    x += w + gap;
  }
}
