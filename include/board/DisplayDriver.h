#pragma once

#include <Arduino.h>

enum class DisplayPixelFormat {
  Rgb565,
  Rgb666,
};

struct DisplayInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  DisplayPixelFormat pixelFormat = DisplayPixelFormat::Rgb565;
};

class DisplayDriver {
 public:
  bool begin(bool landscape);
  void setLandscape(bool landscape);
  bool isReady() const { return ready_; }
  bool isLandscape() const { return landscape_; }
  DisplayInfo info() const;
  void setBacklight(bool enabled);
  bool backlightEnabled() const { return backlightEnabled_; }

  void fillScreen(uint16_t color);
  void fillRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t color);
  void drawRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t color);
  void fillRoundRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t radius, uint16_t color);
  void drawRoundRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t radius, uint16_t color);
  void drawText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size = 1);
  void drawTextRight(int16_t right, int16_t y, const char* text, uint16_t color, uint8_t size = 1);
  bool drawBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* pixels);
  void flush();

  static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

 private:
  bool initializePanel();
  void applyOrientation();

  bool ready_ = false;
  bool landscape_ = true;
  bool backlightEnabled_ = false;
};
