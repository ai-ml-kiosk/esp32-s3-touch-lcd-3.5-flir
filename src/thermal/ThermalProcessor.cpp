#include "thermal/ThermalProcessor.h"

#include "board/DisplayDriver.h"

namespace {

constexpr uint16_t kZoomDivisor = 2;

uint16_t transformedWidth(const ThermalFrame& frame, bool landscape) {
  return landscape ? frame.width : frame.height;
}

uint16_t transformedHeight(const ThermalFrame& frame, bool landscape) {
  return landscape ? frame.height : frame.width;
}

uint16_t sourceXFromTransformed(const ThermalFrame& frame, uint16_t logicalX, uint16_t logicalY, bool landscape) {
  if (landscape) {
    return frame.width - 1 - logicalX;
  }
  return frame.width - 1 - logicalY;
}

uint16_t sourceYFromTransformed(const ThermalFrame& frame, uint16_t logicalX, uint16_t logicalY, bool landscape) {
  if (landscape) {
    return frame.height - 1 - logicalY;
  }
  return logicalX;
}

}  // namespace

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
                                    ThermalStats* stats,
                                    bool zoomed,
                                    bool landscape) const {
  if (out == nullptr || outWidth == 0 || outHeight == 0) {
    return false;
  }

  const ThermalStats fullFrameStats = calculateStats(frame);
  const uint16_t paletteMinRaw = fullFrameStats.minRaw;
  const uint16_t paletteMaxRaw = fullFrameStats.maxRaw;

  const uint16_t viewW = transformedWidth(frame, landscape);
  const uint16_t viewH = transformedHeight(frame, landscape);
  const uint16_t cropW = zoomed ? viewW / kZoomDivisor : viewW;
  const uint16_t cropH = zoomed ? viewH / kZoomDivisor : viewH;
  const uint16_t cropX0 = (viewW - cropW) / 2;
  const uint16_t cropY0 = (viewH - cropH) / 2;

  ThermalStats localStats;
  localStats.minRaw = 0xFFFF;
  localStats.maxRaw = 0;
  localStats.markerWidth = viewW;
  localStats.markerHeight = viewH;
  for (uint16_t logicalY = cropY0; logicalY < cropY0 + cropH; ++logicalY) {
    for (uint16_t logicalX = cropX0; logicalX < cropX0 + cropW; ++logicalX) {
      const uint16_t srcX = sourceXFromTransformed(frame, logicalX, logicalY, landscape);
      const uint16_t srcY = sourceYFromTransformed(frame, logicalX, logicalY, landscape);
      const uint16_t raw = frame.raw[srcY * frame.width + srcX];
      if (raw < localStats.minRaw) {
        localStats.minRaw = raw;
        localStats.coldX = static_cast<uint32_t>(logicalX - cropX0) * viewW / cropW;
        localStats.coldY = static_cast<uint32_t>(logicalY - cropY0) * viewH / cropH;
      }
      if (raw > localStats.maxRaw) {
        localStats.maxRaw = raw;
        localStats.hotX = static_cast<uint32_t>(logicalX - cropX0) * viewW / cropW;
        localStats.hotY = static_cast<uint32_t>(logicalY - cropY0) * viewH / cropH;
      }
    }
  }
  const uint16_t centerLogicalX = cropX0 + cropW / 2;
  const uint16_t centerLogicalY = cropY0 + cropH / 2;
  const uint16_t centerX = sourceXFromTransformed(frame, centerLogicalX, centerLogicalY, landscape);
  const uint16_t centerY = sourceYFromTransformed(frame, centerLogicalX, centerLogicalY, landscape);
  localStats.centerRaw = frame.raw[centerY * frame.width + centerX];
  localStats.minC = rawToCelsius(localStats.minRaw);
  localStats.maxC = rawToCelsius(localStats.maxRaw);
  localStats.centerC = rawToCelsius(localStats.centerRaw);
  if (stats != nullptr) {
    *stats = localStats;
  }

  const uint16_t range = paletteMaxRaw > paletteMinRaw ? paletteMaxRaw - paletteMinRaw : 1;
  for (uint16_t y = 0; y < outHeight; ++y) {
    const uint16_t logicalY = cropY0 + static_cast<uint32_t>(y) * cropH / outHeight;
    for (uint16_t x = 0; x < outWidth; ++x) {
      const uint16_t logicalX = cropX0 + static_cast<uint32_t>(x) * cropW / outWidth;
      const uint16_t srcX = sourceXFromTransformed(frame, logicalX, logicalY, landscape);
      const uint16_t srcY = sourceYFromTransformed(frame, logicalX, logicalY, landscape);
      const uint16_t raw = frame.raw[srcY * frame.width + srcX];
      const uint8_t value = raw <= paletteMinRaw
                                ? 0
                                : (raw >= paletteMaxRaw
                                       ? 255
                                       : static_cast<uint32_t>(raw - paletteMinRaw) * 255 / range);
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
