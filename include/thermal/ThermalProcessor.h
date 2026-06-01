#pragma once

#include "thermal/ThermalFrame.h"

class ThermalProcessor {
 public:
  ThermalStats calculateStats(const ThermalFrame& frame) const;
  bool renderRgb565(const ThermalFrame& frame,
                    PaletteMode palette,
                    uint16_t* out,
                    uint16_t outWidth,
                    uint16_t outHeight,
                    ThermalStats* stats,
                    bool zoomed = false,
                    bool landscape = true,
                    uint8_t displayRotation = 1,
                    uint8_t imageQualityMode = 1);

 private:
  void prepareFilteredFrame(const ThermalFrame& frame, uint8_t imageQualityMode);
  uint16_t filteredRawAt(const ThermalFrame& frame, uint16_t x, uint16_t y) const;
  uint16_t filteredRawAtLogical(const ThermalFrame& frame, uint16_t logicalX, uint16_t logicalY, uint8_t displayRotation) const;
  uint16_t bilinearRawAtLogical(const ThermalFrame& frame, uint32_t logicalX256, uint32_t logicalY256, uint8_t displayRotation) const;
  void calculatePercentileRange(const ThermalFrame& frame,
                                uint16_t cropX0,
                                uint16_t cropY0,
                                uint16_t cropW,
                                uint16_t cropH,
                                uint8_t displayRotation,
                                uint16_t rawMin,
                                uint16_t rawMax,
                                uint8_t lowPercent,
                                uint8_t highPercent,
                                uint16_t* low,
                                uint16_t* high) const;
  bool detectMotion(const ThermalFrame& frame) const;
  static float rawToCelsius(uint16_t raw);
  static uint16_t paletteColor(uint8_t value, PaletteMode palette);

  mutable uint16_t filteredRaw_[kLeptonPixelCount] = {};
  uint16_t previousRaw_[kLeptonPixelCount] = {};
  uint16_t scratchRaw_[kLeptonPixelCount] = {};
  bool hasPreviousRaw_ = false;
  bool hasPaletteRange_ = false;
  uint16_t smoothedRangeLow_ = 0;
  uint16_t smoothedRangeHigh_ = 0;
};
