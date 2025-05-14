#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include <vector>
#include <map>

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2 pins

#define FREQUENCY        915.0
#define BANDWIDTH        500.0
#define SPREADING_FACTOR 12
#define CODING_RATE      8
#define PREAMBLE_LENGTH  20
#define SYNC_WORD        0xF3
#define TX_POWER         22

#define MAX_RETRIES      0
#define RETRY_INTERVAL   6000
#define FRAG_DATA_LEN    11
#define AES_BLOCK_LEN    16

#define TYPE_TEXT_FRAGMENT  0x03
#define TYPE_ACK_FRAGMENT   0x04
#define TYPE_REFRAGMENT_REQ 0x05
#define TYPE_VERIFY_REQUEST 0x06
#define TYPE_VERIFY_REPLY   0x07
#define TYPE_ACK_CONFIRM    0x08

#define PRIORITY_NORMAL 0x03
#define PRIORITY_HIGH   0x13

#define BROADCAST_MEMORY_TIME 30000
#define REQ_TIMEOUT 2000

char deviceName[16];
AES128 aes;
uint8_t aes_key[16] = {
  0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
  0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81
};

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

std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

void processFragment(uint8_t* buf);
void processAck(uint8_t* buf);
void processConfirm(uint8_t* buf);

String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

bool isRecentMessage(String msgId) {
  unsigned long now = millis();
  for (auto it = recentMsgs.begin(); it != recentMsgs.end(); ) {
    if (now - it->second > BROADCAST_MEMORY_TIME) {
      it = recentMsgs.erase(it);
    } else {
      ++it;
    }
  }
  if (recentMsgs.find(msgId) != recentMsgs.end()) return true;
  recentMsgs[msgId] = now;
  return false;
}

void encryptFragment(uint8_t* input) {
  aes.encryptBlock(input, input);
}

void decryptFragment(uint8_t* input) {
  aes.decryptBlock(input, input);
}

void sendVerifyRequest(const String& msgId) {
  uint8_t verify[AES_BLOCK_LEN] = {0};
  verify[0] = TYPE_VERIFY_REQUEST;
  verify[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
  verify[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
  encryptFragment(verify);
  lora.transmit(verify, AES_BLOCK_LEN);
  Serial.printf("VERIFY|SEND|%s\n", msgId.c_str());
  delay(50);
  lora.startReceive();
}

void sendEncryptedText(String msg) {
  bool highPriority = false;
  if (msg.startsWith("!")) {
    highPriority = true;
    msg.remove(0, 1);
  }

  msg = String(deviceName) + "|" + msg;
  std::vector<Fragment> frags;
  String msgId = generateMsgID();
  int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  delay(random(100, 400));  // initial jitter

  for (int i = 0; i < total; i++) {
    uint8_t block[AES_BLOCK_LEN] = {0};
    block[0] = highPriority ? PRIORITY_HIGH : PRIORITY_NORMAL;
    block[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
    block[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
    block[3] = (uint8_t)i;
    block[4] = (uint8_t)total;

    String chunk = msg.substring(i * FRAG_DATA_LEN, min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
    memcpy(&block[5], chunk.c_str(), chunk.length());
    encryptFragment(block);

    Fragment frag;
    memcpy(frag.data, block, AES_BLOCK_LEN);
    frag.retries = 0;
    frag.timestamp = millis();
    frags.push_back(frag);

    for (int r = 0; r < 2; r++) {
      int state = lora.transmit(block, AES_BLOCK_LEN);
      if (state == RADIOLIB_ERR_NONE) {
        Serial.printf("SEND|FRAG|MSGID=%s|SEQ=%d/%d|TRY=%d\n", msgId.c_str(), i + 1, total, r + 1);
      } else {
        Serial.print("ERR|TX_FAIL|");
        Serial.println(state);
      }
    }
  }

  outgoing[msgId] = frags;
  //delay(1000);
  //sendVerifyRequest(msgId);
}

void processConfirm(uint8_t* buf) {
  decryptFragment(buf);
  if (buf[0] != TYPE_VERIFY_REPLY) return;

  String msgId = String((buf[1] << 8) | buf[2], HEX);
  char result[10] = {0};
  memcpy(result, &buf[3], 6);
  if (String(result) == "OK") {
    Serial.printf("CONFIRM|OK|%s\n", msgId.c_str());
    outgoing.erase(msgId);
  } else {
    Serial.printf("CONFIRM|MISSING|%s\n", msgId.c_str());
  }
}

void processFragment(uint8_t* buf) {
  decryptFragment(buf);
  uint8_t type = buf[0];

  // If the message is a text fragement
  if ((type & 0x0F) == TYPE_TEXT_FRAGMENT) {
    String msgId = String((buf[1] << 8) | buf[2], HEX);
    uint8_t seq = buf[3];
    uint8_t total = buf[4];
    String part = "";

    for (int i = 5; i < AES_BLOCK_LEN; i++) {
      if (buf[i] == 0x00) break;
      part += (char)buf[i];
    }

    IncomingText& msg = incoming[msgId];
    msg.total = total;
    msg.parts[seq] = part;
    msg.start = millis();
    if (msg.received.empty()) {
      msg.received.resize(total, false);
    }
    msg.received[seq] = true;

    Serial.printf("FRAG|RECV|%s|%d/%d\n", msgId.c_str(), seq + 1, total);

    uint8_t ack[AES_BLOCK_LEN] = {0};
    ack[0] = TYPE_ACK_FRAGMENT;
    ack[1] = buf[1];
    ack[2] = buf[2];
    ack[3] = buf[3];
    encryptFragment(ack);
    delay(10);
    lora.transmit(ack, AES_BLOCK_LEN);
    delay(50);
    lora.startReceive();

    bool complete = true;
    for (bool got : msg.received) {
      if (!got) {
        complete = false;
        break;
      }
    }

    if (complete) {
      if (isRecentMessage(msgId)) {
        Serial.print("SUPPRESS|DUPLICATE|");
        Serial.println(msgId);
        incoming.erase(msgId);
        return;
      }

      String fullMessage;
      for (int i = 0; i < total; i++) fullMessage += msg.parts[i];

      int sep = fullMessage.indexOf('|');
      String sender = fullMessage.substring(0, sep);
      String message = fullMessage.substring(sep + 1);

      Serial.print("RECV|"); 
      Serial.print(sender);
      Serial.print("|");
      Serial.print(message);
      Serial.print("|");
      Serial.println(msgId);

      uint8_t ackConfirm[AES_BLOCK_LEN] = {0};
      ackConfirm[0] = TYPE_ACK_CONFIRM;
      ackConfirm[1] = buf[1];
      ackConfirm[2] = buf[2];
      ackConfirm[3] = 0xAC; // Arbitrary marker for ACK_CONFIRM
      encryptFragment(ackConfirm);
      delay(10);
      lora.transmit(ackConfirm, AES_BLOCK_LEN);
      delay(50);
      lora.startReceive();
    }

  } else if (type == TYPE_ACK_CONFIRM) {
    Serial.print("RECV|ACK_CONFIRM|");
    String msgId = String((buf[1] << 8) | buf[2], HEX);
    msgId.toUpperCase();
    Serial.print(msgId);
    Serial.println();
  }
}

void processAck(uint8_t* buf) {
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
  for (auto it = outgoing.begin(); it != outgoing.end(); ) {
    bool allAcked = true;
    for (auto& frag : it->second) {
      if (frag.acked || frag.retries >= MAX_RETRIES) continue;

      unsigned long start = millis();
      while (millis() - start < RETRY_INTERVAL) {
        uint8_t buf[128];
        int state = lora.receive(buf, sizeof(buf));
        if (state == RADIOLIB_ERR_NONE) {
          processFragment(buf);
          processAck(buf);
        }

        if (frag.acked) {
          Serial.print("RETRY|SKIP|ACKED|");
          for (int i = 0; i < AES_BLOCK_LEN; i++) Serial.printf("%02X", frag.data[i]);
          Serial.println();
          break;
        }
      }

      if (!frag.acked) {
        int state = lora.transmit(frag.data, AES_BLOCK_LEN);
        if (state == RADIOLIB_ERR_NONE) {
          frag.timestamp = millis();
          frag.retries++;
          Serial.print("RETRY|SEND|");
          for (int i = 0; i < AES_BLOCK_LEN; i++) Serial.printf("%02X", frag.data[i]);
          Serial.println();
        } else {
          Serial.print("RETRY|FAIL|");
          Serial.println(state);
        }

        lora.startReceive();
      }

      if (!frag.acked && frag.retries < MAX_RETRIES) allAcked = false;
    }

    if (allAcked) it = outgoing.erase(it);
    else ++it;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  aes.setKey(aes_key, sizeof(aes_key));
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(deviceName, sizeof(deviceName), "%04X", (uint16_t)((chipId >> 32) & 0xFFFF));

  int state = lora.begin(FREQUENCY);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("ERR|INIT_FAIL|"); Serial.println(state);
    while (true);
  }

  lora.setBandwidth(BANDWIDTH);
  lora.setSpreadingFactor(SPREADING_FACTOR);
  lora.setCodingRate(CODING_RATE);
  lora.setPreambleLength(PREAMBLE_LENGTH);
  lora.setSyncWord(SYNC_WORD);
  lora.setOutputPower(TX_POWER);
  lora.setCRC(true);

  Serial.print("INIT|LoRa Ready as "); Serial.println(deviceName);
  delay(1000 + random(0, 1500));
  lora.startReceive();
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("AT+MSG=")) {
      String msg = input.substring(7);
      sendEncryptedText(msg);
    } else if (input.startsWith("AT+GPS=")) {
      String coords = input.substring(7);
      sendEncryptedText("GPS:" + coords);
    } else {
      Serial.println("ERR|UNKNOWN_CMD");
    }
  }

  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    if (buf[0] == TYPE_ACK_FRAGMENT) {
      processAck(buf);
    } else if (buf[0] == TYPE_VERIFY_REPLY) {
      processConfirm(buf);
    } else {
      processFragment(buf);
    }
    lora.startReceive();
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("ERR|RX_FAIL|");
    Serial.println(state);
    lora.startReceive();
  }

  retryFragments();
}
