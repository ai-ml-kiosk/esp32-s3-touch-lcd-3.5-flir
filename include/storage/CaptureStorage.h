#pragma once

#include <Arduino.h>

#include "settings/AppSettings.h"

class CaptureStorage {
 public:
  bool begin();
  bool isMounted() const { return mounted_; }
  bool validateSavePath(const char* path) const;
  bool ensureSavePath(const char* path);
  bool saveCapture(const AppSettings& settings,
                   const uint16_t* raw14,
                   size_t rawPixelCount,
                   const uint16_t* rgb565,
                   uint16_t width,
                   uint16_t height,
                   char* savedBasePath = nullptr,
                   size_t savedBasePathSize = 0);

 private:
  bool writeRaw(const char* path, const uint16_t* raw14, size_t pixelCount);
  bool writeBmp24(const char* path, const uint16_t* rgb565, uint16_t width, uint16_t height);
  void nextBasePath(const char* dir, char* out, size_t outSize);

  bool mounted_ = false;
  uint32_t sequence_ = 1;
};
