#include "thermal/ThermalProcessor.h"

#include "board/DisplayDriver.h"

ThermalStats ThermalProcessor::calculateStats(const ThermalFrame& frame) const {
  ThermalStats stats;
  stats.minRaw = 0xFFFF;
  stats.maxRaw = 0;

  for (uint16_t y = 0; y < frame.height; ++y) {
    for (uint16_t x = 0; x < frame.width; ++x) {
      const uint16_t raw = frame.raw[y * frame.width + x];
      if (raw < stats.minRaw) {
        stats.minRaw = raw;
        stats.coldX = x;
        stats.coldY = y;
      }
      if (raw > stats.maxRaw) {
        stats.maxRaw = raw;
        stats.hotX = x;
        stats.hotY = y;
      }
    }
  }

  stats.centerRaw = frame.raw[(frame.height / 2) * frame.width + (frame.width / 2)];
  stats.minC = rawToCelsius(stats.minRaw);
  stats.maxC = rawToCelsius(stats.maxRaw);
  stats.centerC = rawToCelsius(stats.centerRaw);
  return stats;
}

bool ThermalProcessor::renderRgb565(const ThermalFrame& frame,
                                    PaletteMode palette,
                                    uint16_t* out,
                                    uint16_t outWidth,
                                    uint16_t outHeight,
                                    ThermalStats* stats) const {
  if (out == nullptr || outWidth == 0 || outHeight == 0) {
    return false;
  }

  ThermalStats localStats = calculateStats(frame);
  if (stats != nullptr) {
    *stats = localStats;
  }

  const uint16_t range = localStats.maxRaw > localStats.minRaw ? localStats.maxRaw - localStats.minRaw : 1;
  for (uint16_t y = 0; y < outHeight; ++y) {
    const uint16_t srcY = static_cast<uint32_t>(y) * frame.height / outHeight;
    for (uint16_t x = 0; x < outWidth; ++x) {
      const uint16_t srcX = static_cast<uint32_t>(x) * frame.width / outWidth;
      const uint16_t raw = frame.raw[srcY * frame.width + srcX];
      const uint8_t value = static_cast<uint32_t>(raw - localStats.minRaw) * 255 / range;
      out[y * outWidth + x] = paletteColor(value, palette);
    }
  }
  return true;
}

float ThermalProcessor::rawToCelsius(uint16_t raw) {
  return raw * 0.01f - 273.15f;
}

uint16_t ThermalProcessor::paletteColor(uint8_t value, PaletteMode palette) {
  switch (palette) {
    case PaletteMode::WhiteHot:
      return DisplayDriver::rgb565(value, value, value);
    case PaletteMode::BlackHot:
      return DisplayDriver::rgb565(255 - value, 255 - value, 255 - value);
    case PaletteMode::Histogram:
      if (value < 64) {
        return DisplayDriver::rgb565(0, value * 3, 180);
      }
      if (value < 128) {
        return DisplayDriver::rgb565(0, 180, (127 - value) * 3);
      }
      if (value < 192) {
        return DisplayDriver::rgb565((value - 128) * 4, 210, 0);
      }
      return DisplayDriver::rgb565(255, 255 - (value - 192) * 3, 0);
    case PaletteMode::Ironbow:
    default:
      if (value < 64) {
        return DisplayDriver::rgb565(value * 2, 0, value * 3);
      }
      if (value < 128) {
        return DisplayDriver::rgb565(128 + (value - 64) * 2, 0, 192 - (value - 64));
      }
      if (value < 192) {
        return DisplayDriver::rgb565(255, (value - 128) * 3, 0);
      }
      return DisplayDriver::rgb565(255, 192 + (value - 192), (value - 192) * 2);
  }
}
