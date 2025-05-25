#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "comms/lora.h"
#include "config.h"
#include "gps/gps.h"
#include "system/display.h"
#include "system/settings.h"
#include "tasks/task_app.h"
#include "tasks/task_lora.h"
#include <RF24.h>


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, RST_OLED_PIN);
bool isTransmitting = false;

// NRF24
RF24 radio(38, 39);  // CE, CSN

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Setup the RF24 Radio
  SPI.begin(40, 42, 41, 39);  // SCK, MISO, MOSI, CSN

  if (!radio.begin()) {
    Serial.println("NRF|ERROR|INIT_FAIL");
    while (1);
  }

  const byte address[6] = "00001";
  radio.openWritingPipe(address);
  radio.setPALevel(RF24_PA_MIN);
  radio.stopListening();

  delay(1000);  // Allow time for the radio to initialize
  const char text[] = "Hello NRF24!";
  if (radio.write(&text, sizeof(text))) {
    Serial.println("NRF|INFO|SEND_OK");
  } else {
    Serial.println("NRF|ERROR|SEND_FAIL");
  }

  //pinMode(LED_PIN, OUTPUT);
  //loadSettings();
  
  //initDisplay(settings.deviceName, settings.frequency, settings.txPower);
  
  //initLoRa(settings.frequency, settings.txPower);
  
  //initGPS();

  //xTaskCreatePinnedToCore(taskLoRaHandler, "LoRaTask", 4096, NULL, 1, NULL, 1);
  //xTaskCreatePinnedToCore(taskAppHandler, "AppTask", 8192, NULL, 1, NULL, 0);
  //xTaskCreatePinnedToCore(taskNRF24Handler,"NRF24Task",4096, NULL,1,NULL, 1); // Run on core 1 with LoRa????
}

void loop() {
  //if (radio.available()) {
  //  char received[32] = {0};
  //  radio.read(&received, sizeof(received));
  //  Serial.print("NRF|INFO|RECEIVED|");
  //  Serial.println(received);
  //}
}
