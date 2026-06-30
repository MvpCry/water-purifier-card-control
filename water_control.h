/**
 * 净水机刷卡控制系统 - 出水控制模块
 *
 * 负责: 电磁阀/水泵控制、流量计量、温度检测、加热控制、超时保护
 * 依赖: OneWire + DallasTemperature 库 (DS18B20)
 */

#ifndef WATER_CONTROL_H
#define WATER_CONTROL_H

#include "config.h"

// ============================
// 系统状态枚举
// ============================

enum SystemState {
  STATE_IDLE,          // 待机
  STATE_DISPENSING,    // 出水中
  STATE_HEATING,       // 加热中
  STATE_ERROR,         // 故障
  STATE_PAUSED,        // 暂停 (流量计无脉冲)
};

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
   * 返回: true=启动成功, false=失败 (当前忙、故障、或水温不足)
   */
  bool startHotWater(int targetMl = 0);

  /**
   * 立即停止出水
   */
  void stopDispensing();

  /**
   * 获取当前水温 (℃)
   */
  float getTemperature();

  /**
   * 获取当前出水量 (已出毫升数)
   */
  int getDispensedMl();

  /**
   * 获取目标出水量 (毫升)
   */
  int getTargetMl();

  /**
   * 获取当前系统状态
   */
  SystemState getState();

  /**
   * 主循环更新 - 需在 loop() 中调用
   * 负责: 流量计脉冲累计、超时检测、加热控制
   */
  void update();

  /**
   * 热水是否就绪 (达到最低温度)
   */
  bool isHotWaterReady();

  /**
   * 流量计中断服务函数 - 须在硬件中断中调用
   */
  void flowPulseISR();

  /**
   * 设置加热器状态
   */
  void setHeater(bool on);

private:
  // 硬件状态
  SystemState _state;
  bool _relayPure;       // 净化水继电器
  bool _relayHot;        // 热水继电器
  bool _heaterOn;        // 加热器状态

  // 水量计量
  volatile unsigned long _pulseCount;  // 脉冲计数 (volatile: ISR会修改)
  int _targetMl;                       // 目标出水量
  int _dispensedMl;                    // 已出水毫升数 (上一次计算值)
  unsigned long _dispenseStartMs;      // 出水开始时间

  // 温度
  float _currentTemp;                  // 当前水温

  // 超时控制
  unsigned long _lastFlowPulseMs;      // 最后一次流量脉冲时间 (断流检测)

  // 内部方法
  void _resetCounters();
  void _openValve(int relayPin);
  void _closeAllValves();
  int _pulsesToMl(unsigned long pulses);
};

// ============================
// 实现
// ============================

void WaterControl::begin() {
  // 初始化输出引脚
  pinMode(RELAY_PURE_WATER, OUTPUT);
  pinMode(RELAY_HOT_WATER,  OUTPUT);
  pinMode(RELAY_HEATER,     OUTPUT);

  // 初始化 LED
  pinMode(LED_READY,         OUTPUT);
  pinMode(LED_PURE_DISPENSE, OUTPUT);
  pinMode(LED_HOT_DISPENSE,  OUTPUT);
  pinMode(LED_ERROR,         OUTPUT);

  // 初始化蜂鸣器
  pinMode(BUZZER_PIN, OUTPUT);

  // 初始化流量计中断引脚
  pinMode(FLOW_METER_PIN, INPUT_PULLUP);

  // 全部关闭
  _closeAllValves();
  setHeater(false);

  // 初始状态
  _state          = STATE_IDLE;
  _pulseCount     = 0;
  _targetMl       = 0;
  _dispensedMl    = 0;
  _dispenseStartMs = 0;
  _currentTemp    = 25.0;  // 室温默认值
  _lastFlowPulseMs = 0;

  // 就绪指示灯
  digitalWrite(LED_READY, HIGH);
}

bool WaterControl::startPureWater(int targetMl) {
  if (_state != STATE_IDLE) return false;

  if (targetMl <= 0)
    _targetMl = DEFAULT_PURE_WATER_ML;
  else if (targetMl > MAX_WATER_ML)
    _targetMl = MAX_WATER_ML;
  else
    _targetMl = targetMl;

  _resetCounters();
  _openValve(RELAY_PURE_WATER);

  _state = STATE_DISPENSING;
  _dispenseStartMs = millis();
  _lastFlowPulseMs = millis();

  digitalWrite(LED_PURE_DISPENSE, HIGH);
  digitalWrite(LED_READY, LOW);

  return true;
}

bool WaterControl::startHotWater(int targetMl) {
  if (_state != STATE_IDLE) return false;

  // 检查热水是否就绪
  if (!isHotWaterReady()) return false;

  if (targetMl <= 0)
    _targetMl = DEFAULT_HOT_WATER_ML;
  else if (targetMl > MAX_WATER_ML)
    _targetMl = MAX_WATER_ML;
  else
    _targetMl = targetMl;

  _resetCounters();
  _openValve(RELAY_HOT_WATER);

  _state = STATE_DISPENSING;
  _dispenseStartMs = millis();
  _lastFlowPulseMs = millis();

  digitalWrite(LED_HOT_DISPENSE, HIGH);
  digitalWrite(LED_READY, LOW);

  return true;
}

void WaterControl::stopDispensing() {
  _closeAllValves();
  _state = STATE_IDLE;
  _dispensedMl = _pulsesToMl(_pulseCount);

  digitalWrite(LED_PURE_DISPENSE, LOW);
  digitalWrite(LED_HOT_DISPENSE, LOW);
  digitalWrite(LED_READY, HIGH);
}

float WaterControl::getTemperature() {
  // 实际部署: 用 DallasTemperature 库读取 DS18B20
  // sensors.requestTemperatures();
  // _currentTemp = sensors.getTempCByIndex(0);
  // 此处返回缓存值（模拟）
  return _currentTemp;
}

int WaterControl::getDispensedMl() {
  _dispensedMl = _pulsesToMl(_pulseCount);
  return _dispensedMl;
}

int WaterControl::getTargetMl() {
  return _targetMl;
}

SystemState WaterControl::getState() {
  return _state;
}

void WaterControl::update() {
  unsigned long now = millis();

  switch (_state) {

    case STATE_DISPENSING: {
      // 将脉冲数转为毫升
      _dispensedMl = _pulsesToMl(_pulseCount);

      // 达到目标水量 -> 停止
      if (_dispensedMl >= _targetMl) {
        stopDispensing();
        return;
      }

      // 出水超时保护 (例如: 水桶空了)
      if (now - _dispenseStartMs > DISPENSE_TIMEOUT) {
        stopDispensing();

        // 超时故障
        _state = STATE_ERROR;
        digitalWrite(LED_ERROR, HIGH);

        // 蜂鸣器报警
        digitalWrite(BUZZER_PIN, HIGH);
        delay(BUZZER_LONG_BEEP);
        digitalWrite(BUZZER_PIN, LOW);
        return;
      }

      // 断流检测 (2秒内无脉冲 -> 可能水桶空或堵)
      if (_lastFlowPulseMs > 0 &&
          (now - _lastFlowPulseMs) > 2000 &&
          _pulseCount == 0) {
        stopDispensing();
        _state = STATE_ERROR;
        digitalWrite(LED_ERROR, HIGH);
      }
      break;
    }

    case STATE_HEATING: {
      // 温度控制 (带回差)
      float temp = getTemperature();

      if (_heaterOn) {
        // 达到目标温度 -> 停止加热
        if (temp >= HOT_WATER_TARGET_TEMP) {
          setHeater(false);
        }
        // 超时保护
        else if (now - _dispenseStartMs > MAX_HEAT_TIME_SEC * 1000UL) {
          setHeater(false);
          _state = STATE_ERROR;
          digitalWrite(LED_ERROR, HIGH);
        }
      } else {
        // 温度低于 (目标 - 回差) -> 重新加热
        if (temp < HOT_WATER_TARGET_TEMP - HEATER_HYSTERESIS) {
          setHeater(true);
          _dispenseStartMs = now;  // 重置加热计时
        }
      }

      // 热水就绪则回到待机
      if (isHotWaterReady()) {
        _state = STATE_IDLE;
        digitalWrite(LED_READY, HIGH);
      }
      break;
    }

    case STATE_ERROR: {
      // 错误状态: 闪烁错误LED
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

bool WaterControl::isHotWaterReady() {
  return (getTemperature() >= HOT_WATER_MIN_TEMP);
}

void WaterControl::flowPulseISR() {
  _pulseCount++;
  _lastFlowPulseMs = millis();
}

void WaterControl::setHeater(bool on) {
  _heaterOn = on;
  digitalWrite(RELAY_HEATER, on ? HIGH : LOW);

  if (on) {
    _state = STATE_HEATING;
    digitalWrite(LED_READY, LOW);
  }
}

// ---- 内部辅助方法 ----

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
  // YF-S201: 450脉冲 = 1升 = 1000毫升
  return (int)(((unsigned long)pulses * 1000) / PULSES_PER_LITER);
}

#endif // WATER_CONTROL_H
