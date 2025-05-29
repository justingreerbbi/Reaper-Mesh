#include "lora.h"

#include <AES.h>
#include <Crypto.h>
#include <RadioLib.h>
#include <map>
#include <vector>
#include <set>
#include <Arduino.h>

#include "../gps/gps.h"
#include "../system/settings.h"
#include "../config.h"

AES128 aes;
uint8_t aes_key[16] = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
                       0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81};

SX1262 lora = new Module(8, 14, 12, 13);
std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;
std::set<String> confirmedMsgs;

bool isTransmitting = false;
int retryAttemptLimit = 3;

void encryptFragment(uint8_t *b) { aes.encryptBlock(b, b); }
void decryptFragment(uint8_t *b) { aes.decryptBlock(b, b); }

String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

void initLoRa(float freq, int txPower) {
  aes.setKey(aes_key, sizeof(aes_key));
  int state = lora.begin(freq);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("ERR|LORA_INIT_FAILED|%d\n", state);
    while (1);
  }

  lora.setBandwidth(LORA_BANDWIDTH);
  lora.setSpreadingFactor(LORA_SPREADING_FACTOR);
  lora.setCodingRate(LORA_CODING_RATE);
  lora.setPreambleLength(LORA_PREAMBLE_LENGTH);
  lora.setSyncWord(LORA_SYNC_WORD);
  lora.setOutputPower(txPower);
  lora.setCRC(LORA_CRC);
  lora.startReceive();
}

bool isRecentMessage(const String &msgId) {
  unsigned long now = millis();
  for (auto it = recentMsgs.begin(); it != recentMsgs.end();) {
    if (now - it->second > BROADCAST_MEMORY_TIME) // Remove old messages
      it = recentMsgs.erase(it);
    else
      ++it;
  }
  if (recentMsgs.count(msgId)) return true;
  recentMsgs[msgId] = now;
  return false;
}

void sendAckConfirmMessage(const String &msgId) {
  Serial.printf("SEND|ACK_CONFIRM|%s\n", msgId.c_str());
  uint8_t ackConfirm[AES_BLOCK_LEN] = {0};
  ackConfirm[0] = TYPE_ACK_CONFIRM;
  ackConfirm[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
  ackConfirm[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
  encryptFragment(ackConfirm);
  lora.transmit(ackConfirm, AES_BLOCK_LEN);
}

void handleIncoming(uint8_t *buf) {
  decryptFragment(buf);
  uint8_t type = buf[0] & 0x0F;

  if (type == TYPE_TEXT_FRAGMENT) {
    String msgId = String((buf[1] << 8) | buf[2], HEX);
    msgId.toUpperCase();
    uint8_t seq = buf[3];
    uint8_t total = buf[4];

    String part;
    for (int i = 5; i < AES_BLOCK_LEN; i++) {
      if (buf[i] == 0x00) break;
      part += (char)buf[i];
    }

    IncomingText &msg = incoming[msgId];
    if (msg.received.size() != total) {
      msg.total = total;
      msg.received.assign(total, false);
    }
    msg.parts[seq] = part;
    msg.received[seq] = true;

    Serial.printf("RECV|FRAG|%s|%d/%d\n", msgId.c_str(), seq + 1, total);

    // Check if all fragments have been received
    bool complete = true;
    for (size_t i = 0; i < msg.received.size(); ++i) {
      if (!msg.received[i]) {
        complete = false;
        break;
      }
    }

    if (complete) {
      if (isRecentMessage(msgId)) {
        //Serial.printf("RECV|DUPLICATE|%s\n", msgId.c_str());
        sendAckConfirmMessage(msgId);
        return;
      }
      lora.startReceive();

      String fullMessage;
      for (int i = 0; i < total; i++) fullMessage += msg.parts[i];

      std::vector<String> parts;
      int last = 0, next = 0;
      while ((next = fullMessage.indexOf('|', last)) != -1) {
        parts.push_back(fullMessage.substring(last, next));
        last = next + 1;
      }
      parts.push_back(fullMessage.substring(last));

      String msgType = parts[0];
      String sender = parts[1];

      if (msgType == "MSG") {
        Serial.printf("RECV|MSG|%s|%s|%s\n", sender.c_str(), parts[2].c_str(), msgId.c_str());
      } else if (msgType == "DMSG") {
        Serial.printf("RECV|DMSG|%s|%s|%s|%s\n", sender.c_str(), parts[2].c_str(), parts[3].c_str(), msgId.c_str());
      } else if (msgType == "BEACON") {
        Serial.print("RECV|");
        Serial.println(fullMessage);
      } else {
        Serial.printf("RECV|UNKNOWN|%s\n", fullMessage.c_str());
      }
      sendAckConfirmMessage(msgId);
    }
  } else if (type == TYPE_ACK_CONFIRM) {
    char bufId[5];
    snprintf(bufId, sizeof(bufId), "%02X%02X", buf[1], buf[2]);
    String mId(bufId);
    mId.toUpperCase();
    confirmedMsgs.insert(mId);
    Serial.printf("ACK|CONFIRM|%s\n", mId.c_str());
  }
}

void sendMessages() {
  if (isTransmitting) return;
  isTransmitting = true;

  for (auto it = outgoing.begin(); it != outgoing.end();) {
    const String& msgId = it->first;

    // Skip if already confirmed
    if (confirmedMsgs.find(msgId) != confirmedMsgs.end()) {
      it = outgoing.erase(it);
      continue;
    }

    std::vector<Fragment>& fragments = it->second;
    bool didSend = false;

    // Send all fragments once before listening for ACK_CONFIRM
    for (size_t i = 0; i < fragments.size(); ++i) {
      Fragment& frag = fragments[i];

      // If max retries hit, skip sending
      if (frag.retries >= retryAttemptLimit) continue;

      int state = lora.transmit(frag.data, AES_BLOCK_LEN);
      if (state == RADIOLIB_ERR_NONE) {
        frag.timestamp = millis();
        frag.retries++;
        Serial.printf("SEND|FRAG|%s|%d/%d|try=%d\n",
          msgId.c_str(), static_cast<int>(i) + 1, static_cast<int>(fragments.size()), frag.retries);
        didSend = true;
      }
    }

    // After first full send, switch to receive mode
    lora.startReceive();

    // Wait for possible ACK_CONFIRM
    uint8_t buf[200];
    int state = lora.receive(buf, sizeof(buf));
    if (state == RADIOLIB_ERR_NONE) {
      handleIncoming(buf);
    }

    // Clean up if ACK received
    if (confirmedMsgs.find(msgId) != confirmedMsgs.end()) {
      it = outgoing.erase(it);
    } else {
      ++it;
    }
  }

  isTransmitting = false;
  lora.startReceive();  // Ensure we stay in RX mode
}

void sendBeacon() {
  String msg;
  ReaperGPSData data = getGPSData();
  msg = String(data.latitude, 6) + "," + String(data.longitude, 6) + "," +
        String(data.altitude) + "," + String(data.speed) + "," +
        String(data.course) + "," + String(data.satellites);
  msg = "BEACON|" + String(settings.deviceName) + "|" + msg;
  processMessageToOutgoing(msg);
}

void processMessageToOutgoing(String msg) {
  String msgId = generateMsgID();
  Serial.printf("SENDING|MSGID|%s\n", msgId.c_str());
  std::vector<Fragment> frags;
  int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  for (int i = 0; i < total; i++) {
    uint8_t block[AES_BLOCK_LEN] = {0};
    block[0] = PRIORITY_NORMAL;
    block[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
    block[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
    block[3] = i;
    block[4] = total;

    String chunk = msg.substring(i * FRAG_DATA_LEN,
                                 min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));

    memcpy(&block[5], chunk.c_str(), chunk.length());
    encryptFragment(block);

    Fragment frag;
    memcpy(frag.data, block, AES_BLOCK_LEN);
    frag.retries = 0;
    frag.timestamp = millis();
    frags.push_back(frag);
  }

  outgoing[msgId] = frags;
}

void taskLoRaHandler(void* param) {
  while (true) {
    uint8_t buf[200];
    int state = lora.receive(buf, sizeof(buf));
    if (state == RADIOLIB_ERR_NONE) {
      handleIncoming(buf);
    }

    sendMessages();
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}
