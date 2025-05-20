#include "gps.h"

#include <HardwareSerial.h>

#include "../config.h"

TinyGPSPlus gps;
HardwareSerial GPSSerial(2);

static double lastLat = 0;
static double lastLon = 0;
static double lastAlt = 0;
static double lastSpd = 0;
static double lastCourse = 0;
static int lastSats = 0;
static unsigned long lastGPSSend = 0;

void initGPS() {
  GPSSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void updateGPS() {
  while (GPSSerial.available()) {
    gps.encode(GPSSerial.read());
  }
}

bool gpsDataChanged() {
  double lat = gps.location.lat();
  double lon = gps.location.lng();
  double alt = gps.altitude.meters();
  double spd = gps.speed.kmph();
  double crs = gps.course.deg();
  int sats = gps.satellites.value();
  unsigned long now = millis();

  bool changed = fabs(lat - lastLat) > GPS_TOLERANCE_LATLON ||
                 fabs(lon - lastLon) > GPS_TOLERANCE_LATLON ||
                 fabs(alt - lastAlt) > GPS_TOLERANCE_ALT ||
                 fabs(spd - lastSpd) > GPS_TOLERANCE_SPEED ||
                 fabs(crs - lastCourse) > GPS_TOLERANCE_COURSE ||
                 sats != lastSats;

  if (changed && (now - lastGPSSend >= GPS_DEBOUNCE_MS)) {
    lastLat = lat;
    lastLon = lon;
    lastAlt = alt;
    lastSpd = spd;
    lastCourse = crs;
    lastSats = sats;
    lastGPSSend = now;
    return true;
  }
  return false;
}

void printGPSDataIfChanged() {
  if (gps.location.isValid() && gpsDataChanged()) {
    Serial.printf("GPS|%.6f,%.6f,%.1f,%.1f,%.1f,%d\n", gps.location.lat(),
                  gps.location.lng(), gps.altitude.meters(), gps.speed.kmph(),
                  gps.course.deg(), gps.satellites.value());
  }
}

ReaperGPSData getGPSData() {
  ReaperGPSData data;

  if (!gps.location.isValid()) {
    data.latitude = 0;
    data.longitude = 0;
    data.altitude = 0;
    data.speed = 0;
    data.course = 0;
    data.satellites = 0;
    return data;
  }

  data.latitude = gps.location.lat();
  data.longitude = gps.location.lng();
  data.altitude = gps.altitude.meters();
  data.speed = gps.speed.kmph();
  data.course = gps.course.deg();
  data.satellites = gps.satellites.value();
  return data;
}
