#include "task_app.h"

#include "../comms/lora.h"        // queueMessage / sendBeacon
#include "../gps/gps.h"
#include "../system/settings.h"

extern bool isTransmitting;
static bool startupBeaconSent = false;

void taskAppHandler(void* /*param*/) {
  uint32_t lastBeacon = 0;

  while (true) {
    /* ───────────── UART COMMANDS ───────────── */
    if (Serial.available()) {
      String in = Serial.readStringUntil('\n');
      in.trim();

      /* Device prompt */
      if (in.startsWith("AT+DEVICE?")) {
        Serial.println("NODE|READY|" + String(settings.deviceName));
        continue;
      }

      /* Group message ------------------------------------------------------- */
      if (in.startsWith("AT+MSG=")) {
        String text = in.substring(7);
        queueMessage("MSG", text);          // << use high-level API
        continue;
      }

      /* Direct message ------------------------------------------------------ */
      if (in.startsWith("AT+DMSG=")) {
        String remainder = in.substring(8); // "<to>|<msg>"
        queueMessage("DMSG", remainder);
        continue;
      }

      /* GPS request --------------------------------------------------------- */
      if (in == "AT+GPS?") {
        ReaperGPSData d = getGPSData();
        Serial.printf("GPS|%.6f,%.6f,%.1f,%.1f,%.1f,%d\n",
                      d.latitude, d.longitude, d.altitude,
                      d.speed, d.course, d.satellites);
        continue;
      }

      /* Manual beacon ------------------------------------------------------- */
      if (in.startsWith("AT+BEACON")) {
        sendBeacon();
        continue;
      }
    }

    /* ───────────── periodic work ───────────── */
    uint32_t now = millis();

    if (!startupBeaconSent) {
      // optional initial beacon
      // sendBeacon();
      startupBeaconSent = true;
      lastBeacon = now;
    }
    else if (settings.beaconEnabled &&
             now - lastBeacon >= settings.beaconInterval) {
      //sendBeacon();
      lastBeacon = now;
    }

    updateGPS();
    //printGPSDataIfChanged();

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}
