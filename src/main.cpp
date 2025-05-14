#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include <vector>
#include <map>

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2 pins

#define FREQUENCY        915.0
#define BANDWIDTH        500.0
#define SPREADING_FACTOR 9
#define CODING_RATE      6
#define PREAMBLE_LENGTH  12
#define SYNC_WORD        0xF3
#define TX_POWER         22

#define MAX_RETRIES      3
#define RETRY_INTERVAL   5000  // ms
#define FRAG_DATA_LEN    11
#define AES_BLOCK_LEN    16

#define TYPE_TEXT_FRAGMENT 0x03
#define TYPE_ACK_FRAGMENT  0x04

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
};

std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;

String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

void encryptFragment(uint8_t* input) {
  aes.encryptBlock(input, input);
}

void decryptFragment(uint8_t* input) {
  aes.decryptBlock(input, input);
}

void sendEncryptedText(String msg) {
  msg = String(deviceName) + "|" + msg;  // prepend device name

  std::vector<Fragment> frags;
  String msgId = generateMsgID();
  int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  for (int i = 0; i < total; i++) {
    uint8_t block[AES_BLOCK_LEN] = {0};
    block[0] = TYPE_TEXT_FRAGMENT;
    block[1] = (uint8_t)(msgId.toInt() >> 8);
    block[2] = (uint8_t)(msgId.toInt() & 0xFF);
    block[3] = (uint8_t)i;
    block[4] = (uint8_t)total;

    String chunk = msg.substring(i * FRAG_DATA_LEN, (i + 1) * FRAG_DATA_LEN);
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

void processFragment(uint8_t* buf) {
  decryptFragment(buf);
  if (buf[0] != TYPE_TEXT_FRAGMENT) return;

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

  // Send ACK
  uint8_t ack[AES_BLOCK_LEN] = {0};
  ack[0] = TYPE_ACK_FRAGMENT;
  ack[1] = buf[1];
  ack[2] = buf[2];
  ack[3] = buf[3];
  encryptFragment(ack);
  lora.transmit(ack, AES_BLOCK_LEN);

  if (msg.parts.size() == total) {
    String complete;
    for (int i = 0; i < total; i++) complete += msg.parts[i];

    int sep = complete.indexOf('|');
    String sender = complete.substring(0, sep);
    String message = complete.substring(sep + 1);

    Serial.print("RECV|FROM=");
    Serial.print(sender);
    Serial.print("|MSG=");
    Serial.print(message);
    Serial.print("|ID=");
    Serial.println(msgId);

    incoming.erase(msgId);
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

      if (millis() - frag.timestamp >= RETRY_INTERVAL) {
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
  snprintf(deviceName, sizeof(deviceName), "R-%04X", (uint16_t)((chipId >> 32) & 0xFFFF));

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
