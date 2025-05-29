#include "app.h"
#include "lora.h"
#include "../gps/gps.h"
#include "../config.h"

bool startupBeaconSent = false;

void taskAppHandler(void* param) {
  unsigned long lastBeacon = 0;

  while (true) {
    if (Serial.available()) {
      String in = Serial.readStringUntil('\n');
      in.trim();

      if (in.startsWith("AT+DEVICE?")) {
        Serial.println("REAPER_NODE|READY|" + String(settings.deviceName));
        continue;  // Break
      }

      // Send a group message. MSG|<device_name>|<message>
      if (in.startsWith("AT+MSG=")) {
        String msg = in.substring(7);
        msg = "MSG|" + String(settings.deviceName) + "|" + msg;
        processMessageToOutgoing(msg);
      }

      // Send a direct message to a node
      // DMSG|<device_name>|<to_device_name>|<message>|<msgID>
      if (in.startsWith("AT+DMSG=")) {
        String msg = in.substring(8);
        msg = "DMSG|" + String(settings.deviceName) + "|" + msg;
        processMessageToOutgoing(msg);
      }

      if (in.startsWith("AT+GPS?")) {
        ReaperGPSData data = getGPSData();
        Serial.printf("GPS|%.6f,%.6f,%.1f,%.1f,%.1f,%d\n", data.latitude,
                      data.longitude, data.altitude, data.speed, data.course,
                      data.satellites);
      }

      if (in.startsWith("AT+BEACON")) {
        sendBeacon();
      }

    }  // END OF INCOMING STATEMENT

    unsigned long now = millis();
    if (!startupBeaconSent) {
      // sendBeacon();
      // Serial.println("LOG|BEACON_SENT");
      startupBeaconSent = true;
      lastBeacon = now;
    } else if (now - lastBeacon >= settings.beaconInterval) {
      // Serial.println("LOG|BEACON_SENT");
      // sendBeacon();
      lastBeacon = now;
    }

    updateGPS();

    // Report the gps if the device is not transmitting.
    //if (!isTransmitting) {
      // printGPSDataIfChanged();
    //}

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}
