#pragma once
#include <Arduino.h>
#include <RadioLib.h>

#include <map>
#include <vector>

#define AES_BLOCK_LEN 16
#define FRAG_DATA_LEN 11
#define TYPE_TEXT_FRAGMENT 0x03
#define TYPE_ACK_FRAGMENT 0x04
#define TYPE_ACK_CONFIRM 0x08
#define PRIORITY_NORMAL 0x03
#define BROADCAST_MEMORY_TIME 30000UL

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
void processAck(uint8_t *buf);
String generateMsgID();
void encryptFragment(uint8_t *b);
void decryptFragment(uint8_t *b);
bool isRecentMessage(const String &msgId);
void sendBeacon();
