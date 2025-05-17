#include "task_app.h"

#include "../comms/lora.h"
#include "../gps/gps.h"
#include "../system/settings.h"

extern bool isTransmitting;
bool startupBeaconSent = false;

void taskAppHandler(void* param) {
  unsigned long lastBeacon = 0;

  while (true) {
    if (Serial.available()) {
      String in = Serial.readStringUntil('\n');
      in.trim();

      if (in.startsWith("AT+MSG=")) {
        String msg = in.substring(7);
        if (isTransmitting) continue;
        isTransmitting = true;

        msg = String(settings.deviceName) + "|" + msg;
        String msgId = generateMsgID();
        std::vector<Fragment> frags;
        int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

        for (int i = 0; i < total; i++) {
          uint8_t block[AES_BLOCK_LEN] = {0};
          block[0] = PRIORITY_NORMAL;
          block[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
          block[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
          block[3] = i;
          block[4] = total;

          String chunk =
              msg.substring(i * FRAG_DATA_LEN,
                            min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
          memcpy(&block[5], chunk.c_str(), chunk.length());

          encryptFragment(block);
          Fragment frag;
          memcpy(frag.data, block, AES_BLOCK_LEN);
          frag.retries = 0;
          frag.timestamp = millis();
          frag.acked = false;
          frags.push_back(frag);
          lora.transmit(block, AES_BLOCK_LEN);
        }

        outgoing[msgId] = frags;
        isTransmitting = false;
      }
    }

    unsigned long now = millis();

    if (!startupBeaconSent) {
      Serial.println("LOG|BEACON_SENT");
      startupBeaconSent = true;
      lastBeacon = now;
    } else if (now - lastBeacon >= settings.beaconInterval && !isTransmitting) {
      Serial.println("LOG|BEACON_SENT");
      lastBeacon = now;
    }

    updateGPS();
    printGPSDataIfChanged();

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
