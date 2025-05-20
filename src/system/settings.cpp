// src/system/settings.cpp
#include "settings.h"

#include <EEPROM.h>

Settings settings;

void loadSettings() {
  EEPROM.begin(128);
  if (EEPROM.read(0) != 0x42) {
    uint16_t cid = (uint16_t)((ESP.getEfuseMac() >> 32) & 0xFFFF);
    snprintf(settings.deviceName, sizeof(settings.deviceName), "%04X", cid);
    settings.frequency = 915.0;
    settings.txPower = 22;
    settings.maxRetries = 2;
    settings.retryInterval = 1000;
    settings.beaconInterval = 30000;
    settings.beaconEnabled = true;
    EEPROM.write(0, 0x42);
    EEPROM.put(4, settings);
    EEPROM.commit();
  } else {
    EEPROM.get(4, settings);
  }
}

void saveSettings() {
  EEPROM.put(4, settings);
  EEPROM.commit();
  ESP.restart();
}

void updateSetting(const char* key, const void* value) {
  if (strcmp(key, "deviceName") == 0) {
    strncpy(settings.deviceName, (const char*)value,
            sizeof(settings.deviceName) - 1);
    settings.deviceName[sizeof(settings.deviceName) - 1] = '\0';
  } else if (strcmp(key, "frequency") == 0) {
    settings.frequency = *(const float*)value;
  } else if (strcmp(key, "txPower") == 0) {
    int txPower = *(const int*)value;
    if (txPower > 0 && txPower < 23) {
      settings.txPower = txPower;
    }
    settings.txPower = *(const int*)value;
  } else if (strcmp(key, "maxRetries") == 0) {
    settings.maxRetries = *(const int*)value;
  } else if (strcmp(key, "retryInterval") == 0) {
    settings.retryInterval = *(const int*)value;
  } else if (strcmp(key, "beaconInterval") == 0) {
    settings.beaconInterval = *(const int*)value;
  } else if (strcmp(key, "beaconEnabled") == 0) {
    settings.beaconEnabled = *(const bool*)value;
  }
  saveSettings();
}
