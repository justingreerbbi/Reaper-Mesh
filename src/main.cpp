/**
 * @file main.cpp
 * @brief Encrypted LoRa Messaging Firmware for Heltec V3.2
 *
 * Implements AES-128 encrypted, fragmented messaging over LoRa with basic ACK
 * support, retry logic, OLED status display, command interface over Serial,
 * and EEPROM-backed settings.
 */

#include <AES.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Crypto.h>
#include <EEPROM.h>
#include <RadioLib.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

#include <map>
#include <vector>

#define REAPER_VERSION "1.77.6"

// EEPROM settings storage
#define EEPROM_SIZE 128
#define ADDR_MAGIC 0
#define ADDR_SETTINGS 4
#define EEPROM_MAGIC 0x42

// Default LoRa parameters (overridden by settings)
#define BANDWIDTH 500.0
#define SPREADING_FACTOR 12
#define CODING_RATE 8
#define PREAMBLE_LENGTH 20
#define SYNC_WORD 0xF3

// Fragmentation & retry config defaults
#define FRAG_DATA_LEN 11
#define AES_BLOCK_LEN 16

// Packet types
#define TYPE_TEXT_FRAGMENT 0x03
#define TYPE_ACK_FRAGMENT 0x04
#define TYPE_REFRAGMENT_REQ 0x05
#define TYPE_VERIFY_REQUEST 0x06
#define TYPE_VERIFY_REPLY 0x07
#define TYPE_ACK_CONFIRM 0x08

#define PRIORITY_NORMAL 0x03
#define PRIORITY_HIGH 0x13

#define BROADCAST_MEMORY_TIME 30000UL
#define REQ_TIMEOUT 2000UL

// GPIO / Display
#define LED_PIN 35
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_POWER_PIN 36
#define RST_OLED_PIN 21
#define SCL_OLED_PIN 18
#define SDA_OLED_PIN 17

// Beacon config
#define BEACON_ENABLED false
#define BEACON_INTERVAL 0

// Global Status
bool isTransmitting = false;
bool isReceiving = false;

// Settings struct saved to EEPROM
struct Settings {
  char deviceName[16];
  float frequency;  // 900.0 to 915.0 step 4.0
  int txPower;
  int maxRetries;
  unsigned long retryInterval;
  unsigned long beaconInterval;
  bool beaconEnabled;
};

// Globals
Settings settings;
char deviceName[16];

// Prototypes
void loadSettings();
void saveSettings();
void applySettings();
void processATSetting(const String &cmd);
void processATBulk(const String &cmd);

// LoRa & display objects
SX1262 lora = new Module(8, 14, 12, 13);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, RST_OLED_PIN);

// AES
AES128 aes;
uint8_t aes_key[16] = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
                       0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81};

// Handshake state
String currentMsgId;

// Original message logic types
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

// Utility
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
void encryptFragment(uint8_t *b) { aes.encryptBlock(b, b); }
void decryptFragment(uint8_t *b) { aes.decryptBlock(b, b); }

/*== SEND ENCRYPTED TEXT */
void sendEncryptedText(String msg) {
  // If TX is busy, return error
  if (isTransmitting) {
    Serial.println("ERR|TX_BUSY");
    return;
  }
  isTransmitting = true;

  bool highPriority = false;
  if (msg.startsWith("!")) {
    highPriority = true;
    msg.remove(0, 1);
  }

  msg = String(settings.deviceName) + "|" + msg;
  std::vector<Fragment> frags;
  String msgId = generateMsgID();
  currentMsgId = msgId;
  int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  delay(random(100, 400));  // initial jitter

  for (int i = 0; i < total; i++) {
    uint8_t block[AES_BLOCK_LEN] = {0};
    block[0] = highPriority ? PRIORITY_HIGH : PRIORITY_NORMAL;
    block[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
    block[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
    block[3] = (uint8_t)i;
    block[4] = (uint8_t)total;

    String chunk = msg.substring(
        i * FRAG_DATA_LEN, min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
    memcpy(&block[5], chunk.c_str(), chunk.length());
    encryptFragment(block);

    Fragment frag;
    memcpy(frag.data, block, AES_BLOCK_LEN);
    frag.retries = 0;
    frag.timestamp = millis();
    frag.acked = false;
    frags.push_back(frag);

    for (int r = 0; r < settings.maxRetries + 1; r++) {
      int state = lora.transmit(block, AES_BLOCK_LEN);
      if (state == RADIOLIB_ERR_NONE) {
        Serial.printf("SEND|FRAG|%s|%d/%d|TRY=%d\n", msgId.c_str(), i + 1,
                      total, r + 1);
        break;
      }
      Serial.print("ERR|TX_FAIL|");
      Serial.println(state);
    }
  }

  outgoing[msgId] = frags;
  isTransmitting = false;  // Reset TX state
}

/*== PROCESS A FRAGMENT */
void processFragment(uint8_t *buf) {
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

      int sep = fullMessage.indexOf('|');
      String sender = fullMessage.substring(0, sep);
      String message = fullMessage.substring(sep + 1);

      Serial.print("RECV|");
      Serial.print(sender);
      Serial.print("|");
      Serial.print(message);
      Serial.print("|");
      Serial.println(msgId);
    }
  } else if (type == TYPE_ACK_CONFIRM) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);

    Serial.print("RECV|ACK_CONFIRM|");
    char bufId[5];
    snprintf(bufId, sizeof(bufId), "%02X%02X", buf[1], buf[2]);
    String mid(bufId);
    mid.toUpperCase();
    Serial.println(mid);

    // Unconditionally mark all fragments for this msgId as acknowledged
    auto it = outgoing.find(mid);
    if (it != outgoing.end()) {
      for (auto &frag : it->second) frag.acked = true;
    }
  }
}

// Original processAck
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

// Original retryFragments
void retryFragments() {
  for (auto it = outgoing.begin(); it != outgoing.end();) {
    bool allAcked = true;
    for (auto &frag : it->second) {
      if (frag.acked || frag.retries >= settings.maxRetries) continue;
      unsigned long start = millis();
      while (millis() - start < settings.retryInterval) {
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
          Serial.printf("SEND|RETRY|%s|%d/%d|try=%d\n", it->first.c_str(),
                        (&frag - &it->second[0]) + 1, (int)it->second.size(),
                        frag.retries);
        }
        lora.startReceive();
      }
      if (!frag.acked && frag.retries < settings.maxRetries) allAcked = false;
    }
    if (allAcked)
      it = outgoing.erase(it);
    else
      ++it;
  }
}

// Original sendBeacon
void sendBeacon() {
  sendEncryptedText("BEACON");
  Serial.println("LOG|BEACON_SENT");
}

/*== LOAD SETTINGS */
void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(ADDR_MAGIC) != EEPROM_MAGIC) {
    uint16_t cid = (uint16_t)((ESP.getEfuseMac() >> 32) & 0xFFFF);
    snprintf(settings.deviceName, sizeof(settings.deviceName), "%04X", cid);
    settings.frequency = 915.0;
    settings.txPower = 22;
    settings.maxRetries = 1;
    settings.retryInterval = 1000;
    settings.beaconInterval = 10000;
    settings.beaconEnabled = true;
    EEPROM.write(ADDR_MAGIC, EEPROM_MAGIC);
    EEPROM.put(ADDR_SETTINGS, settings);
    EEPROM.commit();
  } else {
    EEPROM.get(ADDR_SETTINGS, settings);
  }
}

/*== SAVE SETTINGS */
void saveSettings() {
  EEPROM.put(ADDR_SETTINGS, settings);
  EEPROM.commit();
}

/*== APPLY SETTINGS */
void applySettings() {
  lora.begin(settings.frequency);
  lora.setBandwidth(BANDWIDTH);
  lora.setSpreadingFactor(SPREADING_FACTOR);
  lora.setCodingRate(CODING_RATE);
  lora.setPreambleLength(PREAMBLE_LENGTH);
  lora.setSyncWord(SYNC_WORD);
  lora.setOutputPower(settings.txPower);
  lora.setCRC(true);
}

/*== SET SETTINGS */
void setSetting(const String &k, const String &v) {
  if (k == "name")
    v.toCharArray(settings.deviceName, sizeof(settings.deviceName));
  else if (k == "freq") {
    float f = v.toFloat();
    if (f >= 900.0 && f <= 915.0 &&
        fabs(f - 900.0 - round((f - 900.0) / 4.0) * 4.0) < 0.01)
      settings.frequency = f;
  } else if (k == "power")
    settings.txPower = v.toInt();
  else if (k == "maxret")
    settings.maxRetries = v.toInt();
  else if (k == "retryint")
    settings.retryInterval = v.toInt();
  else if (k == "beaconint")
    settings.beaconInterval = v.toInt();
  else if (k == "beacon")
    settings.beaconEnabled = v.equalsIgnoreCase("true");
  else
    Serial.println("ERR|UNKNOWN_SETTING");
}

/*== PROCESS AT COMMANDS FOR SETTINGS */
void processATSetting(const String &cmd) {
  int eq = cmd.indexOf('=');
  String kv = cmd.substring(eq + 1);
  int c = kv.indexOf(',');
  setSetting(kv.substring(0, c), kv.substring(c + 1));
  saveSettings();
  Serial.println("RESTARTING");
  delay(2000);
  ESP.restart();
}

/*== PROCESS BULK AT COMMANDS FOR SETTINGS */
void processATBulk(const String &cmd) {
  String list = cmd.substring(cmd.indexOf('=') + 1);
  int pos = 0;
  while (pos < list.length()) {
    int sc = list.indexOf(';', pos);
    String pr = (sc < 0 ? list.substring(pos) : list.substring(pos, sc));
    int c = pr.indexOf(',');
    setSetting(pr.substring(0, c), pr.substring(c + 1));
    pos = (sc < 0 ? list.length() : sc + 1);
  }
  saveSettings();
  Serial.println("RESTARTING");
  delay(100);
  ESP.restart();
}

/*== WIPE DEVICE MEMORY/FACTORY RESET & RESTART */
void wipeDeviceAndRestart() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; ++i) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  Serial.println("LOG|EEPROM_WIPED_RESTARTING");
  delay(500);
  ESP.restart();
}

/*== MAIN SETUP */
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(OLED_POWER_PIN, OUTPUT);
  digitalWrite(OLED_POWER_PIN, LOW);
  delay(50);
  Wire.begin(SDA_OLED_PIN, SCL_OLED_PIN, 500000);
  Serial.begin(115200);
  while (!Serial);
  Serial.println("LOG|STARTING");

  loadSettings();
  applySettings();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("LOG|OLED_FAILED");
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("Name:%s\nFreq:%.1f\nPwr:%d\n", settings.deviceName,
                 settings.frequency, settings.txPower);
  display.display();

  aes.setKey(aes_key, sizeof(aes_key));
  uint16_t cid = (uint16_t)((ESP.getEfuseMac() >> 32) & 0xFFFF);
  snprintf(deviceName, sizeof(deviceName), "%04X", cid);

  int st = lora.begin(settings.frequency);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("ERR|INIT_FAIL|%d\n", st);
    while (1);
  }
  Serial.println("LOG|DEVICE_CONNECTED");
  lora.setBandwidth(BANDWIDTH);
  lora.setSpreadingFactor(SPREADING_FACTOR);
  lora.setCodingRate(CODING_RATE);
  lora.setPreambleLength(PREAMBLE_LENGTH);
  lora.setSyncWord(SYNC_WORD);
  lora.setOutputPower(settings.txPower);
  lora.setCRC(true);
  lora.startReceive();
  digitalWrite(LED_PIN, LOW);
}

/*== MAIN LOOP */
void loop() {
  if (Serial.available()) {
    String in = Serial.readStringUntil('\n');
    in.trim();
    if (in.startsWith("AT+SET="))
      processATSetting(in);
    else if (in.startsWith("AT+SETA="))
      processATBulk(in);
    else if (in.startsWith("AT+MSG="))
      sendEncryptedText(in.substring(7));
    else if (in.startsWith("AT+GPS="))
      sendEncryptedText("GPS:" + in.substring(7));
    else if (in.startsWith("AT+RESET_DEVICE"))
      wipeDeviceAndRestart();
    else if (in.startsWith("AT+SEND_BEACON")) {
      if (settings.beaconEnabled)
        sendBeacon();
      else
        Serial.println("ERR|BEACON_DISABLED");
    } else if (in.startsWith("AT+GET_SETTINGS")) {
      Serial.print("SETTINGS|");
      Serial.print(settings.deviceName);
      Serial.print("|");
      Serial.print(settings.frequency);
      Serial.print("|");
      Serial.print(settings.txPower);
      Serial.print("|");
      Serial.print(settings.maxRetries);
      Serial.print("|");
      Serial.print(settings.retryInterval);
      Serial.print("|");
      Serial.print(settings.beaconInterval);
      Serial.print("|");
      Serial.println(settings.beaconEnabled ? "true" : "false");
    } else if (in.startsWith("AT+DEVICE"))
      Serial.println("HELTEC|READY|REAPER_A3");
    else
      Serial.println("ERR|UNKNOWN_CMD");
  }

  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    processFragment(buf);
    lora.startReceive();
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.printf("ERR|RX_FAIL|%d\n", state);
    lora.startReceive();
  }

  retryFragments();

  static unsigned long lastBeacon = 0;
  static bool beaconSent = false;
  unsigned long now = millis();

  if (!beaconSent) {
    // sendBeacon();
    lastBeacon = now;
    beaconSent = true;
  } else if (now - lastBeacon >= settings.beaconInterval) {
    // sendBeacon();
    lastBeacon = now;
  }
}
