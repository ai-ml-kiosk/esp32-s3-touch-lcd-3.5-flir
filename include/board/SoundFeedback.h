#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

#include "es8311.h"

class SoundFeedback {
 public:
  bool begin();
  bool available() const { return ready_; }
  void setVolume(uint8_t volume);
  void playClick(bool enabled);
  void playAlert(bool enabled);
  void playScroll(bool enabled);

 private:
  struct ChimeStep {
    uint16_t fundamentalHz;
    uint16_t harmonicHz;
    uint16_t durationMs;
    uint16_t amplitude;
    uint8_t harmonicMix;
  };

  void playChime(bool enabled, const ChimeStep* steps, uint8_t stepCount);
  void playStep(const ChimeStep& step);
  void writeSilence(uint16_t frames);

  I2SClass i2s_;
  es8311_handle_t codec_ = nullptr;
  bool ready_ = false;
  uint8_t volume_ = 80;
  uint32_t lastToneMs_ = 0;
};
