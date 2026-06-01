#include "thermal/ThermalProcessor.h"

#include <cstring>

#include "board/DisplayDriver.h"

namespace {

constexpr uint16_t kZoomDivisor = 2;
constexpr uint16_t kOutlierThresholdRaw = 90;
constexpr uint16_t kMotionAverageDeltaRaw = 45;
constexpr uint16_t kHistogramBins = 128;

uint16_t transformedWidth(const ThermalFrame& frame, uint8_t displayRotation) {
  return (displayRotation == 1 || displayRotation == 3) ? frame.width : frame.height;
}

uint16_t transformedHeight(const ThermalFrame& frame, uint8_t displayRotation) {
  return (displayRotation == 1 || displayRotation == 3) ? frame.height : frame.width;
}

uint16_t sourceXFromTransformed(const ThermalFrame& frame, uint16_t logicalX, uint16_t logicalY, uint8_t displayRotation) {
  switch (displayRotation & 0x03) {
    case 0:
      return logicalY;
    case 1:
      return frame.width - 1 - logicalX;
    case 2:
      return frame.width - 1 - logicalY;
    case 3:
    default:
      return logicalX;
  }
}

uint16_t sourceYFromTransformed(const ThermalFrame& frame, uint16_t logicalX, uint16_t logicalY, uint8_t displayRotation) {
  switch (displayRotation & 0x03) {
    case 0:
      return frame.height - 1 - logicalX;
    case 1:
      return frame.height - 1 - logicalY;
    case 2:
      return logicalX;
    case 3:
    default:
      return logicalY;
  }
}

uint16_t clampU16(int32_t value, uint16_t low, uint16_t high) {
  if (value < static_cast<int32_t>(low)) {
    return low;
  }
  if (value > static_cast<int32_t>(high)) {
    return high;
  }
  return static_cast<uint16_t>(value);
}

uint16_t median9(uint16_t values[9]) {
  for (uint8_t i = 1; i < 9; ++i) {
    const uint16_t key = values[i];
    int8_t j = static_cast<int8_t>(i) - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
  return values[4];
}

uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
  return static_cast<uint16_t>(a) * (255 - t) / 255 + static_cast<uint16_t>(b) * t / 255;
}

uint16_t gradientColor(uint8_t value,
                       uint8_t r0,
                       uint8_t g0,
                       uint8_t b0,
                       uint8_t r1,
                       uint8_t g1,
                       uint8_t b1,
                       uint8_t r2,
                       uint8_t g2,
                       uint8_t b2,
                       uint8_t r3,
                       uint8_t g3,
                       uint8_t b3) {
  const uint8_t segment = value / 85;
  const uint8_t t = (value % 85) * 3;
  switch (segment) {
    case 0:
      return DisplayDriver::rgb565(lerp8(r0, r1, t), lerp8(g0, g1, t), lerp8(b0, b1, t));
    case 1:
      return DisplayDriver::rgb565(lerp8(r1, r2, t), lerp8(g1, g2, t), lerp8(b1, b2, t));
    default:
      return DisplayDriver::rgb565(lerp8(r2, r3, t), lerp8(g2, g3, t), lerp8(b2, b3, t));
  }
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
                                    bool landscape,
                                    uint8_t displayRotation,
                                    uint8_t imageQualityMode) {
  if (out == nullptr || outWidth == 0 || outHeight == 0) {
    return false;
  }
  (void)landscape;
  displayRotation &= 0x03;
  const uint8_t thermalRotation = displayRotation;
  if (imageQualityMode > 2) {
    imageQualityMode = 1;
  }

  prepareFilteredFrame(frame, imageQualityMode);

  const uint16_t viewW = transformedWidth(frame, thermalRotation);
  const uint16_t viewH = transformedHeight(frame, thermalRotation);
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
      const uint16_t raw = filteredRawAtLogical(frame, logicalX, logicalY, thermalRotation);
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
  localStats.centerRaw = filteredRawAtLogical(frame, centerLogicalX, centerLogicalY, thermalRotation);
  localStats.hotX = viewW - 1 - localStats.hotX;
  localStats.hotY = viewH - 1 - localStats.hotY;
  localStats.coldX = viewW - 1 - localStats.coldX;
  localStats.coldY = viewH - 1 - localStats.coldY;
  localStats.minC = rawToCelsius(localStats.minRaw);
  localStats.maxC = rawToCelsius(localStats.maxRaw);
  localStats.centerC = rawToCelsius(localStats.centerRaw);
  if (stats != nullptr) {
    *stats = localStats;
  }

  uint16_t paletteMinRaw = localStats.minRaw;
  uint16_t paletteMaxRaw = localStats.maxRaw;
  const uint8_t lowPercent = imageQualityMode == 2 ? 3 : 2;
  const uint8_t highPercent = imageQualityMode == 2 ? 97 : 98;
  calculatePercentileRange(frame,
                           cropX0,
                           cropY0,
                           cropW,
                           cropH,
                           thermalRotation,
                           localStats.minRaw,
                           localStats.maxRaw,
                           lowPercent,
                           highPercent,
                           &paletteMinRaw,
                           &paletteMaxRaw);
  if (!hasPaletteRange_) {
    smoothedRangeLow_ = paletteMinRaw;
    smoothedRangeHigh_ = paletteMaxRaw;
    hasPaletteRange_ = true;
  } else {
    const uint8_t newWeight = imageQualityMode == 0 ? 192 : (imageQualityMode == 1 ? 144 : 72);
    smoothedRangeLow_ = static_cast<uint32_t>(smoothedRangeLow_) * (256 - newWeight) / 256 +
                        static_cast<uint32_t>(paletteMinRaw) * newWeight / 256;
    smoothedRangeHigh_ = static_cast<uint32_t>(smoothedRangeHigh_) * (256 - newWeight) / 256 +
                         static_cast<uint32_t>(paletteMaxRaw) * newWeight / 256;
  }
  paletteMinRaw = smoothedRangeLow_;
  paletteMaxRaw = smoothedRangeHigh_;
  if (paletteMaxRaw <= paletteMinRaw + 4) {
    paletteMaxRaw = paletteMinRaw + 4;
  }

  const uint16_t range = paletteMaxRaw - paletteMinRaw;
  for (uint16_t y = 0; y < outHeight; ++y) {
    const uint16_t sampleY = outHeight - 1 - y;
    const uint32_t logicalY256 = (static_cast<uint32_t>(cropY0) << 8) +
                                 static_cast<uint32_t>(sampleY) * cropH * 256 / outHeight;
    for (uint16_t x = 0; x < outWidth; ++x) {
      const uint16_t sampleX = outWidth - 1 - x;
      const uint32_t logicalX256 = (static_cast<uint32_t>(cropX0) << 8) +
                                   static_cast<uint32_t>(sampleX) * cropW * 256 / outWidth;
      const uint16_t raw = imageQualityMode == 0
                               ? filteredRawAtLogical(frame,
                                                      static_cast<uint16_t>(logicalX256 >> 8),
                                                      static_cast<uint16_t>(logicalY256 >> 8),
                                                      thermalRotation)
                               : bilinearRawAtLogical(frame, logicalX256, logicalY256, thermalRotation);
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

void ThermalProcessor::prepareFilteredFrame(const ThermalFrame& frame, uint8_t imageQualityMode) {
  const bool moving = detectMotion(frame);
  const uint8_t temporalNewWeight = moving || imageQualityMode == 0 ? 256 : (imageQualityMode == 1 ? 224 : 144);

  for (uint16_t y = 0; y < frame.height; ++y) {
    for (uint16_t x = 0; x < frame.width; ++x) {
      const uint16_t idx = y * frame.width + x;
      const uint16_t raw = frame.raw[idx];
      uint16_t filtered = raw;
      if (hasPreviousRaw_ && temporalNewWeight < 256) {
        filtered = static_cast<uint32_t>(previousRaw_[idx]) * (256 - temporalNewWeight) / 256 +
                   static_cast<uint32_t>(raw) * temporalNewWeight / 256;
      }
      scratchRaw_[idx] = filtered;
      previousRaw_[idx] = raw;
    }
  }
  hasPreviousRaw_ = true;

  if (imageQualityMode == 0) {
    memcpy(filteredRaw_, scratchRaw_, sizeof(filteredRaw_));
    return;
  }

  for (uint16_t y = 0; y < frame.height; ++y) {
    for (uint16_t x = 0; x < frame.width; ++x) {
      uint16_t values[9];
      uint8_t valueIndex = 0;
      for (int8_t dy = -1; dy <= 1; ++dy) {
        const uint16_t yy = clampU16(static_cast<int32_t>(y) + dy, 0, frame.height - 1);
        for (int8_t dx = -1; dx <= 1; ++dx) {
          const uint16_t xx = clampU16(static_cast<int32_t>(x) + dx, 0, frame.width - 1);
          values[valueIndex++] = scratchRaw_[yy * frame.width + xx];
        }
      }
      const uint16_t median = median9(values);
      const uint16_t idx = y * frame.width + x;
      if (imageQualityMode == 2 && !moving) {
        filteredRaw_[idx] = median;
      } else {
        const uint16_t center = scratchRaw_[idx];
        const uint16_t delta = center > median ? center - median : median - center;
        filteredRaw_[idx] = delta > kOutlierThresholdRaw ? median : center;
      }
    }
  }
}

bool ThermalProcessor::detectMotion(const ThermalFrame& frame) const {
  if (!hasPreviousRaw_) {
    return false;
  }
  uint32_t totalDelta = 0;
  uint16_t samples = 0;
  for (uint16_t y = 2; y < frame.height; y += 5) {
    for (uint16_t x = 2; x < frame.width; x += 5) {
      const uint16_t idx = y * frame.width + x;
      const uint16_t current = frame.raw[idx];
      const uint16_t previous = previousRaw_[idx];
      totalDelta += current > previous ? current - previous : previous - current;
      ++samples;
    }
  }
  return samples > 0 && (totalDelta / samples) > kMotionAverageDeltaRaw;
}

uint16_t ThermalProcessor::filteredRawAt(const ThermalFrame& frame, uint16_t x, uint16_t y) const {
  if (x >= frame.width) {
    x = frame.width - 1;
  }
  if (y >= frame.height) {
    y = frame.height - 1;
  }
  return filteredRaw_[y * frame.width + x];
}

uint16_t ThermalProcessor::filteredRawAtLogical(const ThermalFrame& frame,
                                                uint16_t logicalX,
                                                uint16_t logicalY,
                                                uint8_t displayRotation) const {
  const uint16_t srcX = sourceXFromTransformed(frame, logicalX, logicalY, displayRotation);
  const uint16_t srcY = sourceYFromTransformed(frame, logicalX, logicalY, displayRotation);
  return filteredRawAt(frame, srcX, srcY);
}

uint16_t ThermalProcessor::bilinearRawAtLogical(const ThermalFrame& frame,
                                                uint32_t logicalX256,
                                                uint32_t logicalY256,
                                                uint8_t displayRotation) const {
  const uint16_t viewW = transformedWidth(frame, displayRotation);
  const uint16_t viewH = transformedHeight(frame, displayRotation);
  uint16_t x0 = logicalX256 >> 8;
  uint16_t y0 = logicalY256 >> 8;
  if (x0 >= viewW) {
    x0 = viewW - 1;
  }
  if (y0 >= viewH) {
    y0 = viewH - 1;
  }
  const uint16_t x1 = x0 + 1 < viewW ? x0 + 1 : x0;
  const uint16_t y1 = y0 + 1 < viewH ? y0 + 1 : y0;
  const uint8_t fx = logicalX256 & 0xFF;
  const uint8_t fy = logicalY256 & 0xFF;
  const uint16_t q00 = filteredRawAtLogical(frame, x0, y0, displayRotation);
  const uint16_t q10 = filteredRawAtLogical(frame, x1, y0, displayRotation);
  const uint16_t q01 = filteredRawAtLogical(frame, x0, y1, displayRotation);
  const uint16_t q11 = filteredRawAtLogical(frame, x1, y1, displayRotation);
  const uint32_t top = static_cast<uint32_t>(q00) * (256 - fx) + static_cast<uint32_t>(q10) * fx;
  const uint32_t bottom = static_cast<uint32_t>(q01) * (256 - fx) + static_cast<uint32_t>(q11) * fx;
  return (top * (256 - fy) + bottom * fy) >> 16;
}

void ThermalProcessor::calculatePercentileRange(const ThermalFrame& frame,
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
                                                uint16_t* high) const {
  if (rawMax <= rawMin + 4) {
    *low = rawMin;
    *high = rawMin + 4;
    return;
  }

  uint16_t bins[kHistogramBins] = {};
  const uint16_t range = rawMax - rawMin;
  for (uint16_t logicalY = cropY0; logicalY < cropY0 + cropH; ++logicalY) {
    for (uint16_t logicalX = cropX0; logicalX < cropX0 + cropW; ++logicalX) {
      const uint16_t raw = filteredRawAtLogical(frame, logicalX, logicalY, displayRotation);
      uint16_t bin = static_cast<uint32_t>(raw - rawMin) * (kHistogramBins - 1) / range;
      ++bins[bin];
    }
  }

  const uint32_t total = static_cast<uint32_t>(cropW) * cropH;
  const uint32_t lowTarget = total * lowPercent / 100;
  const uint32_t highTarget = total * highPercent / 100;
  uint32_t cumulative = 0;
  uint16_t lowBin = 0;
  uint16_t highBin = kHistogramBins - 1;
  bool lowFound = false;
  for (uint16_t bin = 0; bin < kHistogramBins; ++bin) {
    cumulative += bins[bin];
    if (!lowFound && cumulative >= lowTarget) {
      lowBin = bin;
      lowFound = true;
    }
    if (cumulative >= highTarget) {
      highBin = bin;
      break;
    }
  }

  *low = rawMin + static_cast<uint32_t>(lowBin) * range / (kHistogramBins - 1);
  *high = rawMin + static_cast<uint32_t>(highBin) * range / (kHistogramBins - 1);
  if (*high <= *low + 4) {
    *high = *low + 4;
  }
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
    case PaletteMode::Lava:
      return gradientColor(value, 8, 0, 16, 96, 0, 24, 220, 30, 0, 255, 220, 64);
    case PaletteMode::HotIron:
      return gradientColor(value, 0, 0, 0, 120, 28, 0, 240, 96, 0, 255, 255, 180);
    case PaletteMode::Medical:
      return gradientColor(value, 0, 32, 80, 0, 180, 180, 220, 255, 160, 255, 255, 255);
    case PaletteMode::Arctic:
      return gradientColor(value, 0, 12, 36, 0, 96, 180, 120, 220, 255, 255, 255, 255);
    case PaletteMode::Rainbow:
      if (value < 51) {
        return DisplayDriver::rgb565(80, 0, 180 + value);
      }
      if (value < 102) {
        return DisplayDriver::rgb565(0, (value - 51) * 5, 255);
      }
      if (value < 153) {
        return DisplayDriver::rgb565(0, 255, 255 - (value - 102) * 5);
      }
      if (value < 204) {
        return DisplayDriver::rgb565((value - 153) * 5, 255, 0);
      }
      return DisplayDriver::rgb565(255, 255 - (value - 204) * 5, 0);
    case PaletteMode::RedHot:
      return gradientColor(value, 0, 0, 0, 90, 0, 0, 220, 0, 0, 255, 230, 210);
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
