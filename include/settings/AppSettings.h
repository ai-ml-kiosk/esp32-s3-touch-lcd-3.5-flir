#pragma once

#include <Arduino.h>

struct AppSettings {
  char savePath[64] = "/flir";
  char locale[16] = "en_AU";
  char dateFormat[16] = "YYYY-MM-DD";
  char timeFormat[16] = "HH:mm:ss";
  bool landscape = true;
  uint8_t displayRotation = 1;
  bool autoRotate = true;
  uint16_t inactivitySleepSeconds = 0;
  int8_t temperatureOffsetTenths = 0;
  bool includeFilenameInCapture = true;
  bool saveRawCapture = true;
  bool showHotColdDetails = true;
  bool showCenterTemperature = true;
  uint8_t imageQualityMode = 1;
  bool autoFfcEnabled = true;
  uint8_t clipDurationSeconds = 3;
  uint8_t paletteMode = 0;
  bool zoomed = false;
  bool soundEnabled = true;
  uint8_t soundVolume = 80;
};
