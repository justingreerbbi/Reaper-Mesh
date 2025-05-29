#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include "../config.h"

#include <map>
#include <vector>

struct Fragment {
  uint8_t data[AES_BLOCK_LEN];
  int retries;
  unsigned long timestamp;
  bool acked = false;
};

struct IncomingText {
  int total;
  unsigned long start;
  std::map<uint8_t, String> parts;
  std::vector<bool> received;
};

extern SX1262 lora;
extern std::map<String, std::vector<Fragment>> outgoing;
extern std::map<String, IncomingText> incoming;

void initLoRa(float freq, int txPower);
void handleIncoming(uint8_t *buf);
void sendMessages();
String generateMsgID();
void encryptFragment(uint8_t *b);
void decryptFragment(uint8_t *b);
bool isRecentMessage(const String &msgId);
void sendBeacon();
void taskLoRaHandler(void* param);
void processMessageToOutgoing(String msg);
void sendAckConfirmMessage(const String &msgId);
