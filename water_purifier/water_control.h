/**
 * 净水机刷卡控制系统 - 出水控制模块
 *
 * 负责: 电磁阀/水泵控制、流量计量、温度检测、加热控制、超时/断流保护
 * 依赖: OneWire + DallasTemperature 库 (DS18B20)
 *
 * 硬件:
 *   继电器(净化水) -> D5
 *   继电器(热水)   -> D6
 *   继电器(加热器) -> D7
 *   DS18B20        -> D3 (4.7kΩ上拉至VCC)
 *   流量计(YF-S201) -> D2 (中断)
 */

#ifndef WATER_CONTROL_H
#define WATER_CONTROL_H

#include "config.h"

#if USE_TEMP_SENSOR
  #include <OneWire.h>
  #include <DallasTemperature.h>
#endif

// ============================
// 系统状态枚举
// ============================

enum SystemState {
  STATE_IDLE,        // 待机
  STATE_DISPENSING,  // 出水中
  STATE_HEATING,     // 加热中
  STATE_ERROR,       // 故障
};

#if USE_TEMP_SENSOR
  extern OneWire oneWire;
  extern DallasTemperature sensors;
#endif

// ============================
// WaterControl 类
// ============================

class WaterControl {
public:
  /**
   * 初始化硬件引脚和传感器
   */
  void begin();

  /**
   * 开始净化水出水
   * targetMl: 目标出水量 (毫升), 0=使用默认值
   * 返回: true=启动成功, false=失败 (当前忙或故障)
   */
  bool startPureWater(int targetMl = 0);

  /**
   * 开始热水出水
   * targetMl: 目标出水量 (毫升), 0=使用默认值
   * 返回: true=启动成功, false=失败 (忙、故障、或水温不足)
   */
  bool startHotWater(int targetMl = 0);

  /**
   * 立即停止出水
   */
  void stopDispensing();

  /**
   * 获取当前水温 (℃)
   * 内部自动处理 DS18B20 的非阻塞温度转换
   */
  float getTemperature();

  /**
   * 获取当前已出水量 (毫升)
   */
  int getDispensedMl();

  /**
   * 获取目标出水量 (毫升)
   */
  int getTargetMl() const { return _targetMl; }

  /**
   * 获取当前系统状态
   */
  SystemState getState() const { return _state; }

  /**
   * 主循环更新 - 需在 loop() 中持续调用
   * 负责: 流量→毫升换算、超时检测、断流检测、加热温度控制
   */
  void update();

  /**
   * 热水是否就绪 (达到最低出水温度)
   */
  bool isHotWaterReady();

  /**
   * 流量计中断服务函数 - 须在硬件中断中调用
   */
  void flowPulseISR();

  /**
   * 设置加热器开关
   */
  void setHeater(bool on);

private:
  // ---- 硬件状态 ----
  SystemState _state;
  bool _relayPure;
  bool _relayHot;
  bool _heaterOn;

  // ---- 水量计量 ----
  volatile unsigned long _pulseCount;   // 脉冲累计 (ISR修改, volatile必须)
  volatile unsigned long _lastFlowPulseMs; // 最后一次脉冲时间 (ISR修改)
  int _targetMl;
  int _dispensedMl;
  unsigned long _dispenseStartMs;

  // ---- 温度传感器 (非阻塞读取) ----
  float _currentTemp;
  bool _tempConversionStarted;
  unsigned long _tempRequestMs;
  static const unsigned long TEMP_CONVERSION_MS = 800; // DS18B20 12位转换时间

  // ---- 内部方法 ----
  void _resetCounters();
  void _openValve(int relayPin);
  void _closeAllValves();
  int _pulsesToMl(unsigned long pulses);
};

// ============================
// 实现
// ============================

void WaterControl::begin() {
  // ---- 引脚初始化 ----
  pinMode(RELAY_PURE_WATER, OUTPUT);
  pinMode(RELAY_HOT_WATER,  OUTPUT);
  pinMode(RELAY_HEATER,     OUTPUT);
  // LED_READY (D13) 与 SPI-SCK 冲突, 不初始化, 用 TFT 屏幕代替
  pinMode(LED_PURE_DISPENSE, OUTPUT);
  pinMode(LED_HOT_DISPENSE,  OUTPUT);
  pinMode(LED_ERROR,         OUTPUT);
  pinMode(BUZZER_PIN,        OUTPUT);
  pinMode(FLOW_METER_PIN,    INPUT_PULLUP);

  // 全部关闭
  _closeAllValves();
  setHeater(false);
  digitalWrite(BUZZER_PIN, LOW);

  // ---- 温度传感器 (可选) ----
#if USE_TEMP_SENSOR
  pinMode(TEMP_SENSOR_PIN, INPUT_PULLUP);
  delay(5);
  sensors.begin();
  if (sensors.getDeviceCount() > 0) {
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    _tempConversionStarted = true;
    _tempRequestMs = millis();
  } else {
    _tempConversionStarted = false;
  }
#else
  _tempConversionStarted = false;
#endif

  // ---- 状态初始化 ----
  _state           = STATE_IDLE;
  _pulseCount      = 0;
  _targetMl        = 0;
  _dispensedMl     = 0;
  _dispenseStartMs = 0;
  _currentTemp     = 25.0;  // 默认室温，首次温度读取后更新
  _lastFlowPulseMs = 0;

  // LED_READY (D13) 禁用 — 与 SPI-SCK 冲突
}

// ---- 出水控制 ----

bool WaterControl::startPureWater(int targetMl) {
  if (_state != STATE_IDLE) return false;

  if (targetMl <= 0) {
    _targetMl = DEFAULT_PURE_WATER_ML;
  } else if (targetMl > MAX_WATER_ML) {
    _targetMl = MAX_WATER_ML;
  } else {
    _targetMl = targetMl;
  }

  _resetCounters();
  _openValve(RELAY_PURE_WATER);

  _state            = STATE_DISPENSING;
  _dispenseStartMs  = millis();
  _lastFlowPulseMs  = millis();

  digitalWrite(LED_PURE_DISPENSE, HIGH);

  return true;
}

bool WaterControl::startHotWater(int targetMl) {
  if (_state != STATE_IDLE) return false;

  // 水温不够不允许出热水
  if (!isHotWaterReady()) return false;

  if (targetMl <= 0) {
    _targetMl = DEFAULT_HOT_WATER_ML;
  } else if (targetMl > MAX_WATER_ML) {
    _targetMl = MAX_WATER_ML;
  } else {
    _targetMl = targetMl;
  }

  _resetCounters();
  _openValve(RELAY_HOT_WATER);

  _state            = STATE_DISPENSING;
  _dispenseStartMs  = millis();
  _lastFlowPulseMs  = millis();

  digitalWrite(LED_HOT_DISPENSE, HIGH);

  return true;
}

void WaterControl::stopDispensing() {
  _closeAllValves();

  // 最终水量换算
  _dispensedMl = _pulsesToMl(_pulseCount);

  _state = STATE_IDLE;

  digitalWrite(LED_PURE_DISPENSE, LOW);
  digitalWrite(LED_HOT_DISPENSE, LOW);
  // LED_READY (D13) 禁用 — SPI-SCK 冲突
}

// ---- 温度读取 (非阻塞) ----

float WaterControl::getTemperature() {
#if USE_TEMP_SENSOR
  if (_tempConversionStarted &&
      millis() - _tempRequestMs >= TEMP_CONVERSION_MS) {

    float temp = sensors.getTempCByIndex(0);

    // DS18B20 通信正常时返回有效温度 (> -100℃)
    // 返回值 -127℃ 表示传感器未连接或故障
    if (temp > -100.0) {
      _currentTemp = temp;
    }

    // 启动下一次转换
    sensors.requestTemperatures();
    _tempRequestMs = millis();
  }
#endif
  return _currentTemp;
}

// ---- 水量查询 ----

int WaterControl::getDispensedMl() {
  _dispensedMl = _pulsesToMl(_pulseCount);
  return _dispensedMl;
}

// ---- 主循环更新 ----

void WaterControl::update() {
  unsigned long now = millis();

  switch (_state) {

    case STATE_DISPENSING: {
      _dispensedMl = _pulsesToMl(_pulseCount);

      // --- 达到目标水量 → 正常停止 ---
      if (_dispensedMl >= _targetMl) {
        stopDispensing();
        return;
      }

      // --- 出水超时保护 (整桶水空了) ---
      if (now - _dispenseStartMs > DISPENSE_TIMEOUT) {
        stopDispensing();
        _state = STATE_ERROR;
        digitalWrite(LED_ERROR, HIGH);
        // 蜂鸣报警
        digitalWrite(BUZZER_PIN, HIGH);
        delay(BUZZER_LONG_BEEP);
        digitalWrite(BUZZER_PIN, LOW);
        return;
      }

      // --- 断流检测 (2秒无脉冲, 且脉冲计数为0) ---
      // 注意: 用 volatile 读取 _lastFlowPulseMs；如果流量计在 2s 内
      // 一个脉冲都没有 → 可能水桶空或管路堵塞
      unsigned long lastPulse = _lastFlowPulseMs;
      if (lastPulse > 0 &&
          (now - lastPulse) > NO_FLOW_TIMEOUT &&
          _pulseCount == 0) {
        stopDispensing();
        _state = STATE_ERROR;
        digitalWrite(LED_ERROR, HIGH);
      }
      break;
    }

    case STATE_HEATING: {
      float temp = getTemperature();

      if (_heaterOn) {
        // --- 达到目标温度 → 停止加热 ---
        if (temp >= HOT_WATER_TARGET_TEMP) {
          setHeater(false);
        }
        // --- 加热超时保护 (5分钟) ---
        else if (now - _dispenseStartMs > (unsigned long)MAX_HEAT_TIME_SEC * 1000UL) {
          setHeater(false);
          _state = STATE_ERROR;
          digitalWrite(LED_ERROR, HIGH);
        }
      } else {
        // --- 低于回差温度 → 重新加热 ---
        if (temp < HOT_WATER_TARGET_TEMP - HEATER_HYSTERESIS) {
          setHeater(true);
          _dispenseStartMs = now;  // 重置加热计时
        }
      }

      // --- 热水就绪 → 回到待机 ---
      if (isHotWaterReady()) {
        _state = STATE_IDLE;
      }
      break;
    }

    case STATE_ERROR: {
      // 错误状态: 闪烁故障 LED (500ms 周期)
      static unsigned long lastToggle = 0;
      if (now - lastToggle >= 500) {
        lastToggle = now;
        digitalWrite(LED_ERROR, !digitalRead(LED_ERROR));
      }
      break;
    }

    default:
      break;
  }
}

// ---- 热水就绪判断 ----

bool WaterControl::isHotWaterReady() {
#if USE_TEMP_SENSOR
  return (getTemperature() >= HOT_WATER_MIN_TEMP);
#else
  return false;  // 无传感器时不允许出热水
#endif
}

// ---- 流量计 ISR (中断上下文) ----

void WaterControl::flowPulseISR() {
  _pulseCount++;
  _lastFlowPulseMs = millis();  // 记录最后一次脉冲的时间戳
}

// ---- 加热器控制 ----

void WaterControl::setHeater(bool on) {
  _heaterOn = on;
  digitalWrite(RELAY_HEATER, on ? HIGH : LOW);

  if (on) {
    _state = STATE_HEATING;
  }
}

// ============================
// 内部辅助方法
// ============================

void WaterControl::_resetCounters() {
  _pulseCount  = 0;
  _dispensedMl = 0;
}

void WaterControl::_openValve(int relayPin) {
  digitalWrite(relayPin, HIGH);
  _relayPure = (relayPin == RELAY_PURE_WATER);
  _relayHot  = (relayPin == RELAY_HOT_WATER);
}

void WaterControl::_closeAllValves() {
  digitalWrite(RELAY_PURE_WATER, LOW);
  digitalWrite(RELAY_HOT_WATER, LOW);
  _relayPure = false;
  _relayHot  = false;
}

int WaterControl::_pulsesToMl(unsigned long pulses) {
  // YF-S201: 450 脉冲 = 1 升 = 1000 毫升
  return (int)((pulses * 1000UL) / PULSES_PER_LITER);
}

#endif // WATER_CONTROL_H
