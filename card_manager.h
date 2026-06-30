/**
 * 净水机刷卡控制系统 - 刷卡管理模块
 *
 * 负责: RFID卡读取、卡号匹配、权限判断
 * 依赖: MFRC522 库
 */

#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "config.h"

// ============================
// 卡类型枚举
// ============================

enum CardType {
  CARD_NONE         = -1,  // 未识别
  CARD_ADMIN        = 0,   // 管理员卡
  CARD_USER_PURE    = 1,   // 净化水用户卡
  CARD_USER_HOT     = 2,   // 热水用户卡
  CARD_USER_BOTH    = 3,   // 通用用户卡
};

// ============================
// CardManager 类
// ============================

class CardManager {
public:
  /**
   * 初始化RFID模块
   * 返回: true=成功, false=失败
   */
  bool begin();

  /**
   * 检测是否有新卡靠近
   * 返回: 卡的UID字符串 (如 "AB:CD:EF:01"), 无卡返回空字符串
   */
  String scanCard();

  /**
   * 识别卡类型
   * uid: 4字节卡UID
   * 返回: CardType 枚举值
   */
  CardType identifyCard(const byte uid[4]);

  /**
   * RFID模块自检
   * 返回: true=正常, false=故障
   */
  bool selfTest();

private:
  // 比较两个 4字节 UID
  bool uidMatch(const byte uid1[4], const byte uid2[4]);
};

// ============================
// 实现 (单文件包含模式)
// ============================

bool CardManager::begin() {
  // 实际部署时取消注释:
  // SPI.begin();
  // mfrc522.PCD_Init(RFID_SS_PIN, RFID_RST_PIN);
  // mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_max);

  // 自检
  // byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  // if (version == 0x00 || version == 0xFF) return false;

  // 模拟初始化成功
  return true;
}

String CardManager::scanCard() {
  // 实际部署时用 MFRC522 库:
  // if (!mfrc522.PICC_IsNewCardPresent()) return "";
  // if (!mfrc522.PICC_ReadCardSerial()) return "";

  // 读取4字节UID
  // byte uid[4];
  // for (byte i = 0; i < 4; i++) uid[i] = mfrc522.uid.uidByte[i];
  // mfrc522.PICC_HaltA();

  // 格式化为字符串
  // char buf[13];
  // snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X",
  //          uid[0], uid[1], uid[2], uid[3]);

  // 无卡: 返回空字符串
  return "";
}

CardType CardManager::identifyCard(const byte uid[4]) {
  // 检查是否是管理员卡
  if (uidMatch(uid, ADMIN_CARD)) {
    return CARD_ADMIN;
  }

  // 检查是否是注册用户卡
  for (int i = 0; i < USER_CARD_COUNT; i++) {
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

bool CardManager::selfTest() {
  // 实际部署时:
  // byte v = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  // return (v != 0x00 && v != 0xFF);
  return true;
}

bool CardManager::uidMatch(const byte uid1[4], const byte uid2[4]) {
  for (int i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

#endif // CARD_MANAGER_H
