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

#define TYPE_TEXT_FRAGMENT 0x03  // 3
#define TYPE_ACK_FRAGMENT 0x04   // 4
#define TYPE_ACK_CONFIRM 0x08    // 8

#define PRIORITY_NORMAL 0x03  // 3
#define BROADCAST_MEMORY_TIME 30000 // 30 seconds

#define MSG_RETRY_INTERVAL_MS 2000  // Wait 1 second between retry attempts

#define GPS_TOLERANCE_LATLON 0.0001
#define GPS_TOLERANCE_ALT 2.0
#define GPS_TOLERANCE_SPEED 1.0
#define GPS_TOLERANCE_COURSE 5.0
#define GPS_DEBOUNCE_MS 3000

#define LORA_BANDWIDTH 500.0
#define LORA_SPREADING_FACTOR 12
#define LORA_CODING_RATE 8
#define LORA_PREAMBLE_LENGTH 20
#define LORA_SYNC_WORD 0xF3
#define LORA_CRC true

// Safe AES block sizes based on spreading factor
#if LORA_SPREADING_FACTOR == 12
  #define AES_BLOCK_LEN 20     // 5 header + 15 payload
  #define FRAG_DATA_LEN 15
#elif LORA_SPREADING_FACTOR == 11
  #define AES_BLOCK_LEN 24     // 5 header + 19 payload
  #define FRAG_DATA_LEN 19
#elif LORA_SPREADING_FACTOR == 10
  #define AES_BLOCK_LEN 32     // 5 header + 27 payload
  #define FRAG_DATA_LEN 27
#elif LORA_SPREADING_FACTOR == 9
  #define AES_BLOCK_LEN 40     // 5 header + 35 payload
  #define FRAG_DATA_LEN 35
#elif LORA_SPREADING_FACTOR == 8
  #define AES_BLOCK_LEN 60     // 5 header + 55 payload
  #define FRAG_DATA_LEN 55
#elif LORA_SPREADING_FACTOR == 7
  #define AES_BLOCK_LEN 80     // 5 header + 75 payload
  #define FRAG_DATA_LEN 75
#else
  #error "Unsupported LORA_SPREADING_FACTOR value"
#endif

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
