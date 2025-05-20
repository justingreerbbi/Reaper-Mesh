#pragma once
#include <TinyGPSPlus.h>

extern TinyGPSPlus gps;

struct ReaperGPSData {
  double latitude;
  double longitude;
  double altitude;
  double speed;
  double course;
  int satellites;
  bool valid;
};

void initGPS();
void updateGPS();
bool gpsDataChanged();
void printGPSDataIfChanged();
ReaperGPSData getGPSData();
