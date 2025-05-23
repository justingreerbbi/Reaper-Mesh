#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include <map>
#include <vector>
#include "lora_defs.h"        // NEW – contains all LoRa constants

struct Fragment {
  uint8_t data[MAX_FRAGMENT_SIZE];
  uint16_t length;            // AES-padded length actually transmitted
  uint8_t  retries;
  uint32_t timestamp;
  bool     acked = false;
};

struct IncomingText {
  uint8_t              total = 0;
  std::map<uint8_t,String> parts;
  std::vector<bool>    received;
};

extern SX1262 lora;
extern std::map<String,std::vector<Fragment>> outgoing;
extern std::map<String,IncomingText>          incoming;

void   initLoRa(float freq,int txPower);
void   queueMessage(const String& type,const String& payload);
void   sendMessages();
void   handleIncoming(uint8_t* buf,size_t len);
void   processAck(uint8_t* buf,size_t len);
bool   isRecentMessage(const String& id);
String generateMsgID();
void   sendBeacon();
