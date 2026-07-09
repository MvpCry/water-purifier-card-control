/**
 * 净水机刷卡控制系统 - PC 模拟器
 * ==================================
 *
 * 保留全部控制逻辑，用软件模拟替代 Arduino 硬件。
 * 编译: g++ -std=c++11 simulate.cpp -o simulate.exe
 * 运行: ./simulate.exe
 *
 * 操作键:
 *   [1] 净化水卡   [2] 热水卡   [3] 通用卡
 *   [A] 管理员卡   [H] 切换加热  [Q] 退出
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

#ifdef _WIN32
  #include <conio.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
  int _kbhit() {
    struct termios oldt, newt;
    int ch, oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if (ch != EOF) { ungetc(ch, stdin); return 1; }
    return 0;
  }
  int _getch() { return getchar(); }
#endif

// ============================
// 1. Arduino 硬件模拟层
// ============================

using Clock = std::chrono::steady_clock;
static auto g_startTime = Clock::now();

unsigned long millis() {
  auto now = Clock::now();
  return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
    now - g_startTime).count();
}

void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---- 引脚名称映射 ----
struct PinInfo {
  int   num;
  const char* name;
  const char* highMsg;   // 置高时含义
  const char* lowMsg;    // 置低时含义
};

static PinInfo pinTable[] = {
  { 5,  "净化水阀",   "开", "关" },
  { 6,  "热水阀",     "开", "关" },
  { 7,  "加热器",     "开", "关" },
  { 8,  "蜂鸣器",     "响", "停" },
  { 13, "就绪LED",    "亮", "灭" },
  { 14, "净化水LED",  "亮", "灭" },  // A0
  { 15, "热水LED",    "亮", "灭" },  // A1
  { 16, "故障LED",    "亮", "灭" },  // A2
  { 0,  nullptr, nullptr, nullptr }
};

static int  pinValues[64];      // 引脚当前值
static int  pinModes[64];       // 0=未设置, 1=OUTPUT, 2=INPUT

#define INPUT        0x0
#define OUTPUT       0x1
#define INPUT_PULLUP 0x2
#define HIGH         0x1
#define LOW          0x0
#define A0           14
#define A1           15
#define A2           16

void pinMode(int pin, int mode) {
  pinModes[pin] = (mode == INPUT || mode == INPUT_PULLUP) ? 2 : 1;
}

void digitalWrite(int pin, int value) {
  if (pinValues[pin] == value) return;  // 无变化则跳过
  pinValues[pin] = value;

  // 查找引脚名称
  for (int i = 0; pinTable[i].name; i++) {
    if (pinTable[i].num == pin) {
      const char* icon = (pin == 8) ? "🔊" :
                         (pin >= 14) ? "💡" : "🔧";
      std::cout << "  " << icon << " [" << pinTable[i].name << "] → "
                << (value ? pinTable[i].highMsg : pinTable[i].lowMsg)
                << std::endl;
      return;
    }
  }
}

int digitalRead(int pin) {
  return pinValues[pin];
}

// ---- Serial 模拟 ----
#define F(str) str

struct SerialClass {
  void begin(int) {}
  void print(const char* s)   { std::cout << s; }
  void print(int n)           { std::cout << n; }
  void print(float f)         { std::cout << std::fixed << std::setprecision(1) << f; }
  void print(char c)          { std::cout << c; }
  void println(const char* s = "") { std::cout << s << std::endl; }
  void println(int n)         { std::cout << n << std::endl; }
  void println(float f)       { std::cout << std::fixed << std::setprecision(1) << f << std::endl; }
};

static SerialClass Serial;


// ============================
// 2. 配置 (从 config.h 内联)
// ============================

#define RFID_SS_PIN           10
#define RFID_RST_PIN          9
#define RELAY_PURE_WATER      5
#define RELAY_HOT_WATER       6
#define RELAY_HEATER          7
#define TEMP_SENSOR_PIN       3
#define FLOW_METER_PIN        2
#define LED_READY             13
#define LED_PURE_DISPENSE     A0
#define LED_HOT_DISPENSE      A1
#define LED_ERROR             A2
#define BUZZER_PIN            8

#define PULSES_PER_LITER      450
#define DEFAULT_PURE_WATER_ML 500
#define DEFAULT_HOT_WATER_ML  300
#define MAX_WATER_ML          2000

#define HOT_WATER_TARGET_TEMP 92.0f
#define HOT_WATER_MIN_TEMP    85.0f
#define HEATER_HYSTERESIS     3.0f
#define MAX_HEAT_TIME_SEC     300

#define CARD_SCAN_INTERVAL    200
#define DISPENSE_TIMEOUT      60000
#define DISPLAY_REFRESH       1000
#define BUZZER_SHORT_BEEP     100
#define BUZZER_LONG_BEEP      500

// 管理员卡
const unsigned char ADMIN_CARD[4] = {0xAB, 0xCD, 0xEF, 0x01};

// 用户卡
const unsigned char USER_CARDS[3][4] = {
  {0x12, 0x34, 0x56, 0x01},  // 用户卡1
  {0x12, 0x34, 0x56, 0x02},  // 用户卡2
  {0x12, 0x34, 0x56, 0x03},  // 用户卡3
};

const int USER_CARD_TYPES[] = {0, 1, 2};
#define USER_CARD_COUNT (sizeof(USER_CARDS) / sizeof(USER_CARDS[0]))


// ============================
// 3. 枚举和 CardManager 类
// ============================

enum CardType {
  CARD_NONE      = -1,
  CARD_ADMIN     = 0,
  CARD_USER_PURE = 1,
  CARD_USER_HOT  = 2,
  CARD_USER_BOTH = 3,
};

class CardManager {
public:
  bool begin() {
    pinMode(RFID_SS_PIN, OUTPUT);
    pinMode(RFID_RST_PIN, OUTPUT);
    return true;  // 模拟初始化成功
  }

  /**
   * 模拟刷卡: 键盘输入已由主循环统一处理，此方法不再使用
   */
  std::string scanCard() {
    return "";  // 键盘输入由 main() 统一处理
  }

  /**
   * 根据按键返回模拟卡字符串，非刷卡键返回空
   */
  static std::string keyToCardId(int ch) {
    switch (ch) {
      case '1': return "SIM_CARD_PURE";
      case '2': return "SIM_CARD_HOT";
      case '3': return "SIM_CARD_BOTH";
      case 'a': case 'A': return "SIM_CARD_ADMIN";
      case 'x': case 'X': return "SIM_CARD_UNKNOWN";
      default:  return "";
    }
  }

  CardType identifyCard(const unsigned char uid[4]) {
    if (uidMatch(uid, ADMIN_CARD)) return CARD_ADMIN;
    for (int i = 0; i < (int)USER_CARD_COUNT; i++) {
      if (uidMatch(uid, USER_CARDS[i])) {
        switch (USER_CARD_TYPES[i]) {
          case 0: return CARD_USER_PURE;
          case 1: return CARD_USER_HOT;
          case 2: return CARD_USER_BOTH;
          default: return CARD_NONE;
        }
      }
    }
    return CARD_NONE;
  }

  bool selfTest() { return true; }

  /**
   * 根据模拟卡字符串返回 CardType（用于模拟）
   */
  CardType identifyByString(const std::string& cardStr) {
    if (cardStr == "SIM_CARD_PURE")   return CARD_USER_PURE;
    if (cardStr == "SIM_CARD_HOT")    return CARD_USER_HOT;
    if (cardStr == "SIM_CARD_BOTH")   return CARD_USER_BOTH;
    if (cardStr == "SIM_CARD_ADMIN")  return CARD_ADMIN;
    return CARD_NONE;
  }

private:
  bool uidMatch(const unsigned char uid1[4], const unsigned char uid2[4]) {
    for (int i = 0; i < 4; i++)
      if (uid1[i] != uid2[i]) return false;
    return true;
  }
};


// ============================
// 4. WaterControl 类 (含温度模型 + 模拟流量计)
// ============================

enum SystemState {
  STATE_IDLE,
  STATE_DISPENSING,
  STATE_HEATING,
  STATE_ERROR,
  STATE_PAUSED,
};

class WaterControl {
public:
  void begin() {
    pinMode(RELAY_PURE_WATER, OUTPUT);
    pinMode(RELAY_HOT_WATER,  OUTPUT);
    pinMode(RELAY_HEATER,     OUTPUT);
    pinMode(LED_READY,         OUTPUT);
    pinMode(LED_PURE_DISPENSE, OUTPUT);
    pinMode(LED_HOT_DISPENSE,  OUTPUT);
    pinMode(LED_ERROR,         OUTPUT);
    pinMode(BUZZER_PIN,        OUTPUT);
    pinMode(FLOW_METER_PIN,    INPUT_PULLUP);

    _closeAllValves();
    setHeater(false);

    _state          = STATE_IDLE;
    _pulseCount     = 0;
    _targetMl       = 0;
    _dispensedMl    = 0;
    _dispenseStartMs = 0;
    _currentTemp    = 25.0f;
    _lastFlowPulseMs = 0;
    _lastTempUpdateMs = millis();
    _heaterStartedMs  = 0;

    digitalWrite(LED_READY, HIGH);
  }

  bool startPureWater(int targetMl) {
    if (_state != STATE_IDLE) return false;

    if (targetMl <= 0)      _targetMl = DEFAULT_PURE_WATER_ML;
    else if (targetMl > MAX_WATER_ML) _targetMl = MAX_WATER_ML;
    else                    _targetMl = targetMl;

    _resetCounters();
    _openValve(RELAY_PURE_WATER);

    _state = STATE_DISPENSING;
    _dispenseStartMs = millis();
    _lastFlowPulseMs = millis();

    digitalWrite(LED_PURE_DISPENSE, HIGH);
    digitalWrite(LED_READY, LOW);
    return true;
  }

  bool startHotWater(int targetMl) {
    if (_state != STATE_IDLE) return false;
    if (!isHotWaterReady())   return false;

    if (targetMl <= 0)      _targetMl = DEFAULT_HOT_WATER_ML;
    else if (targetMl > MAX_WATER_ML) _targetMl = MAX_WATER_ML;
    else                    _targetMl = targetMl;

    _resetCounters();
    _openValve(RELAY_HOT_WATER);

    _state = STATE_DISPENSING;
    _dispenseStartMs = millis();
    _lastFlowPulseMs = millis();

    digitalWrite(LED_HOT_DISPENSE, HIGH);
    digitalWrite(LED_READY, LOW);
    return true;
  }

  void stopDispensing() {
    _closeAllValves();
    _state = STATE_IDLE;
    _dispensedMl = _pulsesToMl(_pulseCount);

    digitalWrite(LED_PURE_DISPENSE, LOW);
    digitalWrite(LED_HOT_DISPENSE, LOW);
    digitalWrite(LED_READY, HIGH);
  }

  float getTemperature() {
    // ---- 温度热力学模型 ----
    unsigned long now = millis();
    float dt = (now - _lastTempUpdateMs) / 1000.0f;  // 秒
    _lastTempUpdateMs = now;

    if (dt > 1.0f) dt = 1.0f;  // 防止跳帧导致温度突变

    if (_heaterOn) {
      // 加热: 升温 ~0.3°C/s
      _currentTemp += 0.3f * dt;
      if (_currentTemp > 100.0f) _currentTemp = 100.0f;  // 物理上限
    } else {
      // 自然降温: 向室温 25°C 衰减
      float decayRate = 0.05f;  // 降温系数
      _currentTemp -= decayRate * (_currentTemp - 25.0f) * dt;
      if (_currentTemp < 25.0f) _currentTemp = 25.0f;
    }

    return _currentTemp;
  }

  int getDispensedMl() {
    _dispensedMl = _pulsesToMl(_pulseCount);
    return _dispensedMl;
  }

  int getTargetMl()    { return _targetMl; }
  SystemState getState() { return _state; }

  void update() {
    unsigned long now = millis();

    // ---- 模拟流量计脉冲 ----
    // 出水时按 ~1 L/min 速率注入脉冲 (YF-S201: 450脉冲/升)
    if (_state == STATE_DISPENSING) {
      // 1 L/min = 1000/60 = 16.67 ml/s, 每 ml = 0.45 脉冲
      // 即 7.5 脉冲/秒, 间隔 ~133ms
      static unsigned long lastPulseInject = 0;
      unsigned long pulseInterval = (unsigned long)(1000.0f / (PULSES_PER_LITER * 1.0f / 60.0f));
      if (now - lastPulseInject >= pulseInterval) {
        lastPulseInject = now;
        flowPulseISR();
      }
    }

    // ---- 状态逻辑 (与原版一致) ----
    switch (_state) {

      case STATE_DISPENSING: {
        _dispensedMl = _pulsesToMl(_pulseCount);

        // 达到目标水量 -> 停止
        if (_dispensedMl >= _targetMl) {
          stopDispensing();
          std::cout << "  ✅ 出水完成: " << _dispensedMl << "ml" << std::endl;
          return;
        }

        // 超时保护
        if (now - _dispenseStartMs > DISPENSE_TIMEOUT) {
          stopDispensing();
          _state = STATE_ERROR;
          digitalWrite(LED_ERROR, HIGH);
          std::cout << "  ⚠️  出水超时! (>" << DISPENSE_TIMEOUT/1000 << "s)" << std::endl;
          digitalWrite(BUZZER_PIN, HIGH);
          delay(BUZZER_LONG_BEEP);
          digitalWrite(BUZZER_PIN, LOW);
          return;
        }

        // 断流检测 (2秒无脉冲且还没出过水)
        if (_lastFlowPulseMs > 0 &&
            (now - _lastFlowPulseMs) > 2000 &&
            _pulseCount == 0) {
          stopDispensing();
          _state = STATE_ERROR;
          digitalWrite(LED_ERROR, HIGH);
          std::cout << "  ⚠️  断流! (2秒无流量脉冲，水桶空?)" << std::endl;
        }
        break;
      }

      case STATE_HEATING: {
        float temp = getTemperature();

        if (_heaterOn) {
          if (temp >= HOT_WATER_TARGET_TEMP) {
            setHeater(false);
          } else if (now - _heaterStartedMs > MAX_HEAT_TIME_SEC * 1000UL) {
            setHeater(false);
            _state = STATE_ERROR;
            digitalWrite(LED_ERROR, HIGH);
            std::cout << "  ⚠️  加热超时! (>" << MAX_HEAT_TIME_SEC << "s)" << std::endl;
          }
        } else {
          if (temp < HOT_WATER_TARGET_TEMP - HEATER_HYSTERESIS) {
            setHeater(true);
            _heaterStartedMs = now;
          }
        }

        if (isHotWaterReady()) {
          _state = STATE_IDLE;
          digitalWrite(LED_READY, HIGH);
          std::cout << "  ✅ 热水就绪! " << _currentTemp << "℃" << std::endl;
        }
        break;
      }

      case STATE_ERROR: {
        static unsigned long lastErrorToggle = 0;
        if (now - lastErrorToggle > 500) {
          lastErrorToggle = now;
          digitalWrite(LED_ERROR, !digitalRead(LED_ERROR));
        }
        break;
      }

      default:
        break;
    }
  }

  bool isHotWaterReady() {
    return (getTemperature() >= HOT_WATER_MIN_TEMP);
  }

  void flowPulseISR() {
    _pulseCount++;
    _lastFlowPulseMs = millis();
  }

  void setHeater(bool on) {
    _heaterOn = on;
    digitalWrite(RELAY_HEATER, on ? HIGH : LOW);

    if (on) {
      _state = STATE_HEATING;
      _heaterStartedMs = millis();
      digitalWrite(LED_READY, LOW);
      std::cout << "  🔥 开始加热 (当前 " << _currentTemp
                << "℃ → 目标 " << HOT_WATER_TARGET_TEMP << "℃)" << std::endl;
    } else {
      std::cout << "  🔥 停止加热 (当前 " << _currentTemp << "℃)" << std::endl;
    }
  }

  // 手动切换加热（模拟用）
  void toggleHeater() {
    if (_state == STATE_HEATING) {
      setHeater(false);
      _state = STATE_IDLE;
      digitalWrite(LED_READY, HIGH);
    } else if (_state == STATE_IDLE) {
      setHeater(true);
    }
  }

  // 模拟断流: 手动清除脉冲（调试用）
  void simulateNoFlow() {
    _pulseCount = 0;
    _lastFlowPulseMs = 0;  // 强制触发断流检测
  }

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
  unsigned long _lastTempUpdateMs;

  unsigned long _lastFlowPulseMs;

  void _resetCounters() {
    _pulseCount  = 0;
    _dispensedMl = 0;
  }

  void _openValve(int relayPin) {
    digitalWrite(relayPin, HIGH);
    _relayPure = (relayPin == RELAY_PURE_WATER);
    _relayHot  = (relayPin == RELAY_HOT_WATER);
  }

  void _closeAllValves() {
    digitalWrite(RELAY_PURE_WATER, LOW);
    digitalWrite(RELAY_HOT_WATER, LOW);
    _relayPure = false;
    _relayHot  = false;
  }

  int _pulsesToMl(unsigned long pulses) {
    return (int)((pulses * 1000UL) / PULSES_PER_LITER);
  }
};


// ============================
// 5. 应用层 (主程序逻辑)
// ============================

CardManager  cardMgr;
WaterControl waterCtrl;

CardType     currentCard = CARD_NONE;
std::string  lastCardUid = "";

unsigned long lastDisplayMs   = 0;
unsigned long lastTempReadMs  = 0;

// 蜂鸣器模拟
void beep(int durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

// 进度条
void drawProgressBar(int done, int goal) {
  if (goal <= 0) goal = 1;
  int progress = (int)((long)done * 16 / goal);
  if (progress > 16) progress = 16;
  std::cout << "[";
  for (int i = 0; i < progress; i++) std::cout << "=";
  for (int i = progress; i < 16; i++) std::cout << " ";
  std::cout << "]";
}

// LCD 显示更新
void updateDisplay() {
  SystemState state = waterCtrl.getState();

  switch (state) {
    case STATE_IDLE: {
      float temp = waterCtrl.getTemperature();
      std::cout << "┌──────────────────┐" << std::endl;
      std::cout << "│ Ready     T:" << std::fixed << std::setprecision(1)
                << std::setw(5) << temp << "℃ │" << std::endl;
      if (waterCtrl.isHotWaterReady()) {
        std::cout << "│ Hot:Ready Swipe  │" << std::endl;
      } else {
        std::cout << "│ Hot:Wait Heat..  │" << std::endl;
      }
      std::cout << "└──────────────────┘" << std::endl;
      break;
    }

    case STATE_DISPENSING: {
      int done = waterCtrl.getDispensedMl();
      int goal = waterCtrl.getTargetMl();
      std::cout << "┌──────────────────┐" << std::endl;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "│ Out...%6d/%-5d│", done, goal);
      std::cout << buf << std::endl;
      std::cout << "│";
      drawProgressBar(done, goal);
      std::cout << "│" << std::endl;
      std::cout << "└──────────────────┘" << std::endl;
      break;
    }

    case STATE_HEATING: {
      float temp = waterCtrl.getTemperature();
      std::cout << "┌──────────────────┐" << std::endl;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "│ Heating...       │");
      std::cout << buf << std::endl;
      std::snprintf(buf, sizeof(buf), "│ T:%.1f/%.0f℃      │", temp, HOT_WATER_TARGET_TEMP);
      std::cout << buf << std::endl;
      std::cout << "└──────────────────┘" << std::endl;
      break;
    }

    case STATE_ERROR: {
      std::cout << "┌──────────────────┐" << std::endl;
      std::cout << "│ !!! ERROR !!!    │" << std::endl;
      std::cout << "│ Check Water Src  │" << std::endl;
      std::cout << "└──────────────────┘" << std::endl;
      break;
    }

    default:
      break;
  }
}

// 刷卡处理
void handleCardSwiped(const std::string& cardUid) {
  std::cout << "💳 检测到卡: " << cardUid << std::endl;

  CardType type = cardMgr.identifyByString(cardUid);
  lastCardUid  = cardUid;
  currentCard  = type;

  switch (type) {

    case CARD_ADMIN:
      std::cout << ">>> 管理员卡 <<<" << std::endl;
      beep(BUZZER_SHORT_BEEP);
      delay(50);
      beep(BUZZER_SHORT_BEEP);
      break;

    case CARD_USER_PURE:
      std::cout << ">>> 净化水用户卡 - 出净化水 <<<" << std::endl;
      beep(BUZZER_SHORT_BEEP);
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        std::cout << "开始出净化水 " << DEFAULT_PURE_WATER_ML << "ml" << std::endl;
      }
      break;

    case CARD_USER_HOT:
      std::cout << ">>> 热水用户卡 - 出热水 <<<" << std::endl;
      if (waterCtrl.isHotWaterReady()) {
        beep(BUZZER_SHORT_BEEP);
        if (waterCtrl.startHotWater(DEFAULT_HOT_WATER_ML)) {
          std::cout << "开始出热水 " << DEFAULT_HOT_WATER_ML << "ml" << std::endl;
        }
      } else {
        std::cout << "❌ 热水未就绪 (当前 " << waterCtrl.getTemperature()
                  << "℃, 需要 ≥" << HOT_WATER_MIN_TEMP << "℃)" << std::endl;
        beep(BUZZER_LONG_BEEP);
      }
      break;

    case CARD_USER_BOTH:
      std::cout << ">>> 通用用户卡 <<<" << std::endl;
      beep(BUZZER_SHORT_BEEP);
      if (waterCtrl.startPureWater(DEFAULT_PURE_WATER_ML)) {
        std::cout << "开始出净化水 " << DEFAULT_PURE_WATER_ML << "ml" << std::endl;
      }
      break;

    case CARD_NONE:
    default:
      std::cout << "❌ 未授权卡!" << std::endl;
      beep(BUZZER_LONG_BEEP);
      beep(BUZZER_LONG_BEEP);
      break;
  }
}

// 打印操作提示
void printHelp() {
  std::cout << std::endl;
  std::cout << "  ┌────────── 操作键 ──────────┐" << std::endl;
  std::cout << "  │ [1] 净化水卡  [2] 热水卡    │" << std::endl;
  std::cout << "  │ [3] 通用卡    [A] 管理员卡  │" << std::endl;
  std::cout << "  │ [H] 手动加热  [F] 模拟断流  │" << std::endl;
  std::cout << "  │ [T] 温度+状态 [Q] 退出      │" << std::endl;
  std::cout << "  └────────────────────────────┘" << std::endl;
}

// ---- 初始化 ----
void setup() {
  Serial.begin(9600);
  std::cout << std::endl;
  std::cout << "╔══════════════════════════════════╗" << std::endl;
  std::cout << "║  净水机刷卡控制系统 - PC 模拟器  ║" << std::endl;
  std::cout << "╚══════════════════════════════════╝" << std::endl;
  std::cout << std::endl;
  std::cout << "初始化中..." << std::endl;

  waterCtrl.begin();
  std::cout << "[OK] 出水控制模块 (模拟)" << std::endl;

  if (cardMgr.begin()) {
    std::cout << "[OK] RFID读卡模块 (模拟)" << std::endl;
  } else {
    std::cout << "[FAIL] RFID读卡模块" << std::endl;
    while (1) {
      digitalWrite(LED_ERROR, !digitalRead(LED_ERROR));
      delay(200);
    }
  }

  std::cout << "[OK] LCD显示模块 (模拟)" << std::endl;
  std::cout << "[OK] 流量计 (模拟)" << std::endl;

  std::cout << std::endl << "系统就绪，请刷卡..." << std::endl;
  beep(BUZZER_SHORT_BEEP);
  delay(100);
  beep(BUZZER_SHORT_BEEP);

  printHelp();
}


// ============================
// 6. 主循环
// ============================

int main() {
  setup();

  while (true) {
    unsigned long now = millis();

    // ---- 1. 更新系统状态 ----
    waterCtrl.update();

    // ---- 2. 键盘输入处理 ----
    if (_kbhit()) {
      int ch = _getch();

      // 2a. 刷卡键 (仅待机状态接受)
      std::string cardUid = CardManager::keyToCardId(ch);
      if (!cardUid.empty()) {
        if (waterCtrl.getState() == STATE_IDLE) {
          handleCardSwiped(cardUid);
        } else {
          std::cout << "  ⚠️  系统忙，请等待当前操作完成" << std::endl;
        }
      }
      // 2b. 控制键 (任何状态可用)
      else {
        switch (ch) {
          case 'h': case 'H':
            waterCtrl.toggleHeater();
            break;
          case 'f': case 'F':
            std::cout << "⚡ 手动触发断流模拟..." << std::endl;
            waterCtrl.simulateNoFlow();
            break;
          case 't': case 'T': {
            float t = waterCtrl.getTemperature();
            SystemState s = waterCtrl.getState();
            const char* stateNames[] = {"待机","出水中","加热中","故障","暂停"};
            std::cout << "🌡️  水温: " << t << "℃  状态: "
                      << stateNames[s] << std::endl;
            break;
          }
          case 'q': case 'Q':
          case '\033':  // ESC
            std::cout << std::endl << "退出模拟器" << std::endl;
            return 0;
        }
      }
    }

    // ---- 3. 温度定期读取 ----
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
      // 清屏
#ifdef _WIN32
      system("cls");
#else
      system("clear");
#endif
      std::cout << std::endl;
      updateDisplay();
      printHelp();
      std::cout << "> " << std::flush;
    }

    // ---- 5. 错误恢复 ----
    if (waterCtrl.getState() == STATE_ERROR) {
      static unsigned long errorStart = 0;
      if (errorStart == 0) errorStart = now;
      if (now - errorStart > 10000) {
        errorStart = 0;
        digitalWrite(LED_ERROR, LOW);
        waterCtrl.stopDispensing();
        std::cout << "  ✅ 错误已清除，系统恢复待机" << std::endl;
      }
    }

    // 主循环节拍 (~50Hz)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  return 0;
}
