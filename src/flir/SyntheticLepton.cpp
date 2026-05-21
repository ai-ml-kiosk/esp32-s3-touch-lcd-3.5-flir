#include "flir/SyntheticLepton.h"

#include <Arduino.h>
#include <math.h>

bool SyntheticLepton::begin() {
  frameNumber_ = 0;
  return true;
}

bool SyntheticLepton::readFrame(ThermalFrame& frame) {
  ++frameNumber_;
  frame.frameNumber = frameNumber_;

  const float t = frameNumber_ * 0.14f;
  const float hotCx = 42.0f + 18.0f * sinf(t * 0.7f);
  const float hotCy = 25.0f + 12.0f * cosf(t * 0.5f);
  const float warmCx = 24.0f + 10.0f * cosf(t * 0.4f);
  const float warmCy = 42.0f + 8.0f * sinf(t * 0.6f);

  for (uint16_t y = 0; y < kLeptonHeight; ++y) {
    for (uint16_t x = 0; x < kLeptonWidth; ++x) {
      const float dxHot = x - hotCx;
      const float dyHot = y - hotCy;
      const float dxWarm = x - warmCx;
      const float dyWarm = y - warmCy;
      const float hot = 950.0f * expf(-(dxHot * dxHot + dyHot * dyHot) / 260.0f);
      const float warm = 420.0f * expf(-(dxWarm * dxWarm + dyWarm * dyWarm) / 340.0f);
      const float wave = 85.0f * sinf((x + frameNumber_) * 0.12f) + 60.0f * cosf((y - frameNumber_) * 0.18f);
      frame.raw[y * kLeptonWidth + x] = static_cast<uint16_t>(29600.0f + hot + warm + wave);
    }
  }
  return true;
}
