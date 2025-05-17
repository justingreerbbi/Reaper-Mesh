// [Header Includes and Setup – same as before]
#include <AES.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Crypto.h>
#include <EEPROM.h>
#include <HardwareSerial.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

#include <map>
#include <vector>

#define LED_PIN 35
#define OLED_POWER_PIN 36
#define RST_OLED_PIN 21
#define SCL_OLED_PIN 18
#define SDA_OLED_PIN 17
#define GPS_RX_PIN 47
#define GPS_TX_PIN 48
#define GPS_BAUD_RATE 9600

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, RST_OLED_PIN);
SX1262 lora = new Module(8, 14, 12, 13);
TinyGPSPlus gps;
HardwareSerial GPSSerial(2);
int numberOfSatellitesFound = -1;

AES128 aes;
uint8_t aes_key[16] = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
                       0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81};

#define AES_BLOCK_LEN 16
#define FRAG_DATA_LEN 11
#define TYPE_TEXT_FRAGMENT 0x03
#define TYPE_ACK_FRAGMENT 0x04
#define TYPE_ACK_CONFIRM 0x08
#define PRIORITY_NORMAL 0x03
#define BROADCAST_MEMORY_TIME 30000UL

bool isTransmitting = false;
bool startupBeaconSent = false;

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

struct Settings {
  char deviceName[16];
  float frequency;
  int txPower;
  int maxRetries;
  unsigned long retryInterval;
  unsigned long beaconInterval;
  bool beaconEnabled;
};

Settings settings;
char deviceName[16];

std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

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
      if (frag.acked || frag.retries >= settings.maxRetries) continue;
      unsigned long start = millis();
      while (millis() - start < settings.retryInterval) {
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
          Serial.printf("SEND|RETRY|%s|%d|try=%d\n", it->first.c_str(),
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

// === FreeRTOS LoRa RX + ACK task ===
void taskLoRaHandler(void *param) {
  while (true) {
    uint8_t buf[128];
    int state = lora.receive(buf, sizeof(buf));
    if (state == RADIOLIB_ERR_NONE) {
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
            continue;
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

          Serial.printf("RECV|%s|%s|%s\n", sender.c_str(), message.c_str(),
                        msgId.c_str());
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

        auto it = outgoing.find(mid);
        if (it != outgoing.end()) {
          for (auto &frag : it->second) frag.acked = true;
        }
      } else if (type == TYPE_ACK_FRAGMENT) {
        processAck(buf);
      }
    }
    retryFragments();
    lora.startReceive();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// === App logic task (Core 0) ===
void taskAppHandler(void *param) {
  unsigned long lastBeacon = 0;
  unsigned long lastGPSPoll = 0;

  while (true) {
    if (Serial.available()) {
      String in = Serial.readStringUntil('\n');
      in.trim();
      if (in.startsWith("AT+MSG=")) {
        String msg = in.substring(7);
        if (isTransmitting) continue;
        isTransmitting = true;
        msg = String(settings.deviceName) + "|" + msg;
        String msgId = generateMsgID();
        std::vector<Fragment> frags;
        int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

        for (int i = 0; i < total; i++) {
          uint8_t block[AES_BLOCK_LEN] = {0};
          block[0] = PRIORITY_NORMAL;
          block[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
          block[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
          block[3] = i;
          block[4] = total;
          String chunk =
              msg.substring(i * FRAG_DATA_LEN,
                            min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
          memcpy(&block[5], chunk.c_str(), chunk.length());
          encryptFragment(block);
          Fragment frag;
          memcpy(frag.data, block, AES_BLOCK_LEN);
          frag.retries = 0;
          frag.timestamp = millis();
          frag.acked = false;
          frags.push_back(frag);
          lora.transmit(block, AES_BLOCK_LEN);
        }

        outgoing[msgId] = frags;
        isTransmitting = false;
      }
    }

    unsigned long now = millis();

    if (!startupBeaconSent) {
      Serial.println("LOG|BEACON_SENT");
      startupBeaconSent = true;
      lastBeacon = now;
    } else if (now - lastBeacon >= settings.beaconInterval && !isTransmitting) {
      Serial.println("LOG|BEACON_SENT");
      lastBeacon = now;
    }

    if (gps.location.isValid() && now - lastGPSPoll >= 5000) {
      Serial.printf("GPS|%.6f,%.6f,%d,%d,%d,%d\n", gps.location.lat(),
                    gps.location.lng(), (int)gps.altitude.meters(),
                    (int)gps.speed.kmph(), (int)gps.course.deg(),
                    gps.satellites.value());
      lastGPSPoll = now;
    }

    while (GPSSerial.available()) {
      gps.encode(GPSSerial.read());
      int sats = gps.satellites.value();
      if (sats != numberOfSatellitesFound) {
        numberOfSatellitesFound = sats;
        Serial.printf("LOG|SATELLITES_FOUND|%d\n", numberOfSatellitesFound);
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(OLED_POWER_PIN, OUTPUT);
  digitalWrite(OLED_POWER_PIN, LOW);
  delay(50);
  Wire.begin(SDA_OLED_PIN, SCL_OLED_PIN, 500000);
  Serial.begin(115200);
  while (!Serial);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  snprintf(deviceName, sizeof(deviceName), "%04X",
           (uint16_t)((ESP.getEfuseMac() >> 32) & 0xFFFF));
  strcpy(settings.deviceName, deviceName);
  settings.frequency = 915.0;
  settings.txPower = 22;
  settings.maxRetries = 2;
  settings.retryInterval = 1000;
  settings.beaconInterval = 30000;
  settings.beaconEnabled = true;
  display.printf("Name:%s\nFreq:%.1f\nPwr:%d\n", settings.deviceName,
                 settings.frequency, settings.txPower);
  display.display();
  aes.setKey(aes_key, sizeof(aes_key));
  int st = lora.begin(settings.frequency);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("ERR|LORA_INIT_FAILED|%d\n", st);
    while (1);
  }
  lora.setBandwidth(500.0);
  lora.setSpreadingFactor(12);
  lora.setCodingRate(8);
  lora.setPreambleLength(20);
  lora.setSyncWord(0xF3);
  lora.setOutputPower(settings.txPower);
  lora.setCRC(true);
  lora.startReceive();
  GPSSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // FreeRTOS task setup
  xTaskCreatePinnedToCore(taskLoRaHandler, "LoRaTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskAppHandler, "AppTask", 8192, NULL, 1, NULL, 0);
}

void loop() {}
