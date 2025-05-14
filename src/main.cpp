#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include <vector>

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2

#define FREQUENCY        915.0
#define BANDWIDTH        500.0
#define SPREADING_FACTOR 12
#define CODING_RATE      5
#define PREAMBLE_LENGTH  12
#define SYNC_WORD        0xF3
#define TX_POWER         14

#define MAX_PAYLOAD_LEN  240
#define MAX_RETRIES      3
#define RETRY_INTERVAL   5000  // ms

char deviceName[16];
bool awaitingAck = false;
unsigned long lastSendTime = 0;
String lastSentMessage = "";

AES128 aes;
uint8_t aes_key[16] = {
  0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
  0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81
};

struct PendingMessage {
  String message;
  int retries = 0;
  unsigned long timestamp = 0;
};

std::vector<PendingMessage> retryBuffer;

String encryptPayload(String plain) {
  uint8_t block[32] = {0};
  strncpy((char*)block, plain.c_str(), sizeof(block));
  for (int i = 0; i < 32; i += 16) {
    aes.encryptBlock(block + i, block + i);
  }

  char hex[65] = {0};
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02X", block[i]);
  }
  return String(hex);
}

String decryptPayload(String hex) {
  uint8_t block[32] = {0};
  for (int i = 0; i < 32 && i * 2 + 1 < hex.length(); i++) {
    sscanf(hex.c_str() + i * 2, "%2hhx", &block[i]);
  }

  for (int i = 0; i < 32; i += 16) {
    aes.decryptBlock(block + i, block + i);
  }

  char output[33] = {0};
  strncpy(output, (char*)block, 32);
  return String(output);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  uint64_t chipId = ESP.getEfuseMac();
  snprintf(deviceName, sizeof(deviceName), "NODE-%04X", (uint16_t)(chipId & 0xFFFF));

  randomSeed(esp_random());

  aes.setKey(aes_key, sizeof(aes_key));

  Serial.println("BOOT|Starting LoRa init...");

  int state = lora.begin(FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE, SYNC_WORD, PREAMBLE_LENGTH, TX_POWER);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("ERR|INIT_FAIL|");
    Serial.println(state);
    while (true);
  }

  Serial.print("INIT|LoRa ready as ");
  Serial.println(deviceName);
  lora.startReceive();
}

void sendMessage(String msg) {
  if (msg.length() > MAX_PAYLOAD_LEN) {
    Serial.println("ERR|MSG_TOO_LONG");
    return;
  }

  delay(random(50, 200));
  int state = lora.transmit((uint8_t*)msg.c_str(), msg.length());
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("SEND|OK|");
    Serial.println(msg);
    lastSentMessage = msg;
    lastSendTime = millis();
    awaitingAck = true;
  } else {
    Serial.print("ERR|TX_FAIL|");
    Serial.println(state);
  }

  lora.startReceive();
}

void retryFailedMessages() {
  for (int i = 0; i < retryBuffer.size(); i++) {
    PendingMessage& pending = retryBuffer[i];
    if (millis() - pending.timestamp >= RETRY_INTERVAL) {
      if (pending.retries >= MAX_RETRIES) {
        Serial.print("DROP|GIVE_UP|");
        Serial.println(pending.message);
        retryBuffer.erase(retryBuffer.begin() + i);
        i--;
        continue;
      }

      Serial.print("RETRY|");
      Serial.println(pending.message);
      delay(random(50, 150));
      int state = lora.transmit((uint8_t*)pending.message.c_str(), pending.message.length());

      if (state == RADIOLIB_ERR_NONE) {
        Serial.print("RETRY|SENT|");
        Serial.println(pending.message);
        pending.timestamp = millis();
        pending.retries++;
        awaitingAck = true;
        lastSendTime = millis();
      } else {
        Serial.print("RETRY|FAIL|");
        Serial.println(state);
      }

      lora.startReceive();
    }
  }
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("AT+MSG=")) {
      String rawPayload = input.substring(7);
      String encrypted = encryptPayload(rawPayload);
      String formatted = "MSG|" + String(deviceName) + "|" + encrypted + "|" + String(millis());

      sendMessage(formatted);

      PendingMessage pending;
      pending.message = formatted;
      pending.retries = 0;
      pending.timestamp = millis();
      retryBuffer.push_back(pending);
    } else {
      Serial.println("ERR|UNKNOWN_CMD");
    }
  }

  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    String received = (char*)buf;
    Serial.print("RECV|RAW|");
    Serial.println(received);

    if (received.startsWith("ACK|")) {
      String ackId = received.substring(received.lastIndexOf('|') + 1);
      for (int i = 0; i < retryBuffer.size(); i++) {
        String sentId = retryBuffer[i].message.substring(retryBuffer[i].message.lastIndexOf('|') + 1);
        if (ackId == sentId) {
          Serial.print("ACK|CONFIRMED|");
          Serial.println(retryBuffer[i].message);
          retryBuffer.erase(retryBuffer.begin() + i);
          break;
        }
      }
      awaitingAck = false;

    } else if (received.startsWith("MSG|")) {
      int p1 = received.indexOf('|');
      int p2 = received.indexOf('|', p1 + 1);
      int p3 = received.indexOf('|', p2 + 1);

      String sender = received.substring(p1 + 1, p2);
      String encrypted = received.substring(p2 + 1, p3);
      String msgId = received.substring(p3 + 1);

      String message = decryptPayload(encrypted);

      Serial.print("RECV|FROM=");
      Serial.print(sender);
      Serial.print("|MSG=");
      Serial.print(message);
      Serial.print("|ID=");
      Serial.println(msgId);

      String ackMsg = "ACK|" + String(deviceName) + "|for:" + received;
      if (ackMsg.length() <= MAX_PAYLOAD_LEN) {
        int ackState = lora.transmit((uint8_t*)ackMsg.c_str(), ackMsg.length());
        if (ackState == RADIOLIB_ERR_NONE) {
          Serial.print("SEND|");
          Serial.println(ackMsg);
        } else {
          Serial.print("ERR|ACK_TX_FAIL|");
          Serial.println(ackState);
        }
      }
    }

    lora.startReceive();
    awaitingAck = false;
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("ERR|RX_FAIL|");
    Serial.println(state);
    lora.startReceive();
  }

  retryFailedMessages();
}
