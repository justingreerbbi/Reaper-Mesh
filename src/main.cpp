#include <RadioLib.h>

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2 SX1262 pins

// LoRa parameters
#define FREQUENCY       915.0
#define BANDWIDTH       500.0
#define SPREADING_FACTOR 7
#define CODING_RATE     5
#define PREAMBLE_LENGTH 8
#define SYNC_WORD       0xF3
#define TX_POWER        14

bool isWaiting = false;
unsigned long lastSendTime = 0;
String lastSentMessage = "";

void setup() {
  Serial.begin(115200);
  while (!Serial);

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
  // Check for serial input to trigger send
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("SEND")) {
      lastSentMessage = "Hello from board at " + String(millis());
      int state = lora.transmit(lastSentMessage);
      if (state == RADIOLIB_ERR_NONE) {
        Serial.print("SEND|OK|");
        Serial.println(lastSentMessage);
        lastSendTime = millis();
        isWaiting = true;
      } else {
        Serial.print("ERR|TX_FAIL|");
        Serial.println(state);
      }
      lora.startReceive();
    }
  }

  // Listen for incoming messages
  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("RECV|");
    Serial.println((char*)buf);
    Serial.print("RSSI=");
    Serial.print(lora.getRSSI());
    Serial.print(" dBm | SNR=");
    Serial.println(lora.getSNR());
    lora.startReceive();
    isWaiting = false;
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("ERR|RX_FAIL|");
    Serial.println(state);
    lora.startReceive();
  }

  // Optional: timeout for ACK-style response (5s max)
  if (isWaiting && millis() - lastSendTime > 5000) {
    Serial.println("RECV|TIMEOUT");
    isWaiting = false;
  }
}
