#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include "config.h"
#include "gps/gps.h"
#include "system/display.h"
#include "system/settings.h"
#include "tasks/app.h"
#include "tasks/lora.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, RST_OLED_PIN);

/**
 * MAIN APP ENTRY POINT
 */
void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(LED_PIN, OUTPUT);
  loadSettings();
  initDisplay(settings.deviceName, settings.frequency, settings.txPower);
  initLoRa(settings.frequency, settings.txPower);
  initGPS();

  xTaskCreatePinnedToCore(taskLoRaHandler, "LoRaTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskAppHandler, "AppTask", 8192, NULL, 1, NULL, 0);
}

void loop() {}
