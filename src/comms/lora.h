#pragma once

#include <RadioLib.h>
#include <map>
#include <vector>
#include <Arduino.h>

#define TYPE_TEXT_FRAGMENT 0x01
#define TYPE_ACK_FRAGMENT  0x02
#define TYPE_ACK_CONFIRM   0x03
#define PRIORITY_NORMAL    0x00

#define LORA_BANDWIDTH     500.0
#define LORA_SPREADING_FACTOR 12
#define LORA_CODING_RATE   8
#define LORA_PREAMBLE_LENGTH 20
#define LORA_SYNC_WORD     0xF3
#define LORA_CRC           true

#define BROADCAST_MEMORY_TIME 30000UL
#define MAX_FRAGMENT_SIZE 200
#define FRAG_HEADER_SIZE 5
#define FRAG_DATA_LEN (MAX_FRAGMENT_SIZE - FRAG_HEADER_SIZE)

struct Fragment {
  uint8_t data[MAX_FRAGMENT_SIZE];
  uint8_t length;  // actual payload length
  bool acked;
  int retries;
  unsigned long timestamp;
};

struct IncomingText {
  uint8_t total;
  std::vector<bool> received;
  std::map<uint8_t, String> parts;
};

extern SX1262 lora;
extern std::map<String, std::vector<Fragment>> outgoing;
extern std::map<String, IncomingText> incoming;

void initLoRa(float freq, int txPower);
String generateMsgID();
bool isRecentMessage(const String &msgId);
void encryptFragment(uint8_t *b, size_t len);
void decryptFragment(uint8_t *b, size_t len);
void sendMessages();
void handleIncoming(uint8_t *buf, size_t len);
void sendBeacon();
