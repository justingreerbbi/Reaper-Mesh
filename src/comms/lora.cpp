#include "lora.h"

#include <AES.h>
#include <Crypto.h>
#include "../config.h"
#include "../gps/gps.h"

AES128 aes;
uint8_t aes_key[16] = {
  0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
  0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81
};

SX1262 lora = new Module(8, 14, 12, 13);
std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

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

String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

bool isRecentMessage(const String &msgId) {
  unsigned long now = millis();
  for (auto it = recentMsgs.begin(); it != recentMsgs.end();) {
    if (now - it->second > BROADCAST_MEMORY_TIME)
      it = recentMsgs.erase(it);
    else
      ++it;
  }
  if (recentMsgs.count(msgId)) return true;
  recentMsgs[msgId] = now;
  return false;
}

void encryptFragment(uint8_t *b, size_t len) {
  for (size_t i = 0; i < len; i += 16) aes.encryptBlock(b + i, b + i);
}

void decryptFragment(uint8_t *b, size_t len) {
  for (size_t i = 0; i < len; i += 16) aes.decryptBlock(b + i, b + i);
}

void processAck(uint8_t *buf) {
  decryptFragment(buf, MAX_FRAGMENT_SIZE);
  if (buf[0] != TYPE_ACK_FRAGMENT) return;
  String msgId = String((buf[1] << 8) | buf[2], HEX);
  uint8_t seq = buf[3];
  auto it = outgoing.find(msgId);
  if (it != outgoing.end() && seq < it->second.size()) {
    it->second[seq].acked = true;
    Serial.printf("ACK|RECV|%s|SEQ=%d\n", msgId.c_str(), seq);
  }
}

void sendMessages() {
  for (auto it = outgoing.begin(); it != outgoing.end();) {
    bool allAcked = true;
    for (auto &frag : it->second) {
      if (frag.acked || frag.retries >= 2) continue;

      unsigned long start = millis();
      while (millis() - start < 500) {
        uint8_t buf[MAX_FRAGMENT_SIZE];
        int state = lora.receive(buf, sizeof(buf));
        if (state == RADIOLIB_ERR_NONE) {
          decryptFragment(buf, MAX_FRAGMENT_SIZE);
          processAck(buf);
        }
        if (frag.acked) break;
      }

      if (!frag.acked) {
        int state = lora.transmit(frag.data, frag.length);
        if (state == RADIOLIB_ERR_NONE) {
          frag.timestamp = millis();
          frag.retries++;
          Serial.printf("SEND|ATTEMPT|%s|%d/%d|try=%d\n", it->first.c_str(),
                        (&frag - &it->second[0]) + 1, (int)it->second.size(),
                        frag.retries);
        }
        lora.startReceive();
        allAcked = false;
      }
    }
    if (allAcked)
      it = outgoing.erase(it);
    else
      ++it;
  }
}

void handleIncoming(uint8_t *buf, size_t len) {
  decryptFragment(buf, len);
  uint8_t type = buf[0] & 0x0F;

  if (type == TYPE_TEXT_FRAGMENT) {
    String msgId = String((buf[1] << 8) | buf[2], HEX);
    msgId.toUpperCase();
    uint8_t seq = buf[3];
    uint8_t total = buf[4];
    String part;
    for (int i = 5; i < len; i++) {
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

    bool complete = true;
    for (bool got : msg.received) {
      if (!got) {
        complete = false;
        break;
      }
    }

    if (complete) {
      if (isRecentMessage(msgId)) {
        incoming.erase(msgId);
        return;
      }

      uint8_t ackConfirm[MAX_FRAGMENT_SIZE] = {0};
      ackConfirm[0] = TYPE_ACK_CONFIRM;
      ackConfirm[1] = buf[1];
      ackConfirm[2] = buf[2];
      ackConfirm[3] = buf[3];
      encryptFragment(ackConfirm, MAX_FRAGMENT_SIZE);
      lora.transmit(ackConfirm, MAX_FRAGMENT_SIZE);
      delay(50);
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
        String message = parts[2];
        Serial.printf("RECV|MSG|%s|%s|%s\n", sender.c_str(), message.c_str(), msgId.c_str());
      } else if (msgType == "DMSG") {
        String recipient = parts[2];
        String message = parts[3];
        Serial.printf("RECV|DMSG|%s|%s|%s|%s\n", sender.c_str(), recipient.c_str(), message.c_str(), msgId.c_str());
      } else if (msgType == "BEACON") {
        Serial.print("RECV|");
        Serial.println(fullMessage);
      } else {
        Serial.printf("RECV|UNKNOWN|%s\n", fullMessage.c_str());
      }
    }
  } else if (type == TYPE_ACK_CONFIRM) {
    char bufId[5];
    snprintf(bufId, sizeof(bufId), "%02X%02X", buf[1], buf[2]);
    String mid(bufId);
    mid.toUpperCase();
    for (auto &frag : outgoing[mid]) frag.acked = true;
    Serial.printf("ACK|CONFIRM|%s\n", mid.c_str());
  } else if (type == TYPE_ACK_FRAGMENT) {
    processAck(buf);
  }
}

void sendBeacon() {
  String msg;
  ReaperGPSData data = getGPSData();
  msg = String(data.latitude, 6) + "," +
        String(data.longitude, 6) + "," +
        String(data.altitude, 2) + "," +
        String(data.speed, 2) + "," +
        String(data.course) + "," +
        String(data.satellites);
  msg = "BEACON|" + String(settings.deviceName) + "|" + msg;

  String msgId = generateMsgID();
  std::vector<Fragment> frags;
  int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  for (int i = 0; i < total; i++) {
    Fragment frag = {};
    frag.data[0] = TYPE_TEXT_FRAGMENT;
    frag.data[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
    frag.data[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
    frag.data[3] = i;
    frag.data[4] = total;

    String chunk = msg.substring(i * FRAG_DATA_LEN, min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
    memcpy(&frag.data[5], chunk.c_str(), chunk.length());

    frag.length = chunk.length() + 5;
    encryptFragment(frag.data, frag.length);
    frag.retries = 0;
    frag.timestamp = millis();
    frag.acked = false;
    lora.transmit(frag.data, frag.length);
    frags.push_back(frag);
  }

  outgoing[msgId] = frags;
}
