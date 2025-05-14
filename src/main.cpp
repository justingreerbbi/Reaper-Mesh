/**
 * @file main.cpp
 * @brief Encrypted LoRa Messaging Firmware for Heltec V3.2
 * 
 * Implements AES-128 encrypted, fragmented messaging over LoRa with basic ACK support,
 * retry logic, OLED status display, and command interface over Serial.
 */

#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include <vector>
#include <map>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Versioning
#define REAPER_VERSION "1.77.6"

// LoRa radio parameters
#define FREQUENCY        915.0
#define BANDWIDTH        500.0
#define SPREADING_FACTOR 12
#define CODING_RATE      8
#define PREAMBLE_LENGTH  20
#define SYNC_WORD        0xF3
#define TX_POWER         22

// Fragmentation & retry config
#define MAX_RETRIES      1
#define RETRY_INTERVAL   1000 // ms between retries
#define FRAG_DATA_LEN    11   // Max plaintext bytes per fragment
#define AES_BLOCK_LEN    16   // AES block size (bytes)

// Packet type definitions
#define TYPE_TEXT_FRAGMENT  0x03
#define TYPE_ACK_FRAGMENT   0x04
#define TYPE_REFRAGMENT_REQ 0x05
#define TYPE_VERIFY_REQUEST 0x06
#define TYPE_VERIFY_REPLY   0x07
#define TYPE_ACK_CONFIRM    0x08

// Message priority markers
#define PRIORITY_NORMAL 0x03
#define PRIORITY_HIGH   0x13

// Replay protection
#define BROADCAST_MEMORY_TIME 30000 // ms to remember seen message IDs
#define REQ_TIMEOUT 2000 // timeout for some request-based interactions (unused here)

// OLED and LED config
#define LED_PIN 35
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_POWER_PIN 36
#define RST_OLED_PIN 21
#define SCL_OLED_PIN 18
#define SDA_OLED_PIN 17

// Becon Config
#define BEACON_ENABLED true
#define BEACON_INTERVAL 50000 // ms between beacons

// LoRa module (SX1262) and OLED setup
SX1262 lora = new Module(8, 14, 12, 13);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, RST_OLED_PIN);

// State flags
bool isReceiving_flag = false;
bool isSending_flag = false;
bool retry_flag = false;

// Device name buffer
char deviceName[16];

// AES-128 setup
AES128 aes;
uint8_t aes_key[16] = {
  0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
  0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81
};

// Fragment buffer and state tracking
struct Fragment {
  uint8_t data[AES_BLOCK_LEN];
  int retries;
  unsigned long timestamp;
  bool acked = false;
};

// Incoming message structure
struct IncomingText {
  int total;
  unsigned long start;
  std::map<uint8_t, String> parts;
  std::vector<bool> received;
};

// Message tracking maps
std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

// Function declarations
void processFragment(uint8_t* buf);
void processAck(uint8_t* buf);

// Generates a 4-digit hex message ID using random value
String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

// Replay protection: return true if msgId was seen recently
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

// AES helper functions
void encryptFragment(uint8_t* input) {
  aes.encryptBlock(input, input);
}
void decryptFragment(uint8_t* input) {
  aes.decryptBlock(input, input);
}

// Encrypts, fragments, and sends a message
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
  delay(random(100, 400)); // prevent collision

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

    for (int r = 0; r < 1; r++) { // 1 or 2 times for reliability
      int state = lora.transmit(block, AES_BLOCK_LEN);
      if (state == RADIOLIB_ERR_NONE) {
        Serial.printf("SEND|FRAG|%s|%d/%d|TRY=%d\n", msgId.c_str(), i + 1, total, r + 1);
      } else {
        Serial.print("ERR|TX_FAIL|");
        Serial.println(state);
      }
    }
  }

  outgoing[msgId] = frags;
}

// Handles received fragments or ACK_CONFIRM
/**
 * @brief Processes an incoming data fragment buffer, handling both text message fragments and ACK confirmations.
 *
 * This function decrypts the provided buffer, determines the fragment type, and processes it accordingly:
 * - For text fragments, it reconstructs multi-part messages, tracks received parts, and assembles the full message
 *   when all fragments are received. It also sends an ACK_CONFIRM response upon complete reception.
 * - For ACK_CONFIRM fragments, it acknowledges the receipt by toggling an LED and marking the corresponding outgoing
 *   message fragments as acknowledged.
 *
 * @param buf Pointer to the buffer containing the incoming fragment data. The buffer is expected to be at least
 *            AES_BLOCK_LEN bytes long and encrypted.
 *
 * @note Relies on global variables and objects such as `incoming`, `outgoing`, `lora`, and constants like
 *       `TYPE_TEXT_FRAGMENT`, `TYPE_ACK_CONFIRM`, `AES_BLOCK_LEN`, and `LED_PIN`.
 * @note Uses Serial for debug output and assumes the existence of helper functions such as `decryptFragment`,
 *       `encryptFragment`, and `isRecentMessage`.
 */
void processFragment(uint8_t* buf) {
  decryptFragment(buf);
  uint8_t type = buf[0];

  if ((type & 0x0F) == TYPE_TEXT_FRAGMENT) {
    String msgId = String((buf[1] << 8) | buf[2], HEX);
    msgId.toUpperCase();
    uint8_t seq = buf[3];
    uint8_t total = buf[4];
    String part = "";

    for (int i = 5; i < AES_BLOCK_LEN; i++) {
      if (buf[i] == 0x00) break;
      part += (char)buf[i];
    }

    IncomingText& msg = incoming[msgId];
    if (msg.received.size() != total) {
      msg.total = total;
      msg.received.assign(total, false);
    }
    msg.parts[seq] = part;
    msg.received[seq] = true;

    Serial.printf("RECV|FRAG|%s|%d/%d\n", msgId.c_str(), seq + 1, total);

    bool complete = true;
    for (int idx = 0; idx < msg.received.size(); ++idx) {
      if (!msg.received[idx]) {
        complete = false;
        break;
      }
    }

    if (complete) {
      if (isRecentMessage(msgId)) {
        incoming.erase(msgId);
        return;
      }

      // Send ACK_CONFIRM
      uint8_t ackConfirm[AES_BLOCK_LEN] = {0};
      ackConfirm[0] = TYPE_ACK_CONFIRM;
      ackConfirm[1] = buf[1];
      ackConfirm[2] = buf[2];
      ackConfirm[3] = buf[3];
      encryptFragment(ackConfirm);
      lora.transmit(ackConfirm, AES_BLOCK_LEN);
      lora.transmit(ackConfirm, AES_BLOCK_LEN);
      delay(50);
      lora.startReceive();

      String fullMessage;
      for (int i = 0; i < total; i++) fullMessage += msg.parts[i];

      int sep = fullMessage.indexOf('|');
      String sender = fullMessage.substring(0, sep);
      String message = fullMessage.substring(sep + 1);

      Serial.print("RECV|"); Serial.print(sender); Serial.print("|");
      Serial.print(message); Serial.print("|"); Serial.println(msgId);
    }
  } else if (type == TYPE_ACK_CONFIRM) {
    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    digitalWrite(LED_PIN, LOW);

    Serial.print("RECV|ACK_CONFIRM|");
    char msgIdBuf[5];
    snprintf(msgIdBuf, sizeof(msgIdBuf), "%02X%02X", buf[1], buf[2]);
    String msgId = String(msgIdBuf);
    msgId.toUpperCase();
    Serial.println(msgId);

    auto it = outgoing.find(msgId);
    if (it != outgoing.end()) {
      for (auto& frag : it->second) frag.acked = true;
    }
  }
}

// Handles ACK fragment reception
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

// Resend unacked fragments after interval
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
        if (frag.acked) break;
      }

      if (!frag.acked) {
        int state = lora.transmit(frag.data, AES_BLOCK_LEN);
        if (state == RADIOLIB_ERR_NONE) {
          frag.timestamp = millis();
          frag.retries++;
          Serial.printf("SEND|RETRY|%s|%d/%d|try=%d\n",
              it->first.c_str(),
              (&frag - &it->second[0]) + 1,
              (int)it->second.size(),
              frag.retries);
        } else {
          Serial.print("RETRY|FAIL|"); Serial.println(state);
        }

        lora.startReceive();
      }

      if (!frag.acked && frag.retries < MAX_RETRIES) allAcked = false;
    }

    if (allAcked) it = outgoing.erase(it);
    else ++it;
  }
}

// Send a beacon message
void sendBeacon() {
  String beaconMsg = "BEACON";
  sendEncryptedText(beaconMsg);
  Serial.println("SEND|BEACON");
}

// One-time initialization
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(OLED_POWER_PIN, OUTPUT);
  digitalWrite(OLED_POWER_PIN, LOW);
  delay(100);
  Wire.begin(SDA_OLED_PIN, SCL_OLED_PIN, 500000);
  delay(100);
  Serial.begin(115200);
  while (!Serial);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("INIT|OLED_FAILED. CHECK CONNECTIONS.");
    for (;;) ;
  }

  display.clearDisplay();

  // Reaper logo and text
  display.fillCircle(64, 24, 12, SSD1306_WHITE);
  display.fillCircle(64, 27, 12, SSD1306_BLACK);
  display.fillCircle(64, 30, 7, SSD1306_WHITE);
  display.fillCircle(61, 29, 1, SSD1306_BLACK);
  display.fillCircle(67, 29, 1, SSD1306_BLACK);
  display.drawLine(72, 18, 78, 36, SSD1306_WHITE);
  display.drawLine(76, 14, 82, 22, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 54);
  display.print("Reaper - v" REAPER_VERSION);
  display.display();

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
  digitalWrite(LED_PIN, LOW);
}

// Runtime logic
void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.startsWith("AT+MSG=")) {
      sendEncryptedText(input.substring(7));
    } else if (input.startsWith("AT+GPS=")) {
      sendEncryptedText("GPS:" + input.substring(7));
    } else {
      Serial.println("ERR|UNKNOWN_CMD");
    }
  }

  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    processFragment(buf);
    lora.startReceive();
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("ERR|RX_FAIL|"); Serial.println(state);
    lora.startReceive();
  }

  retryFragments();

  // Handle beacon sending
  static unsigned long lastBeacon = 0;
  static bool initBeaconSent = false;
  if (!initBeaconSent) {
    if (BEACON_ENABLED) {
      sendBeacon();
      lastBeacon = millis();
    }
    initBeaconSent = true;
  } else if (BEACON_ENABLED && millis() - lastBeacon > BEACON_INTERVAL) {
    sendBeacon();
    lastBeacon = millis();
  }
}
