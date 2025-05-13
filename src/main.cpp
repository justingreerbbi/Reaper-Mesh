#include <RadioLib.h>

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2 SX1262 pins

#define FREQUENCY        915.0
#define BANDWIDTH        500.0
#define SPREADING_FACTOR 12
#define CODING_RATE      5
#define PREAMBLE_LENGTH  12
#define SYNC_WORD        0xF3
#define TX_POWER         14

char deviceName[16];  // global

bool isWaiting = false;
unsigned long lastSendTime = 0;
String lastSentMessage = "";

void setup() {
  Serial.begin(115200);
  while (!Serial);

  uint64_t chipId = ESP.getEfuseMac();
  snprintf(deviceName, sizeof(deviceName), "NODE-%04X", (uint16_t)(chipId & 0xFFFF));

  randomSeed(esp_random());  // seed random for TX jitter

  Serial.println("BOOT|Starting LoRa init...");

  int state = lora.begin(FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE, SYNC_WORD, PREAMBLE_LENGTH, TX_POWER);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("ERR|INIT_FAIL|");
    Serial.println(state);
    while (true);
  }

  Serial.println("INIT|LoRa ready");
  lora.startReceive();
}

void loop() {
  // === Serial AT command interface ===
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("AT+MSG=")) {
      String payload = input.substring(7);  // everything after "AT+MSG="
      String formatted = "MSG|" + String(deviceName) + "|" + payload + "|" + String(millis());

      delay(random(50, 200));  // random delay to reduce collision risk
      int state = lora.transmit(formatted);
      if (state == RADIOLIB_ERR_NONE) {
        Serial.print("SEND|OK|");
        Serial.println(formatted);
        lastSentMessage = formatted;
        lastSendTime = millis();
        isWaiting = true;
      } else {
        Serial.print("ERR|TX_FAIL|");
        Serial.println(state);
      }

      lora.startReceive();  // resume listening
    } else {
      Serial.println("ERR|UNKNOWN_CMD");
    }
  }

  // === Listen for incoming packets ===
  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    String received = (char*)buf;
    Serial.print("RECV|");
    Serial.println(received);
    Serial.print("RSSI=");
    Serial.print(lora.getRSSI());
    Serial.print(" dBm | SNR=");
    Serial.println(lora.getSNR());

    // Format and send ACK
    String ackMsg = "ACK|" + String(deviceName) + "|for:" + received;
    int ackState = lora.transmit(ackMsg);
    if (ackState == RADIOLIB_ERR_NONE) {
      Serial.print("SEND|ACK|");
      Serial.println(ackMsg);
    } else {
      Serial.print("ERR|ACK_TX_FAIL|");
      Serial.println(ackState);
    }

    lora.startReceive();
    isWaiting = false;
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("ERR|RX_FAIL|");
    Serial.println(state);
    lora.startReceive();
  }

  if (isWaiting && millis() - lastSendTime > 500) {
    Serial.println("RECV|TIMEOUT");
    isWaiting = false;
  }
}
