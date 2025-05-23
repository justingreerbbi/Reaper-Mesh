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

      if (in.startsWith("AT+DEVICE?")) {
        Serial.println("REAPER_NODE|READY|" + String(settings.deviceName));
        continue;
      }

      // AT+MSG=<message>
      if (in.startsWith("AT+MSG=")) {
        String msg = in.substring(7);
        if (isTransmitting) continue;
        isTransmitting = true;

        msg = "MSG|" + String(settings.deviceName) + "|" + msg;
        String msgId = generateMsgID();
        std::vector<Fragment> frags;
        int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

        for (int i = 0; i < total; i++) {
          Fragment frag = {};
          frag.data[0] = TYPE_TEXT_FRAGMENT;
          frag.data[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
          frag.data[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
          frag.data[3] = i;
          frag.data[4] = total;

          String chunk = msg.substring(i * FRAG_DATA_LEN, min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
          memcpy(&frag.data[5], chunk.c_str(), chunk.length());

          frag.length = chunk.length() + 5;
          encryptFragment(frag.data, frag.length);
          frag.retries = 0;
          frag.timestamp = millis();
          frag.acked = false;
          frags.push_back(frag);
        }

        outgoing[msgId] = frags;
        isTransmitting = false;
      }

      // AT+DMSG=<to_device>|<message>
      if (in.startsWith("AT+DMSG=")) {
        String msg = in.substring(8);
        if (isTransmitting) continue;
        isTransmitting = true;

        msg = "DMSG|" + String(settings.deviceName) + "|" + msg;
        String msgId = generateMsgID();
        std::vector<Fragment> frags;
        int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

        for (int i = 0; i < total; i++) {
          Fragment frag = {};
          frag.data[0] = TYPE_TEXT_FRAGMENT;
          frag.data[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
          frag.data[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
          frag.data[3] = i;
          frag.data[4] = total;

          String chunk = msg.substring(i * FRAG_DATA_LEN, min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
          memcpy(&frag.data[5], chunk.c_str(), chunk.length());

          frag.length = chunk.length() + 5;
          encryptFragment(frag.data, frag.length);
          frag.retries = 0;
          frag.timestamp = millis();
          frag.acked = false;
          frags.push_back(frag);
        }

        outgoing[msgId] = frags;
        isTransmitting = false;
      }

      // AT+GPS?
      if (in.startsWith("AT+GPS?")) {
        if (isTransmitting) continue;
        ReaperGPSData data = getGPSData();
        Serial.printf("GPS|%.6f,%.6f,%.1f,%.1f,%.1f,%d\n",
                      data.latitude, data.longitude,
                      data.altitude, data.speed,
                      data.course, data.satellites);
        isTransmitting = false;
      }

      // AT+BEACON
      if (in.startsWith("AT+BEACON")) {
        if (isTransmitting) continue;
        isTransmitting = true;
        sendBeacon();
        isTransmitting = false;
      }
    }

    unsigned long now = millis();
    if (!startupBeaconSent) {
      // sendBeacon();
      startupBeaconSent = true;
      lastBeacon = now;
    } else if (now - lastBeacon >= settings.beaconInterval && !isTransmitting) {
      // sendBeacon();
      lastBeacon = now;
    }

    updateGPS();

    if (!isTransmitting) {
      printGPSDataIfChanged();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
