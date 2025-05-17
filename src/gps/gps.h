#pragma once
#include <TinyGPSPlus.h>

extern TinyGPSPlus gps;

void initGPS();
void updateGPS();
bool gpsDataChanged();
void printGPSDataIfChanged();
