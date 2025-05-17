// src/system/display.cpp
#include "display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "../config.h"

extern Adafruit_SSD1306 display;

void initDisplay(const char* deviceName, float freq, int txPower) {
  pinMode(OLED_POWER_PIN, OUTPUT);
  digitalWrite(OLED_POWER_PIN, LOW);
  delay(50);
  Wire.begin(SDA_OLED_PIN, SCL_OLED_PIN, 500000);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("Name:%s\nFreq:%.1f\nPwr:%d\n", deviceName, freq, txPower);
  display.display();
}
