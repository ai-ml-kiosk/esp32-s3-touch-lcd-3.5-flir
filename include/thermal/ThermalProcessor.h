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
                    bool landscape = true) const;

 private:
  static float rawToCelsius(uint16_t raw);
  static uint16_t paletteColor(uint8_t value, PaletteMode palette);
};
