#pragma once

#include <Arduino.h>

#include "settings/AppSettings.h"
#include "thermal/ThermalFrame.h"

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
                   const ThermalStats& stats,
                   char* savedBasePath = nullptr,
                   size_t savedBasePathSize = 0);
  const char* lastCaptureBasePath() const { return lastBasePath_; }
  bool deleteLastCapture();
  uint16_t captureCount(const char* dir) const;
  bool captureBaseAt(const char* dir, uint16_t index, char* out, size_t outSize) const;
  bool deleteCapture(const char* basePath);
  bool loadCaptureBmp(const char* basePath, uint16_t* out, uint16_t outWidth, uint16_t outHeight) const;
  bool loadCaptureBmpScaled(const char* basePath, uint16_t* out, uint16_t outWidth, uint16_t outHeight) const;

 private:
  bool writeRaw(const char* path, const uint16_t* raw14, size_t pixelCount);
  bool writeBmp24(const char* path,
                  const uint16_t* rgb565,
                  uint16_t width,
                  uint16_t height,
                  const ThermalStats& stats,
                  const AppSettings& settings,
                  const char* basePath);
  void nextBasePath(const char* dir, char* out, size_t outSize);

  bool mounted_ = false;
  uint32_t sequence_ = 1;
  char lastBasePath_[96] = {};
};
