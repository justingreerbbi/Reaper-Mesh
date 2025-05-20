#include "lora.h"

#include <AES.h>
#include <Crypto.h>

#include "../config.h"

AES128 aes;
uint8_t aes_key[16] = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
                       0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81};

SX1262 lora = new Module(8, 14, 12, 13);
std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

// Initialize the LoRa module with the given frequency and transmission power.
void initLoRa(float freq, int txPower) {
  aes.setKey(aes_key, sizeof(aes_key));
  int state = lora.begin(freq);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("ERR|LORA_INIT_FAILED|%d\n", state);
    while (1);
  }

  // Set LoRa parameters. For now, we use a set of hard coded parameters to keep
  // the system simple. These can be changed later to allow for more flexibility
  // but for now, we will keep it simple and use a set of hard coded parameters.
  lora.setBandwidth(LORA_BANDWIDTH);
  lora.setSpreadingFactor(LORA_SPREADING_FACTOR);
  lora.setCodingRate(LORA_CODING_RATE);
  lora.setPreambleLength(LORA_PREAMBLE_LENGTH);
  lora.setSyncWord(LORA_SYNC_WORD);
  lora.setOutputPower(txPower);
  lora.setCRC(LORA_CRC);
  lora.startReceive();
}

// Generate a random message ID
// The ID is a 4-digit hexadecimal number, generated using esp_random().
String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

// Check if the message ID is recent.
// If it is, return true. Otherwise, add it to the recent messages map and
// return false.
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

void encryptFragment(uint8_t *b) { aes.encryptBlock(b, b); }
void decryptFragment(uint8_t *b) { aes.decryptBlock(b, b); }

// Process the ACK fragment.
// This function is called when an ACK fragment is received.
void processAck(uint8_t *buf) {
  decryptFragment(buf);
  if (buf[0] != TYPE_ACK_FRAGMENT) return;
  String msgId = String((buf[1] << 8) | buf[2], HEX);
  uint8_t seq = buf[3];
  auto it = outgoing.find(msgId);
  if (it != outgoing.end() && seq < it->second.size()) {
    it->second[seq].acked = true;
    Serial.printf("ACK|RECV|%s|SEQ=%d\n", msgId.c_str(), seq);
  }
}

void retryFragments() {
  for (auto it = outgoing.begin(); it != outgoing.end();) {
    bool allAcked = true;
    for (auto &frag : it->second) {
      if (frag.acked || frag.retries >= 2) continue;
      unsigned long start = millis();
      while (millis() - start < 1000) {
        uint8_t buf[128];
        int state = lora.receive(buf, sizeof(buf));
        if (state == RADIOLIB_ERR_NONE) {
          decryptFragment(buf);
          processAck(buf);
        }
        if (frag.acked) break;
      }
      if (!frag.acked) {
        int state = lora.transmit(frag.data, AES_BLOCK_LEN);
        if (state == RADIOLIB_ERR_NONE) {
          frag.timestamp = millis();
          frag.retries++;
          Serial.printf("SEND|ATTEMPT|%s|%d|try=%d\n", it->first.c_str(),
                        (&frag - &it->second[0]), frag.retries);
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

      // Send ack_confirm back to the sender
      uint8_t ackConfirm[AES_BLOCK_LEN] = {0};
      ackConfirm[0] = TYPE_ACK_CONFIRM;
      ackConfirm[1] = buf[1];
      ackConfirm[2] = buf[2];
      ackConfirm[3] = buf[3];
      encryptFragment(ackConfirm);
      lora.transmit(ackConfirm, AES_BLOCK_LEN);
      delay(50);
      lora.startReceive();

      String fullMessage;
      for (int i = 0; i < total; i++) fullMessage += msg.parts[i];
      //Serial.println(fullMessage);

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
      }
    }
  } else if (type == TYPE_ACK_CONFIRM) {
    char bufId[5];
    snprintf(bufId, sizeof(bufId), "%02X%02X", buf[1], buf[2]);
    String mid(bufId);
    mid.toUpperCase();
    for (auto &frag : outgoing[mid]) frag.acked = true;
    Serial.printf("ACK|CONFIRM|%s", mid.c_str());
  } else if (type == TYPE_ACK_FRAGMENT) {
    processAck(buf);
  }
}
