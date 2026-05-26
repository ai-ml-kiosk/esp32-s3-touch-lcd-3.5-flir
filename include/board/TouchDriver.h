#pragma once

#include <Arduino.h>

struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
  bool pressed = false;
};

class TouchDriver {
 public:
  bool begin(bool landscape, uint16_t width, uint16_t height);
  void setOrientation(bool landscape, uint16_t width, uint16_t height);
  bool read(TouchPoint& point);

 private:
  bool ready_ = false;
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  uint32_t lastReadMs_ = 0;
};
