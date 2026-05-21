#pragma once

#include <Arduino.h>

struct AppSettings {
  char savePath[64] = "/flir";
  char locale[16] = "en_AU";
  char dateFormat[16] = "YYYY-MM-DD";
  char timeFormat[16] = "HH:mm:ss";
  bool landscape = true;
};
