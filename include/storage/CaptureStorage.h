#pragma once

#include <Arduino.h>
#include <FS.h>

#include "settings/AppSettings.h"
#include "thermal/ThermalFrame.h"

class CaptureStorage {
 public:
  bool begin();
  bool recover();
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
  bool beginThermalClip(const AppSettings& settings, char* savedBasePath = nullptr, size_t savedBasePathSize = 0);
  bool appendThermalClipFrame(const uint16_t* raw14, size_t rawPixelCount);
  bool finishThermalClip();
  bool clipActive() const { return clipActive_; }
  const char* lastCaptureBasePath() const { return lastBasePath_; }
  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  uint64_t freeBytes() const;
  bool deleteLastCapture();
  uint16_t captureCount(const char* dir) const;
  bool captureBaseAt(const char* dir, uint16_t index, char* out, size_t outSize) const;
  bool latestCaptureBase(const char* dir, char* out, size_t outSize) const;
  uint16_t clipCount(const char* dir) const;
  bool clipBaseAt(const char* dir, uint16_t index, char* out, size_t outSize) const;
  bool latestClipBase(const char* dir, char* out, size_t outSize) const;
  bool deleteCapture(const char* basePath);
  bool deleteThermalClip(const char* basePath);
  bool loadThermalClipFrame(const char* basePath, uint32_t frameIndex, ThermalFrame& frame, uint32_t* frameCount = nullptr) const;
  bool openThermalClipPlayback(const char* basePath, uint32_t* frameCount = nullptr);
  bool readNextThermalClipPlaybackFrame(ThermalFrame& frame, uint32_t* frameIndex = nullptr, uint32_t* frameCount = nullptr);
  void closeThermalClipPlayback();
  bool loadCaptureBmp(const char* basePath, uint16_t* out, uint16_t outWidth, uint16_t outHeight) const;
  bool loadCaptureBmpScaled(const char* basePath, uint16_t* out, uint16_t outWidth, uint16_t outHeight) const;

 private:
  bool mountCard();
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
  bool clipActive_ = false;
  uint32_t clipFrameCount_ = 0;
  File clipFile_;
  File playbackClipFile_;
  uint32_t playbackClipFrameCount_ = 0;
  uint32_t playbackClipNextFrame_ = 0;
  char playbackClipBasePath_[96] = {};
  uint32_t sequence_ = 1;
  char lastBasePath_[96] = {};
};
