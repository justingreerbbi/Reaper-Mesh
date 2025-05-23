// src/config.h
#pragma once
#include <Arduino.h>

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

#define AES_BLOCK_LEN 16
#define BROADCAST_MEMORY_TIME 30000UL

#define GPS_TOLERANCE_LATLON 0.0001
#define GPS_TOLERANCE_ALT 2.0
#define GPS_TOLERANCE_SPEED 1.0
#define GPS_TOLERANCE_COURSE 5.0
#define GPS_DEBOUNCE_MS 3000

struct Settings {
  char deviceName[16];
  float frequency;
  int txPower;
  int maxRetries;
  unsigned long retryInterval;
  unsigned long beaconInterval;
  bool beaconEnabled;
};

extern Settings settings;
