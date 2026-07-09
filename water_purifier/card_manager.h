/**
 * 净水机刷卡控制系统 - 刷卡管理模块
 *
 * 负责: RFID卡读取、卡号匹配、权限判断
 * 依赖: MFRC522 库 (by GithubCommunity)
 *
 * 硬件: RFID-RC522 模块, SPI 接口
 *   SDA  -> D10 (SS)
 *   SCK  -> D13
 *   MOSI -> D11
 *   MISO -> D12
 *   RST  -> D9
 */

#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "config.h"
// 注意: <SPI.h> 和 <MFRC522.h> 在净水机.ino 顶部引入
// 确保 .ino 中 #include 顺序为: 库 → config.h → card_manager.h → water_control.h

// ============================
// 卡类型枚举
// ============================

enum CardType {
  CARD_NONE      = -1,  // 未识别 / 无卡
  CARD_ADMIN     = 0,   // 管理员卡
  CARD_USER_PURE = 1,   // 净化水用户卡
  CARD_USER_HOT  = 2,   // 热水用户卡
  CARD_USER_BOTH = 3,   // 通用用户卡 (净化水+热水)
};

// 全局 MFRC522 对象 (在 .ino 中定义)
extern MFRC522 mfrc522;

// ============================
// CardManager 类
// ============================

class CardManager {
public:
  /**
   * 初始化 RFID 模块
   * 返回: true=成功, false=硬件故障
   */
  bool begin();

  /**
   * 检测是否有新卡靠近，有则读取UID
   * 返回: 格式化的 UID 字符串 (如 "AB:CD:EF:01"), 无卡返回 ""
   */
  String scanCard();

  /**
   * 识别当前扫描到的卡类型 (使用内部存储的UID)
   * 返回: CardType 枚举值
   */
  CardType identifyCard();

  /**
   * 识别指定 UID 的卡类型
   * uid: 4字节卡UID
   * 返回: CardType 枚举值
   */
  CardType identifyCard(const byte uid[4]);

  /**
   * 获取最后一次扫描到的 UID (4字节)
   */
  const byte* getLastUid() const { return _lastUid; }

  /**
   * RFID 模块自检
   * 返回: true=正常, false=故障
   */
  bool selfTest();

private:
  byte _lastUid[4];       // 最后一次扫描到的卡UID
  bool _cardPresent;      // 是否有卡

  // 比较两个 4字节 UID
  bool uidMatch(const byte uid1[4], const byte uid2[4]);
};

// ============================
// 实现
// ============================

bool CardManager::begin() {
  // 初始化 SPI 总线
  SPI.begin();

  // 初始化 MFRC522
  mfrc522.PCD_Init(RFID_SS_PIN, RFID_RST_PIN);

  // 设置天线增益 (最大灵敏度)
  mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_max);

  // 自检
  if (!selfTest()) {
    return false;
  }

  // 清空内部状态
  memset(_lastUid, 0, 4);
  _cardPresent = false;

  return true;
}

String CardManager::scanCard() {
  _cardPresent = false;

  // 检测新卡
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return "";
  }

  // 读取卡序列号
  if (!mfrc522.PICC_ReadCardSerial()) {
    return "";
  }

  // 存储 UID (MFRC522 的 UID 最多 10 字节, 我们取前 4 字节)
  for (byte i = 0; i < 4; i++) {
    _lastUid[i] = mfrc522.uid.uidByte[i];
  }

  // 停止卡通信 (进入休眠)
  mfrc522.PICC_HaltA();

  // 停止 PCD 加密 (为下一次读卡准备)
  mfrc522.PCD_StopCrypto1();

  _cardPresent = true;

  // 格式化为可读字符串
  char buf[13];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X",
           _lastUid[0], _lastUid[1], _lastUid[2], _lastUid[3]);
  return String(buf);
}

CardType CardManager::identifyCard() {
  if (!_cardPresent) return CARD_NONE;
  return identifyCard(_lastUid);
}

CardType CardManager::identifyCard(const byte uid[4]) {
  // 检查管理员卡
  if (uidMatch(uid, ADMIN_CARD)) {
    return CARD_ADMIN;
  }

  // 检查注册用户卡
  for (int i = 0; i < USER_CARD_COUNT; i++) {
    if (uidMatch(uid, USER_CARDS[i])) {
      int userType = USER_CARD_TYPES[i];
      switch (userType) {
        case 0: return CARD_USER_PURE;
        case 1: return CARD_USER_HOT;
        case 2: return CARD_USER_BOTH;
        default: return CARD_NONE;
      }
    }
  }

  return CARD_NONE;
}

bool CardManager::selfTest() {
  // 读取 MFRC522 版本寄存器
  // 有效值: 0x92 (v2.0) 或 0x91 (v1.0)
  byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);

  // 0x00 或 0xFF 表示通信故障
  if (version == 0x00 || version == 0xFF) {
    return false;
  }

  return true;
}

// ---- 内部辅助 ----

bool CardManager::uidMatch(const byte uid1[4], const byte uid2[4]) {
  for (int i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

#endif // CARD_MANAGER_H
