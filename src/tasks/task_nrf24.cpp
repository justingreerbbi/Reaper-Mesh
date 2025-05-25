#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// NRF24 Pins
#define NRF_CE_PIN   13
#define NRF_CSN_PIN  12
#define NRF_SCK_PIN   9
#define NRF_MOSI_PIN 10
#define NRF_MISO_PIN 11

// NRF24 Setup
RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);  // CE, CSN
const byte address[6] = "NODE1";

void taskNRF24Handler(void* pvParameters) {
  SPI.begin(NRF_SCK_PIN, NRF_MISO_PIN, NRF_MOSI_PIN, NRF_CSN_PIN);

  if (!radio.begin()) {
    Serial.println("NRF|ERROR|INIT_FAIL");
    vTaskDelete(NULL);
    return;
  }

  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_1MBPS);
  radio.openReadingPipe(1, address);
  radio.startListening();

  Serial.println("NRF|STATUS|INIT_SUCCESS");

  while (true) {
    if (radio.available()) {
      char text[32] = {0};
      radio.read(&text, sizeof(text));
      Serial.print("NRF|RECV|");
      Serial.println(text);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
