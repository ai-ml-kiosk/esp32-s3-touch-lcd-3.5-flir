#include "board/SoundFeedback.h"

#include <Wire.h>
#include <cmath>
#include <cstring>

#include "pin_config.h"

namespace {

constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kMclkMultiple = 256;
constexpr uint16_t kToneChunkFrames = 128;
constexpr float kPi = 3.14159265f;

}  // namespace

bool SoundFeedback::begin() {
  Wire.begin(Pins::AUDIO_I2C_SDA, Pins::AUDIO_I2C_SCL);

  codec_ = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  if (codec_ == nullptr) {
    Serial.println("ES8311 create failed");
    return false;
  }

  const es8311_clock_config_t clockConfig = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = static_cast<int>(kSampleRate * kMclkMultiple),
      .sample_frequency = static_cast<int>(kSampleRate),
  };
  if (es8311_init(codec_, &clockConfig, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("ES8311 init failed");
    return false;
  }
  setVolume(volume_);
  es8311_microphone_config(codec_, false);
  es8311_voice_mute(codec_, false);

  i2s_.setPins(Pins::AUDIO_I2S_BCLK,
               Pins::AUDIO_I2S_LRCK,
               Pins::AUDIO_I2S_DOUT,
               Pins::AUDIO_I2S_DIN,
               Pins::AUDIO_I2S_MCLK);
  ready_ = i2s_.begin(I2S_MODE_STD,
                      kSampleRate,
                      I2S_DATA_BIT_WIDTH_16BIT,
                      I2S_SLOT_MODE_STEREO,
                      I2S_STD_SLOT_BOTH);
  if (!ready_) {
    Serial.println("I2S audio init failed");
    return false;
  }

  writeSilence(256);
  Serial.println("ES8311/I2S sound feedback ready");
  return true;
}

void SoundFeedback::setVolume(uint8_t volume) {
  volume_ = volume > 100 ? 100 : volume;
  if (codec_ != nullptr) {
    es8311_voice_volume_set(codec_, volume_, nullptr);
  }
}

void SoundFeedback::playClick(bool enabled) {
  static constexpr ChimeStep kClick[] = {
      {1568, 3136, 18, 7200, 30},
      {2093, 4186, 34, 6400, 22},
  };
  playChime(enabled, kClick, sizeof(kClick) / sizeof(kClick[0]));
}

void SoundFeedback::playAlert(bool enabled) {
  static constexpr ChimeStep kAlert[] = {
      {523, 1046, 36, 8600, 18},
      {392, 784, 62, 7600, 12},
  };
  playChime(enabled, kAlert, sizeof(kAlert) / sizeof(kAlert[0]));
}

void SoundFeedback::playScroll(bool enabled) {
  static constexpr ChimeStep kScroll[] = {
      {1319, 2638, 16, 4800, 28},
  };
  playChime(enabled, kScroll, sizeof(kScroll) / sizeof(kScroll[0]));
}

void SoundFeedback::playChime(bool enabled, const ChimeStep* steps, uint8_t stepCount) {
  if (!enabled || !ready_ || volume_ == 0 || steps == nullptr || stepCount == 0) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastToneMs_ < 18) {
    return;
  }
  lastToneMs_ = now;

  for (uint8_t i = 0; i < stepCount; ++i) {
    playStep(steps[i]);
  }
  writeSilence(80);
}

void SoundFeedback::playStep(const ChimeStep& step) {
  const uint16_t totalFrames = static_cast<uint32_t>(kSampleRate) * step.durationMs / 1000;
  uint16_t frame = 0;
  int16_t samples[kToneChunkFrames * 2];
  while (frame < totalFrames) {
    const uint16_t framesThisChunk = min<uint16_t>(kToneChunkFrames, totalFrames - frame);
    for (uint16_t i = 0; i < framesThisChunk; ++i) {
      const uint16_t sampleIndex = frame + i;
      const float progress = static_cast<float>(sampleIndex) / static_cast<float>(totalFrames);
      const float attack = progress < 0.16f ? progress / 0.16f : 1.0f;
      const float decay = 1.0f - progress;
      const float envelope = attack * decay * decay;
      const float phaseA = 2.0f * kPi * static_cast<float>(step.fundamentalHz) *
                           static_cast<float>(sampleIndex) / static_cast<float>(kSampleRate);
      const float phaseB = 2.0f * kPi * static_cast<float>(step.harmonicHz) *
                           static_cast<float>(sampleIndex) / static_cast<float>(kSampleRate);
      const float harmonic = static_cast<float>(step.harmonicMix) / 100.0f;
      const float tone = sinf(phaseA) * (1.0f - harmonic) + sinf(phaseB) * harmonic;
      const int16_t value = static_cast<int16_t>(tone * envelope * static_cast<float>(step.amplitude));
      samples[i * 2] = value;
      samples[i * 2 + 1] = value;
    }
    i2s_.write(reinterpret_cast<const uint8_t*>(samples), framesThisChunk * 2 * sizeof(int16_t));
    frame += framesThisChunk;
  }
  writeSilence(64);
}

void SoundFeedback::writeSilence(uint16_t frames) {
  int16_t samples[kToneChunkFrames * 2];
  memset(samples, 0, sizeof(samples));
  while (frames > 0) {
    const uint16_t framesThisChunk = min<uint16_t>(kToneChunkFrames, frames);
    i2s_.write(reinterpret_cast<const uint8_t*>(samples), framesThisChunk * 2 * sizeof(int16_t));
    frames -= framesThisChunk;
  }
}
