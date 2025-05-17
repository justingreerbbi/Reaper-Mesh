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
}
