// src/system/settings.h
#pragma once
#include "../config.h"

void loadSettings();
void saveSettings();
void updateSetting(const char* key, const void* value);