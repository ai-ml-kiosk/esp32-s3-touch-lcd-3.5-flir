#pragma once

#include <Arduino.h>

static constexpr uint16_t kLeptonWidth = 80;
static constexpr uint16_t kLeptonHeight = 60;
static constexpr size_t kLeptonPixelCount = kLeptonWidth * kLeptonHeight;

struct ThermalFrame {
  uint16_t width = kLeptonWidth;
  uint16_t height = kLeptonHeight;
  uint16_t raw[kLeptonPixelCount] = {};
  uint32_t frameNumber = 0;
};

struct ThermalStats {
  uint16_t minRaw = 0;
  uint16_t maxRaw = 0;
  uint16_t centerRaw = 0;
  float minC = 0.0f;
  float maxC = 0.0f;
  float centerC = 0.0f;
  uint16_t hotX = 0;
  uint16_t hotY = 0;
  uint16_t coldX = 0;
  uint16_t coldY = 0;
  uint16_t markerWidth = kLeptonWidth;
  uint16_t markerHeight = kLeptonHeight;
};

enum class PaletteMode {
  Ironbow,
  WhiteHot,
  BlackHot,
  Histogram,
  Lava,
  HotIron,
  Medical,
  Arctic,
  Rainbow,
  RedHot,
};
