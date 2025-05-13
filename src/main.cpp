#include <RadioLib.h>

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2 SX1262 pins

#define FREQUENCY        915.0
#define BANDWIDTH        500.0
#define SPREADING_FACTOR 12
#define CODING_RATE      5
#define PREAMBLE_LENGTH  12
#define SYNC_WORD        0xF3
#define TX_POWER         14

#define LED_PIN 25

bool isWaiting = false;
unsigned long lastSendTime = 0;
String lastSentMessage = "";

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

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
  // === Serial command to trigger TX ===
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("SEND")) {
      delay(random(50, 200));  // small backoff to avoid collision
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
      lora.startReceive();  // resume RX after TX
    }
  }

  // === Listen for incoming packets ===
  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("RECV|");
    Serial.println((char*)buf);
    Serial.print("RSSI=");
    Serial.print(lora.getRSSI());
    Serial.print(" dBm | SNR=");
    Serial.println(lora.getSNR());

    // 🔦 Flash LED
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);

    lora.startReceive();  // resume RX
    isWaiting = false;
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("ERR|RX_FAIL|");
    Serial.println(state);
    lora.startReceive();
  }

  // === Optional: timeout if expecting reply ===
  if (isWaiting && millis() - lastSendTime > 500) {
    Serial.println("RECV|TIMEOUT");
    isWaiting = false;
  }
}
