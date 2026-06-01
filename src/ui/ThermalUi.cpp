#include "ui/ThermalUi.h"

#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr uint16_t kBg = 0x0A12;
constexpr uint16_t kPanel = 0x1186;
constexpr uint16_t kButton = 0x29AA;
constexpr uint16_t kPrimary = 0x04BF;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kMuted = 0xBDF7;
constexpr uint16_t kHot = 0xFFE0;
constexpr uint16_t kCold = 0x7DFF;
constexpr uint16_t kCustomMarker = 0xF81F;
constexpr uint16_t kError = 0xF800;
constexpr uint16_t kLabelBg = 0x0000;
constexpr uint16_t kLandscapeCaptureThumbW = 464;
constexpr uint16_t kLandscapeCaptureThumbH = 226;
constexpr uint16_t kLandscapeClipThumbH = 190;
constexpr uint16_t kPortraitCaptureThumbW = 304;
constexpr uint16_t kPortraitCaptureThumbH = 376;
constexpr uint16_t kPortraitClipThumbH = 330;
constexpr uint16_t kClipFrameIntervalMs = 116;
constexpr int16_t kLandscapeSetupMaxScroll = 124;
constexpr int16_t kPortraitSetupMaxScroll = 152;

struct SetupRect {
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
};

const char* paletteName(PaletteMode palette) {
  switch (palette) {
    case PaletteMode::WhiteHot:
      return "WHITE";
    case PaletteMode::BlackHot:
      return "BLACK";
    case PaletteMode::Histogram:
      return "HIST";
    case PaletteMode::Lava:
      return "LAVA";
    case PaletteMode::HotIron:
      return "HOTIRON";
    case PaletteMode::Medical:
      return "MED";
    case PaletteMode::Arctic:
      return "ARCTIC";
    case PaletteMode::Rainbow:
      return "RAIN";
    case PaletteMode::RedHot:
      return "RED";
    case PaletteMode::Ironbow:
    default:
      return "IRON";
  }
}

PaletteMode paletteFromIndex(uint8_t paletteMode) {
  if (paletteMode > static_cast<uint8_t>(PaletteMode::RedHot)) {
    return PaletteMode::Ironbow;
  }
  return static_cast<PaletteMode>(paletteMode);
}

uint8_t paletteToIndex(PaletteMode palette) {
  return static_cast<uint8_t>(palette);
}

const char* qualityName(uint8_t mode) {
  switch (mode) {
    case 0:
      return "DETAIL";
    case 2:
      return "SMOOTH";
    case 1:
    default:
      return "BAL";
  }
}

uint16_t orientationWidth(bool landscape) {
  return 34;
}

void formatTemp(char* out, size_t outSize, float temp) {
  snprintf(out, outSize, "%.1fC", temp);
}

void formatBytes(char* out, size_t outSize, uint64_t bytes) {
  if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
    snprintf(out, outSize, "%.1fG", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  } else if (bytes >= 1024ULL * 1024ULL) {
    snprintf(out, outSize, "%.1fM", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024ULL) {
    snprintf(out, outSize, "%.1fK", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(out, outSize, "%uB", static_cast<unsigned>(bytes));
  }
}

void formatClipTime(char* out, size_t outSize, uint32_t ms) {
  const uint32_t totalSeconds = (ms + 500) / 1000;
  const uint32_t minutes = totalSeconds / 60;
  const uint32_t seconds = totalSeconds % 60;
  snprintf(out, outSize, "%02lu:%02lu",
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
}

void fitText(char* out, size_t outSize, const char* text, uint16_t maxPixels) {
  if (outSize == 0) {
    return;
  }
  out[0] = '\0';
  if (text == nullptr) {
    return;
  }
  const size_t maxChars = maxPixels > 6 ? maxPixels / 6 : 1;
  const size_t len = strlen(text);
  if (len <= maxChars || maxChars < 4) {
    strncpy(out, text, outSize - 1);
    out[outSize - 1] = '\0';
    return;
  }
  out[0] = '.';
  out[1] = '.';
  out[2] = '.';
  const size_t tail = maxChars - 3;
  strncpy(out + 3, text + len - tail, outSize - 4);
  out[outSize - 1] = '\0';
}

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

float rawToCelsius(uint16_t raw, const AppSettings& settings) {
  return raw * 0.01f - 273.15f + static_cast<float>(settings.temperatureOffsetTenths) / 10.0f;
}

void formatOffset(char* out, size_t outSize, int8_t offsetTenths) {
  snprintf(out, outSize,
           "%+.1fC",
           static_cast<float>(offsetTenths) / 10.0f);
}

bool rowVisible(int16_t y, int16_t contentTop, int16_t contentBottom, int16_t height = 26) {
  return y >= contentTop && (y + height) <= contentBottom;
}

void drawSetupText(DisplayDriver& display,
                   int16_t x,
                   int16_t y,
                   const char* text,
                   uint16_t color,
                   int16_t contentTop,
                   int16_t contentBottom,
                   uint8_t size = 1) {
  const int16_t h = static_cast<int16_t>(8 * size);
  if (rowVisible(y, contentTop, contentBottom, h)) {
    display.drawText(x, y, text, color, size);
  }
}

void drawSetupLabel(DisplayDriver& display,
                    int16_t x,
                    int16_t y,
                    const char* text,
                    uint16_t color,
                    int16_t contentTop,
                    int16_t contentBottom,
                    uint8_t maxChars = 8) {
  if (text == nullptr || !rowVisible(y - 4, contentTop, contentBottom, 22)) {
    return;
  }
  const size_t len = strlen(text);
  if (len <= maxChars) {
    display.drawText(x, y, text, color, 1);
    return;
  }
  const char* split = strchr(text, ' ');
  if (split == nullptr || split == text || strlen(split + 1) == 0) {
    char shortText[10] = {};
    strncpy(shortText, text, sizeof(shortText) - 1);
    display.drawText(x, y, shortText, color, 1);
    return;
  }
  char first[10] = {};
  const size_t firstLen = static_cast<size_t>(split - text);
  strncpy(first, text, firstLen < sizeof(first) ? firstLen : sizeof(first) - 1);
  first[sizeof(first) - 1] = '\0';
  display.drawText(x, y - 5, first, color, 1);
  display.drawText(x, y + 6, split + 1, color, 1);
}

void drawSetupButton(DisplayDriver& display,
                     SetupRect rect,
                     const char* label,
                     int16_t contentTop,
                     int16_t contentBottom,
                     bool primary = false) {
  if (rowVisible(rect.y, contentTop, contentBottom, rect.h)) {
    display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, primary ? kPrimary : kButton);
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 6, DisplayDriver::rgb565(100, 116, 139));
    char visibleLabel[24] = {};
    const size_t labelLen = label != nullptr ? strlen(label) : 0;
    const size_t maxChars = rect.w > 10 ? (rect.w - 10) / 6 : 1;
    if (labelLen > maxChars && maxChars > 3) {
      visibleLabel[0] = '.';
      visibleLabel[1] = '.';
      const size_t tailLen = maxChars - 2;
      strncpy(visibleLabel + 2, label + labelLen - tailLen, sizeof(visibleLabel) - 3);
      visibleLabel[sizeof(visibleLabel) - 1] = '\0';
    } else if (label != nullptr) {
      strncpy(visibleLabel, label, sizeof(visibleLabel) - 1);
    }
    const uint16_t labelWidth = strlen(visibleLabel) * 6;
    const int16_t textX = rect.x + (static_cast<int16_t>(rect.w) - static_cast<int16_t>(labelWidth)) / 2;
    const int16_t textY = rect.y + (static_cast<int16_t>(rect.h) - 8) / 2;
    display.drawText(textX, textY, visibleLabel, kText, 1);
  }
}

void drawSetupStepper(DisplayDriver& display,
                      SetupRect rect,
                      const char* value,
                      int16_t contentTop,
                      int16_t contentBottom) {
  if (!rowVisible(rect.y, contentTop, contentBottom, rect.h)) {
    return;
  }
  display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, kButton);
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 6, DisplayDriver::rgb565(100, 116, 139));
  const uint16_t sideW = rect.w >= 72 ? 18 : 16;
  const int16_t centerX = rect.x + sideW;
  const uint16_t centerW = rect.w > sideW * 2 ? rect.w - sideW * 2 : rect.w / 2;
  display.fillRect(centerX, rect.y + 1, centerW, rect.h - 2, DisplayDriver::rgb565(30, 41, 59));
  display.drawText(rect.x + (sideW / 2) - 3, rect.y + 9, "-", kText, 1);
  display.drawText(rect.x + rect.w - (sideW / 2) - 3, rect.y + 9, "+", kText, 1);
  char visibleValue[16] = {};
  const char* label = value != nullptr ? value : "";
  const size_t maxChars = centerW > 8 ? (centerW - 6) / 6 : 1;
  const size_t len = strlen(label);
  if (len > maxChars && maxChars > 3) {
    visibleValue[0] = '.';
    visibleValue[1] = '.';
    const size_t tailLen = maxChars - 2;
    strncpy(visibleValue + 2, label + len - tailLen, sizeof(visibleValue) - 3);
  } else {
    strncpy(visibleValue, label, sizeof(visibleValue) - 1);
  }
  visibleValue[sizeof(visibleValue) - 1] = '\0';
  const uint16_t valueW = strlen(visibleValue) * 6;
  display.drawText(centerX + (static_cast<int16_t>(centerW) - static_cast<int16_t>(valueW)) / 2,
                   rect.y + 9,
                   visibleValue,
                   kText,
                   1);
}

void drawSetupToggle(DisplayDriver& display,
                     SetupRect rect,
                     bool enabled,
                     int16_t contentTop,
                     int16_t contentBottom) {
  if (!rowVisible(rect.y, contentTop, contentBottom, rect.h)) {
    return;
  }
  const uint16_t trackColor = enabled ? DisplayDriver::rgb565(34, 197, 94)
                                      : DisplayDriver::rgb565(100, 116, 139);
  const uint16_t knobColor = DisplayDriver::rgb565(248, 250, 252);
  display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, trackColor);
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, DisplayDriver::rgb565(71, 85, 105));
  const uint16_t knob = rect.h > 6 ? rect.h - 6 : rect.h;
  const int16_t knobX = enabled ? rect.x + static_cast<int16_t>(rect.w - knob - 3) : rect.x + 3;
  display.fillRoundRect(knobX, rect.y + 3, knob, knob, knob / 2, knobColor);
  display.drawRoundRect(knobX, rect.y + 3, knob, knob, knob / 2, DisplayDriver::rgb565(203, 213, 225));
  display.drawText(enabled ? rect.x + 10 : rect.x + static_cast<int16_t>(rect.w) - 28,
                   rect.y + (static_cast<int16_t>(rect.h) - 8) / 2,
                   enabled ? "ON" : "OFF",
                   kText,
                   1);
}

}  // namespace

bool ThermalUi::begin(DisplayDriver& display) {
  display.fillScreen(kBg);
  return true;
}

void ThermalUi::applySettings(const AppSettings& settings) {
  palette_ = paletteFromIndex(settings.paletteMode);
  zoomed_ = settings.zoomed;
}

void ThermalUi::showStatus(const char* message) {
  if (message == nullptr || message[0] == '\0') {
    return;
  }
  strncpy(feedback_, message, sizeof(feedback_) - 1);
  feedback_[sizeof(feedback_) - 1] = '\0';
  feedbackUntilMs_ = millis() + 3500;
}

bool ThermalUi::consumeVideoClipRequest() {
  const bool requested = videoClipRequest_;
  videoClipRequest_ = false;
  return requested;
}

bool ThermalUi::consumeSoftPowerRequest() {
  const bool requested = softPowerRequest_;
  softPowerRequest_ = false;
  return requested;
}

ThermalUi::SoundEvent ThermalUi::consumeSoundEvent() {
  const SoundEvent event = pendingSoundEvent_;
  pendingSoundEvent_ = SoundEvent::None;
  return event;
}

void ThermalUi::setVideoClipActive(bool active) {
  videoClipActive_ = active;
}

bool ThermalUi::updatePlayback(CaptureStorage& storage,
                               ThermalProcessor& thermal,
                               const AppSettings& settings,
                               uint16_t viewportWidth,
                               uint16_t viewportHeight) {
  if (!playbackActive_ || !playbackClipMode_ || !clipPlaying_) {
    return false;
  }
  const uint32_t now = millis();
  if (now - lastClipPlaybackMs_ < 140) {
    return false;
  }
  lastClipPlaybackMs_ = now;

  if (clipFrameCount_ > 0 && clipFrameIndex_ >= clipFrameCount_) {
    clipPlaying_ = false;
    clipAtEnd_ = true;
    showStatus("Clip ended");
    return true;
  }

  const bool landscapePreview = viewportWidth > viewportHeight;
  const uint16_t thumbW = landscapePreview ? kLandscapeCaptureThumbW : kPortraitCaptureThumbW;
  const uint16_t thumbH = landscapePreview ? kLandscapeClipThumbH : kPortraitClipThumbH;
  if (!ensureCaptureThumbnailBuffer(thumbW, thumbH) || !ensurePlaybackClipFrame()) {
    hasCaptureThumbnail_ = false;
    hasPlaybackClipStats_ = false;
    clipPlaying_ = false;
    showStatus("Clip memory failed");
    return false;
  }

  if (!storage.openThermalClipPlayback(captureBrowserBasePath_, &clipFrameCount_)) {
    hasCaptureThumbnail_ = false;
    hasPlaybackClipStats_ = false;
    clipPlaying_ = false;
    showStatus("Clip unavailable");
    return false;
  }

  uint32_t frameIndex = 0;
  if (!storage.readNextThermalClipPlaybackFrame(*playbackClipFrame_, &frameIndex, &clipFrameCount_)) {
    hasCaptureThumbnail_ = false;
    hasPlaybackClipStats_ = false;
    clipPlaying_ = false;
    showStatus("Clip read failed");
    return false;
  }

  clipFrameIndex_ = frameIndex + 1;
  clipAtEnd_ = clipFrameIndex_ >= clipFrameCount_;
  ThermalStats clipStats;
  hasCaptureThumbnail_ = thermal.renderRgb565(*playbackClipFrame_,
                                              palette_,
                                              captureThumbnailPixels_,
                                              captureThumbnailWidth_,
                                              captureThumbnailHeight_,
                                              &clipStats,
                                              false,
                                              settings.landscape,
                                              settings.displayRotation,
                                              settings.imageQualityMode);
  if (hasCaptureThumbnail_) {
    playbackClipStats_ = clipStats;
    hasPlaybackClipStats_ = true;
  } else {
    hasPlaybackClipStats_ = false;
  }
  if (clipAtEnd_) {
    clipPlaying_ = false;
  }
  hasCapturePreview_ = false;
  return hasCaptureThumbnail_;
}

void ThermalUi::render(DisplayDriver& display,
                       const ThermalFrame& frame,
                       const ThermalStats& stats,
                       const uint16_t* viewportPixels,
                       uint16_t viewportWidth,
                       uint16_t viewportHeight,
                       const AppSettings& settings,
                       CaptureStorage& storage,
                       bool storageReady,
                       const char* lastCaptureBasePath) {
  if (frame.frameNumber == 0) {
    renderWaiting(display, settings, storageReady);
    return;
  }

  const DisplayInfo info = display.info();
  display.fillScreen(kBg);

  const uint16_t statusH = settings.landscape ? 44 : 44;
  display.fillRect(0, 0, info.width, statusH, kPanel);

  const uint16_t orientW = orientationWidth(settings.landscape);
  const int16_t orientX = 8;
  drawOrientationIcon(display, orientX, 10, settings.landscape);

  const int16_t hotColdX = orientX + orientW + 8;
  drawHotColdIcon(display, hotColdX + 12, 21, settings.showHotColdDetails);

  const int16_t centerX = hotColdX + 34;
  drawCenterTempIcon(display, centerX + 12, 21, settings.showCenterTemperature);

  const int16_t clearMarkersX = centerX + 34;
  drawClearMarkersIcon(display, clearMarkersX + 12, 21, customMarkerCount_ > 0);

  const int16_t soundX = clearMarkersX + 34;
  drawSoundIcon(display, soundX + 12, 21, settings.soundEnabled);

  const int16_t zoomTextX = soundX + 36;
  display.drawText(zoomTextX, 14, zoomed_ ? "Z2X" : "Z1X", zoomed_ ? kText : kMuted, 1);

  const int16_t liveX = zoomTextX + 34;
  display.drawText(liveX, 14, "LIVE", kText, 1);
  display.fillRoundRect(liveX + 36, 16, 10, 10, 5, DisplayDriver::rgb565(34, 197, 94));
  if (settings.landscape) {
    display.drawText(liveX + 58, 14, "8.6 fps", kMuted, 1);
  }
  const int16_t storageTextRight = static_cast<int16_t>(info.width - 8);
  const uint16_t storageBgW = settings.landscape ? 74 : 54;
  display.fillRect(info.width > storageBgW ? info.width - storageBgW : 0, 2, storageBgW, statusH - 4, kPanel);
  display.drawTextRight(storageTextRight, settings.landscape ? 10 : 8, storageReady ? "TF OK" : "NO TF", storageReady ? kMuted : kError, 1);
  if (storageReady) {
    char freeText[16] = {};
    char freeLine[24] = {};
    formatBytes(freeText, sizeof(freeText), storage.freeBytes());
    snprintf(freeLine, sizeof(freeLine), "FREE %s", freeText);
    display.drawTextRight(storageTextRight, settings.landscape ? 26 : 24, freeLine, kMuted, 1);
  } else {
    display.drawTextRight(storageTextRight, settings.landscape ? 26 : 24, "FREE --", kError, 1);
  }

  uint16_t viewX;
  uint16_t viewY;
  uint16_t readoutX;
  uint16_t readoutY;
  if (settings.landscape) {
    viewX = 0;
    viewY = 48;
    readoutX = info.width - 42;
    readoutY = viewY;
  } else {
    viewX = 0;
    viewY = 52;
    readoutX = 0;
    readoutY = static_cast<uint16_t>(viewY + viewportHeight + 4);
  }

  const bool replayPreview = playbackActive_ && hasCapturePreview_ &&
                             capturePreviewPixels_ != nullptr &&
                             capturePreviewWidth_ == viewportWidth &&
                             capturePreviewHeight_ == viewportHeight;
  display.drawBitmap(viewX,
                     viewY,
                     viewportWidth,
                     viewportHeight,
                     replayPreview ? capturePreviewPixels_ : viewportPixels);

  const uint16_t markerW = stats.markerWidth > 0 ? stats.markerWidth : frame.width;
  const uint16_t markerH = stats.markerHeight > 0 ? stats.markerHeight : frame.height;
  const uint16_t hotX = viewX + static_cast<uint32_t>(stats.hotX) * viewportWidth / markerW;
  const uint16_t hotY = viewY + static_cast<uint32_t>(stats.hotY) * viewportHeight / markerH;
  const uint16_t coldX = viewX + static_cast<uint32_t>(stats.coldX) * viewportWidth / markerW;
  const uint16_t coldY = viewY + static_cast<uint32_t>(stats.coldY) * viewportHeight / markerH;

  char tempText[16];
  if (settings.showHotColdDetails) {
    display.drawRect(hotX > 5 ? hotX - 5 : hotX, hotY > 5 ? hotY - 5 : hotY, 11, 11, kHot);
    display.drawRect(coldX > 5 ? coldX - 5 : coldX, coldY > 5 ? coldY - 5 : coldY, 11, 11, kCold);
    formatTemp(tempText, sizeof(tempText), stats.maxC);
    int16_t labelX = hotX + 9;
    if (labelX > static_cast<int16_t>(info.width - 74)) {
      labelX = hotX > 74 ? hotX - 74 : 0;
    }
    drawTempLabel(display, labelX, hotY > 18 ? hotY - 18 : hotY + 10, tempText, kHot);
    formatTemp(tempText, sizeof(tempText), stats.minC);
    labelX = coldX + 9;
    if (labelX > static_cast<int16_t>(info.width - 74)) {
      labelX = coldX > 74 ? coldX - 74 : 0;
    }
    drawTempLabel(display, labelX, coldY > 18 ? coldY - 18 : coldY + 10, tempText, kCold);
  }

  if (settings.showCenterTemperature) {
    const uint16_t centerMarkerX = viewX + viewportWidth / 2;
    const uint16_t centerMarkerY = viewY + viewportHeight / 2;
    display.fillRect(centerMarkerX > 10 ? centerMarkerX - 10 : centerMarkerX, centerMarkerY, 21, 2, kText);
    display.fillRect(centerMarkerX, centerMarkerY > 10 ? centerMarkerY - 10 : centerMarkerY, 2, 21, kText);
    formatTemp(tempText, sizeof(tempText), stats.centerC);
    int16_t labelX = centerMarkerX + 11;
    if (labelX > static_cast<int16_t>(info.width - 74)) {
      labelX = centerMarkerX > 74 ? centerMarkerX - 74 : 0;
    }
    drawTempLabel(display, labelX, centerMarkerY > 20 ? centerMarkerY - 20 : centerMarkerY + 12, tempText, kText);
  }

  for (uint8_t i = 0; i < customMarkerCount_; ++i) {
    if (!customMarkers_[i].active) {
      continue;
    }
    const uint16_t markerX = viewX + static_cast<uint32_t>(customMarkers_[i].xPermil) * viewportWidth / 1000;
    const uint16_t markerY = viewY + static_cast<uint32_t>(customMarkers_[i].yPermil) * viewportHeight / 1000;
    display.drawRect(markerX > 6 ? markerX - 6 : markerX, markerY > 6 ? markerY - 6 : markerY, 13, 13, kCustomMarker);
    display.fillRect(markerX > 11 ? markerX - 11 : markerX, markerY, 23, 2, kCustomMarker);
    display.fillRect(markerX, markerY > 11 ? markerY - 11 : markerY, 2, 23, kCustomMarker);
    char markerText[20] = {};
    char tempText[16] = {};
    formatTemp(tempText, sizeof(tempText), customMarkerTemperature(frame, settings, customMarkers_[i]));
    snprintf(markerText, sizeof(markerText), "M%u %s", static_cast<unsigned>(i + 1), tempText);
    int16_t labelX = markerX + 12;
    if (labelX > static_cast<int16_t>(info.width - 80)) {
      labelX = markerX > 80 ? markerX - 80 : 0;
    }
    drawTempLabel(display, labelX, markerY > 20 ? markerY - 20 : markerY + 12, markerText, kCustomMarker);
  }

  if (settings.landscape) {
    int16_t scaleX = static_cast<int16_t>(viewX + viewportWidth + 4);
    if (scaleX > static_cast<int16_t>(info.width - 58)) {
      scaleX = static_cast<int16_t>(info.width - 58);
    }
    display.fillRect(readoutX, readoutY, 42, viewportHeight, kPanel);
    display.drawText(readoutX + 6, readoutY + 16, "CTR", kMuted, 1);
    formatTemp(tempText, sizeof(tempText), stats.centerC);
    display.drawText(readoutX + 6, readoutY + 34, tempText, kText, 1);
    display.drawText(readoutX + 6, readoutY + 82, "RNG", kMuted, 1);
    display.drawText(readoutX + 6, readoutY + 100, "AUTO", kText, 1);
    display.drawText(readoutX + 6, readoutY + 148, "PAL", kMuted, 1);
    display.drawText(readoutX + 6, readoutY + 166, paletteName(palette_), kText, 1);
    drawPaletteScaleEndLabels(display, scaleX, viewY, viewportHeight, stats.maxC, stats.minC);
  } else {
    const int16_t scaleX = static_cast<int16_t>(info.width - 24);
    drawPaletteScaleEndLabels(display, scaleX, viewY, viewportHeight, stats.maxC, stats.minC);
    display.fillRect(readoutX, readoutY, info.width, 48, kPanel);
    display.drawText(16, readoutY + 10, "CTR", kMuted, 1);
    formatTemp(tempText, sizeof(tempText), stats.centerC);
    display.drawText(16, readoutY + 28, tempText, kText, 1);
    display.drawText(112, readoutY + 10, "RNG", kMuted, 1);
    display.drawText(112, readoutY + 28, "AUTO", kText, 1);
    display.drawText(220, readoutY + 10, "PAL", kMuted, 1);
    display.drawText(220, readoutY + 28, paletteName(palette_), kText, 1);
  }

  const uint16_t buttonY = info.height - (settings.landscape ? 52 : 54);
  const uint16_t buttonW = settings.landscape ? 42 : 32;
  const uint16_t buttonH = 32;
  const uint16_t gap = settings.landscape ? 8 : 7;
  const uint16_t mainButtonCount = 6;
  const uint16_t reservedPowerGap = settings.landscape ? 0 : buttonW;
  const uint16_t totalButtonW = mainButtonCount * buttonW + (mainButtonCount - 1) * gap + reservedPowerGap;
  const uint16_t startX = totalButtonW < info.width ? (info.width - totalButtonW) / 2 : 0;
  drawIconButton(display, {static_cast<int16_t>(startX), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Palette);
  drawIconButton(display, {static_cast<int16_t>(startX + buttonW + gap), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Noise);
  drawIconButton(display, {static_cast<int16_t>(startX + 2 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Capture);
  drawIconButton(display, {static_cast<int16_t>(startX + 3 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::VideoClip, videoClipActive_);
  drawIconButton(display, {static_cast<int16_t>(startX + 4 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Playback);
  drawIconButton(display, {static_cast<int16_t>(startX + 5 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Setup);
  drawIconButton(display, {static_cast<int16_t>(info.width - buttonW - 4), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::SoftPower);
  drawFeedback(display, settings.landscape);

  if (playbackActive_) {
    renderPlayback(display, settings, storage);
  }
  if (setupActive_) {
    renderSetup(display, setupDraftSettings_);
  }
}

bool ThermalUi::handleTouch(const TouchPoint& touch,
                            AppSettings& settings,
                            DisplayDriver& display,
                            CaptureStorage& storage,
                            ThermalProcessor& thermal,
                            const ThermalFrame& frame,
                            const ThermalStats& stats,
                            const uint16_t* viewportPixels,
                            uint16_t viewportWidth,
                            uint16_t viewportHeight) {
  const uint32_t now = millis();
  if (setupActive_) {
    return handleSetupTouch(touch, settings, storage);
  }

  if (touch.pressed && touch.touchCount >= 2) {
    return handlePinchZoom(settings, touch);
  }
  if (pinchActive_) {
    pinchActive_ = false;
    return true;
  }

  if (!touch.pressed) {
    setupDragActive_ = false;
    setupDragMoved_ = false;
    setupControlHeld_ = false;
    setupPendingAction_ = Action::None;
    return false;
  }

  if (now < ignoreTouchUntilMs_) {
    return false;
  }
  const uint32_t touchDebounceMs = playbackActive_ ? 55 : 120;
  if (now - lastTouchMs_ < touchDebounceMs) {
    return false;
  }
  lastTouchMs_ = now;

  const Action action = hitTest(touch.x, touch.y, settings.landscape);
  if (playbackActive_ && now < ignoreSetupTouchUntilMs_ &&
      (action == Action::PlaybackClose || action == Action::PlaybackDelete ||
       action == Action::PlaybackPrev || action == Action::PlaybackNext)) {
    return false;
  }
    if (setupActive_ && now < ignoreSetupTouchUntilMs_ &&
      (action == Action::SetupCancel || action == Action::SetupSave ||
       action == Action::SetupPath || action == Action::SetupIdle ||
       action == Action::SetupIdleDown ||
       action == Action::SetupTempOffset || action == Action::SetupTempOffsetDown ||
       action == Action::SetupAutoRotate ||
       action == Action::SetupOrientation || action == Action::SetupFilename ||
       action == Action::SetupRaw || action == Action::SetupAutoFfc ||
       action == Action::SetupClipDuration || action == Action::SetupClipDurationDown ||
       action == Action::SetupSoundVolume ||
       action == Action::SetupSoundVolumeDown)) {
    return false;
  }

  switch (action) {
    case Action::Palette:
      pendingSoundEvent_ = SoundEvent::Click;
      cyclePalette(settings);
      showStatus(paletteName(palette_));
      return true;
    case Action::Noise:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleImageQuality(settings);
      {
        char message[40] = {};
        snprintf(message, sizeof(message), "Quality %s", qualityName(settings.imageQualityMode));
        showStatus(message);
      }
      return true;
    case Action::Capture:
      pendingSoundEvent_ = SoundEvent::Click;
      {
        char savedBasePath[96] = {};
        const bool saved = storage.saveCapture(settings,
                                               frame.raw,
                                               kLeptonPixelCount,
                                               viewportPixels,
                                               viewportWidth,
                                               viewportHeight,
                                               stats,
                                               savedBasePath,
                                               sizeof(savedBasePath));
        char message[40] = {};
        if (saved) {
          storeCapturePreview(viewportPixels, viewportWidth, viewportHeight);
          snprintf(message, sizeof(message), "Saved %s.bmp", savedBasePath);
        } else if (savedBasePath[0] != '\0') {
          snprintf(message, sizeof(message), "Save failed %s", savedBasePath);
        } else {
          snprintf(message, sizeof(message), "Capture failed");
        }
        showStatus(message);
      }
      ignoreTouchUntilMs_ = now + 1500;
      return true;
    case Action::VideoClip:
      pendingSoundEvent_ = SoundEvent::Click;
      videoClipRequest_ = true;
      ignoreTouchUntilMs_ = now + 450;
      showStatus(videoClipActive_ ? "Stopping clip..." : "Recording clip...");
      return true;
    case Action::Playback:
      pendingSoundEvent_ = SoundEvent::Click;
      storage.closeThermalClipPlayback();
      releasePlaybackClipFrame();
      playbackClipMode_ = false;
      clipPlaying_ = false;
      clipAtEnd_ = false;
      refreshCaptureBrowser(storage, settings);
      if (captureBrowserCount_ > 0 &&
          storage.latestCaptureBase(settings.savePath, captureBrowserBasePath_, sizeof(captureBrowserBasePath_))) {
        captureBrowserIndex_ = captureBrowserCount_ - 1;
      }
      loadBrowserPreview(storage, viewportWidth, viewportHeight);
      playbackActive_ = true;
      ignoreSetupTouchUntilMs_ = now + 600;
      showStatus(captureBrowserCount_ == 0 ? "No captures found" : "Capture browser");
      return true;
    case Action::PlaybackImageTab:
      pendingSoundEvent_ = SoundEvent::Click;
      storage.closeThermalClipPlayback();
      releasePlaybackClipFrame();
      playbackClipMode_ = false;
      clipPlaying_ = false;
      clipAtEnd_ = false;
      refreshCaptureBrowser(storage, settings);
      loadBrowserPreview(storage, viewportWidth, viewportHeight);
      showStatus("Image captures");
      return true;
    case Action::PlaybackClipTab:
      pendingSoundEvent_ = SoundEvent::Click;
      storage.closeThermalClipPlayback();
      playbackClipMode_ = true;
      clipPlaying_ = false;
      clipAtEnd_ = false;
      captureBrowserCount_ = storage.clipCount(settings.savePath);
      captureBrowserIndex_ = captureBrowserCount_ > 0 ? captureBrowserCount_ - 1 : 0;
      captureBrowserBasePath_[0] = '\0';
      if (captureBrowserCount_ > 0) {
        storage.clipBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
      }
      clipFrameIndex_ = 0;
      loadClipPreview(storage, thermal, settings, viewportWidth, viewportHeight);
      showStatus(captureBrowserCount_ == 0 ? "No clips found" : "Thermal clips");
      return true;
    case Action::PlaybackPrev:
      pendingSoundEvent_ = SoundEvent::Click;
      if (captureBrowserCount_ > 0) {
        captureBrowserIndex_ = captureBrowserIndex_ == 0 ? captureBrowserCount_ - 1 : captureBrowserIndex_ - 1;
        if (playbackClipMode_) {
          storage.closeThermalClipPlayback();
          storage.clipBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
          clipFrameIndex_ = 0;
          clipPlaying_ = false;
          clipAtEnd_ = false;
          showStatus(loadClipPreview(storage, thermal, settings, viewportWidth, viewportHeight) ? "Previous clip" : "Clip unavailable");
        } else {
          storage.captureBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
          showStatus(loadBrowserPreview(storage, viewportWidth, viewportHeight) ? "Previous capture" : "Preview unavailable");
        }
      }
      ignoreTouchUntilMs_ = now + 90;
      return true;
    case Action::PlaybackNext:
      pendingSoundEvent_ = SoundEvent::Click;
      if (captureBrowserCount_ > 0) {
        captureBrowserIndex_ = (captureBrowserIndex_ + 1) % captureBrowserCount_;
        if (playbackClipMode_) {
          storage.closeThermalClipPlayback();
          storage.clipBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
          clipFrameIndex_ = 0;
          clipPlaying_ = false;
          clipAtEnd_ = false;
          showStatus(loadClipPreview(storage, thermal, settings, viewportWidth, viewportHeight) ? "Next clip" : "Clip unavailable");
        } else {
          storage.captureBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
          showStatus(loadBrowserPreview(storage, viewportWidth, viewportHeight) ? "Next capture" : "Preview unavailable");
        }
      }
      ignoreTouchUntilMs_ = now + 90;
      return true;
    case Action::PlaybackPlayPause:
      pendingSoundEvent_ = SoundEvent::Click;
      if (playbackClipMode_ && captureBrowserCount_ > 0) {
        if (!clipPlaying_) {
          if (clipAtEnd_) {
            storage.closeThermalClipPlayback();
            clipFrameIndex_ = 0;
            clipAtEnd_ = false;
            loadClipPreview(storage, thermal, settings, viewportWidth, viewportHeight);
          }
          if (!storage.openThermalClipPlayback(captureBrowserBasePath_, &clipFrameCount_)) {
            showStatus("Clip unavailable");
            return true;
          }
        }
        clipPlaying_ = !clipPlaying_;
        lastClipPlaybackMs_ = 0;
        showStatus(clipPlaying_ ? "Clip playing" : "Clip paused");
      }
      return true;
    case Action::PlaybackClose:
      pendingSoundEvent_ = SoundEvent::Click;
      playbackActive_ = false;
      hasCapturePreview_ = false;
      storage.closeThermalClipPlayback();
      releasePlaybackClipFrame();
      releaseCaptureThumbnail();
      display.fillScreen(kBg);
      ignoreTouchUntilMs_ = now + 300;
      showStatus("Review closed");
      return true;
    case Action::PlaybackDelete:
      pendingSoundEvent_ = SoundEvent::Click;
      if (playbackClipMode_) {
        if (storage.deleteThermalClip(captureBrowserBasePath_)) {
          hasCaptureThumbnail_ = false;
          storage.closeThermalClipPlayback();
          releasePlaybackClipFrame();
          clipPlaying_ = false;
          clipAtEnd_ = false;
          captureBrowserCount_ = storage.clipCount(settings.savePath);
          if (captureBrowserCount_ == 0) {
            captureBrowserIndex_ = 0;
            captureBrowserBasePath_[0] = '\0';
          } else {
            if (captureBrowserIndex_ >= captureBrowserCount_) {
              captureBrowserIndex_ = captureBrowserCount_ - 1;
            }
            storage.clipBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
            clipFrameIndex_ = 0;
            clipAtEnd_ = false;
            loadClipPreview(storage, thermal, settings, viewportWidth, viewportHeight);
          }
          showStatus("Clip deleted");
        } else {
          showStatus("Delete failed");
        }
      } else if (storage.deleteCapture(captureBrowserBasePath_)) {
        hasCapturePreview_ = false;
        releaseCaptureThumbnail();
        if (captureBrowserCount_ > 0) {
          --captureBrowserCount_;
        }
        if (captureBrowserCount_ == 0) {
          captureBrowserIndex_ = 0;
          captureBrowserBasePath_[0] = '\0';
        } else {
          if (captureBrowserIndex_ >= captureBrowserCount_) {
            captureBrowserIndex_ = captureBrowserCount_ - 1;
          }
          storage.captureBaseAt(settings.savePath,
                                captureBrowserIndex_,
                                captureBrowserBasePath_,
                                sizeof(captureBrowserBasePath_));
          loadBrowserPreview(storage, viewportWidth, viewportHeight);
        }
        showStatus("Capture deleted");
      } else {
        showStatus("Delete failed");
      }
      ignoreTouchUntilMs_ = now + 180;
      return true;
    case Action::SetupScrollUp:
      pendingSoundEvent_ = SoundEvent::Scroll;
      setupScrollY_ -= 44;
      if (setupScrollY_ < 0) {
        setupScrollY_ = 0;
      }
      showStatus("Setup scroll up");
      return true;
    case Action::SetupScrollDown:
      pendingSoundEvent_ = SoundEvent::Scroll;
      setupScrollY_ += 44;
      {
        const int16_t maxSetupScroll = settings.landscape ? kLandscapeSetupMaxScroll : kPortraitSetupMaxScroll;
        if (setupScrollY_ > maxSetupScroll) {
          setupScrollY_ = maxSetupScroll;
        }
      }
      showStatus("Setup scroll down");
      return true;
    case Action::Setup:
      pendingSoundEvent_ = SoundEvent::Click;
      playbackActive_ = false;
      storage.closeThermalClipPlayback();
      releasePlaybackClipFrame();
      setupDraftSettings_ = settings;
      setupActive_ = true;
      setupScrollY_ = 0;
      setupDragActive_ = false;
      setupDragMoved_ = false;
      setupPendingAction_ = Action::None;
      ignoreSetupTouchUntilMs_ = now + 300;
      showStatus("Setup opened");
      return true;
    case Action::HotColdDetails:
      pendingSoundEvent_ = SoundEvent::Click;
      settings.showHotColdDetails = !settings.showHotColdDetails;
      ignoreTouchUntilMs_ = now + 350;
      showStatus(settings.showHotColdDetails ? "High/low on" : "High/low off");
      return true;
    case Action::CenterTemperature:
      pendingSoundEvent_ = SoundEvent::Click;
      settings.showCenterTemperature = !settings.showCenterTemperature;
      ignoreTouchUntilMs_ = now + 350;
      showStatus(settings.showCenterTemperature ? "Center temp on" : "Center temp off");
      return true;
    case Action::ClearCustomMarkers:
      pendingSoundEvent_ = SoundEvent::Click;
      clearCustomMarkers();
      ignoreTouchUntilMs_ = now + 250;
      showStatus("Custom markers cleared");
      return true;
    case Action::Sound:
      settings.soundEnabled = !settings.soundEnabled;
      pendingSoundEvent_ = SoundEvent::Click;
      ignoreTouchUntilMs_ = now + 250;
      showStatus(settings.soundEnabled ? "Sound on" : "Sound off");
      return true;
    case Action::SoftPower:
      pendingSoundEvent_ = SoundEvent::Click;
      softPowerRequest_ = true;
      ignoreTouchUntilMs_ = now + 600;
      showStatus("Powering down...");
      return true;
    case Action::SetupCancel:
      pendingSoundEvent_ = SoundEvent::Click;
      setupActive_ = false;
      setupControlHeld_ = false;
      ignoreTouchUntilMs_ = now + 350;
      showStatus("Setup canceled");
      return true;
    case Action::SetupSave:
      pendingSoundEvent_ = SoundEvent::Click;
      if (storage.validateSavePath(setupDraftSettings_.savePath)) {
        settings = setupDraftSettings_;
        setupActive_ = false;
        setupControlHeld_ = false;
        ignoreTouchUntilMs_ = now + 350;
        showStatus("Setup saved");
      } else {
        showStatus("Invalid path");
      }
      return true;
    case Action::SetupPath:
      pendingSoundEvent_ = SoundEvent::Click;
      cyclePath(setupDraftSettings_);
      showStatus(setupDraftSettings_.savePath);
      return true;
    case Action::SetupIdle:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleIdleSleep(setupDraftSettings_);
      {
        char message[40] = {};
        if (setupDraftSettings_.inactivitySleepSeconds == 0) {
          snprintf(message, sizeof(message), "Idle sleep off");
        } else {
          snprintf(message, sizeof(message), "Idle sleep %us", setupDraftSettings_.inactivitySleepSeconds);
        }
        showStatus(message);
      }
      return true;
    case Action::SetupIdleDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseIdleSleep(setupDraftSettings_);
      {
        char message[40] = {};
        if (setupDraftSettings_.inactivitySleepSeconds == 0) {
          snprintf(message, sizeof(message), "Idle sleep off");
        } else {
          snprintf(message, sizeof(message), "Idle sleep %us", setupDraftSettings_.inactivitySleepSeconds);
        }
        showStatus(message);
      }
      return true;
    case Action::SetupTempOffset:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleTemperatureOffset(setupDraftSettings_);
      {
        char offsetText[16] = {};
        char message[40] = {};
        formatOffset(offsetText, sizeof(offsetText), setupDraftSettings_.temperatureOffsetTenths);
        snprintf(message, sizeof(message), "Temp offset %s", offsetText);
        showStatus(message);
      }
      return true;
    case Action::SetupTempOffsetDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseTemperatureOffset(setupDraftSettings_);
      {
        char offsetText[16] = {};
        char message[40] = {};
        formatOffset(offsetText, sizeof(offsetText), setupDraftSettings_.temperatureOffsetTenths);
        snprintf(message, sizeof(message), "Temp offset %s", offsetText);
        showStatus(message);
      }
      return true;
    case Action::SetupAutoRotate:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleAutoRotate(setupDraftSettings_);
      showStatus(setupDraftSettings_.autoRotate ? "Auto rotate on" : "Auto rotate off");
      return true;
    case Action::SetupOrientation:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleManualOrientation(setupDraftSettings_);
      showStatus(setupDraftSettings_.landscape ? "Manual landscape" : "Manual portrait");
      return true;
    case Action::SetupFilename:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleFilenameFooter(setupDraftSettings_);
      showStatus(setupDraftSettings_.includeFilenameInCapture ? "Capture name on" : "Capture name off");
      return true;
    case Action::SetupRaw:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleRawCapture(setupDraftSettings_);
      showStatus(setupDraftSettings_.saveRawCapture ? "Raw save on" : "Raw save off");
      return true;
    case Action::SetupAutoFfc:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleAutoFfc(setupDraftSettings_);
      showStatus(setupDraftSettings_.autoFfcEnabled ? "Auto FFC on" : "Auto FFC off");
      return true;
    case Action::SetupClipDuration:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleClipDuration(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Clip %us", setupDraftSettings_.clipDurationSeconds);
        showStatus(message);
      }
      return true;
    case Action::SetupClipDurationDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseClipDuration(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Clip %us", setupDraftSettings_.clipDurationSeconds);
        showStatus(message);
      }
      return true;
    case Action::SetupSoundVolume:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleSoundVolume(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Volume %u%%", setupDraftSettings_.soundVolume);
        showStatus(message);
      }
      return true;
    case Action::SetupSoundVolumeDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseSoundVolume(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Volume %u%%", setupDraftSettings_.soundVolume);
        showStatus(message);
      }
      return true;
    case Action::None:
    default:
      if (!playbackActive_) {
        const uint16_t viewX = 0;
        const uint16_t viewY = settings.landscape ? 48 : 52;
        if (contains({static_cast<int16_t>(viewX), static_cast<int16_t>(viewY), viewportWidth, viewportHeight},
                     touch.x,
                     touch.y)) {
          addCustomMarker(touch.x - viewX, touch.y - viewY, viewportWidth, viewportHeight);
          pendingSoundEvent_ = SoundEvent::Click;
          ignoreTouchUntilMs_ = now + 220;
          char message[40] = {};
          snprintf(message, sizeof(message), "Marker %u set", static_cast<unsigned>(customMarkerCount_));
          showStatus(message);
          return true;
        }
      }
      pendingSoundEvent_ = SoundEvent::Alert;
      return false;
  }
}

ThermalUi::Action ThermalUi::hitTest(uint16_t x, uint16_t y, bool landscape) const {
  const uint16_t screenW = landscape ? 480 : 320;
  const uint16_t screenH = landscape ? 320 : 480;

  if (playbackActive_) {
    const uint16_t panelX = 0;
    const uint16_t panelW = screenW - (panelX * 2);
    const uint16_t bottomY = screenH - 46;
    if (contains({static_cast<int16_t>(panelX + 128), 4, 48, 36}, x, y)) return Action::PlaybackImageTab;
    if (contains({static_cast<int16_t>(panelX + 182), 4, 48, 36}, x, y)) return Action::PlaybackClipTab;
    if (contains({static_cast<int16_t>(panelX + 4), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackPrev;
    if (contains({static_cast<int16_t>(panelX + 62), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackNext;
    if (playbackClipMode_ &&
        contains({static_cast<int16_t>(panelX + panelW / 2 - 31), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackPlayPause;
    if (contains({static_cast<int16_t>(panelX + panelW - 126), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackDelete;
    if (contains({static_cast<int16_t>(panelX + panelW - 68), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackClose;
    return Action::None;
  }

  if (setupActive_) {
    const uint16_t panelX = landscape ? 40 : 20;
    const uint16_t panelY = landscape ? 34 : 56;
    const uint16_t panelW = screenW - (panelX * 2);
    const uint16_t panelH = landscape ? 280 : 368;
    const int16_t maxScroll = landscape ? kLandscapeSetupMaxScroll : kPortraitSetupMaxScroll;
    if (contains({static_cast<int16_t>(panelX), static_cast<int16_t>(panelY), panelW, panelH}, x, y)) {
      if (contains({static_cast<int16_t>(panelX + 12), static_cast<int16_t>(panelY + panelH - 42), 112, 36}, x, y)) return Action::SetupCancel;
      if (contains({static_cast<int16_t>(panelX + panelW - 124), static_cast<int16_t>(panelY + panelH - 42), 112, 36}, x, y)) return Action::SetupSave;
      if (maxScroll > 0 && setupScrollY_ > 0 &&
          contains({static_cast<int16_t>(panelX + panelW - 38), static_cast<int16_t>(panelY + 38), 38, 38}, x, y)) {
        return Action::SetupScrollUp;
      }
      if (maxScroll > 0 && setupScrollY_ < maxScroll &&
          contains({static_cast<int16_t>(panelX + panelW - 38), static_cast<int16_t>(panelY + panelH - 84), 38, 42}, x, y)) {
        return Action::SetupScrollDown;
      }
      const uint16_t contentY = static_cast<uint16_t>(y + setupScrollY_);
      if (landscape) {
        const uint16_t leftFieldX = panelX + 66;
        const uint16_t rightFieldX = panelX + 252;
        const uint16_t fieldW = 84;
        const uint16_t hitW = fieldW + 16;
        const uint16_t stepZoneW = fieldW / 3;
        if (contains({static_cast<int16_t>(leftFieldX - 8), static_cast<int16_t>(panelY + 78), hitW, 34}, x, contentY)) return Action::SetupPath;
        if (contains({static_cast<int16_t>(leftFieldX - 8), static_cast<int16_t>(panelY + 116), hitW, 34}, x, contentY)) {
          if (x < leftFieldX + stepZoneW) return Action::SetupIdleDown;
          if (x >= leftFieldX + fieldW - stepZoneW) return Action::SetupIdle;
          return Action::None;
        }
        if (contains({static_cast<int16_t>(leftFieldX - 8), static_cast<int16_t>(panelY + 154), hitW, 34}, x, contentY)) {
          if (x < leftFieldX + stepZoneW) return Action::SetupTempOffsetDown;
          if (x >= leftFieldX + fieldW - stepZoneW) return Action::SetupTempOffset;
          return Action::None;
        }
        if (contains({static_cast<int16_t>(leftFieldX - 8), static_cast<int16_t>(panelY + 192), hitW, 34}, x, contentY)) {
          if (x < leftFieldX + stepZoneW) return Action::SetupSoundVolumeDown;
          if (x >= leftFieldX + fieldW - stepZoneW) return Action::SetupSoundVolume;
          return Action::None;
        }
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 78), hitW, 34}, x, contentY)) return Action::SetupAutoRotate;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 116), hitW, 34}, x, contentY)) return Action::SetupOrientation;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 154), hitW, 34}, x, contentY)) return Action::SetupFilename;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 192), hitW, 34}, x, contentY)) return Action::SetupRaw;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 230), hitW, 34}, x, contentY)) return Action::SetupAutoFfc;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 268), hitW, 34}, x, contentY)) {
          if (x < rightFieldX + stepZoneW) return Action::SetupClipDurationDown;
          if (x >= rightFieldX + fieldW - stepZoneW) return Action::SetupClipDuration;
          return Action::None;
        }
      } else {
        const uint16_t fieldX = panelX + 108;
        const uint16_t fieldW = panelW - 198;
        const uint16_t hitW = fieldW + 16;
        const uint16_t stepZoneW = fieldW / 3;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 82), hitW, 32}, x, contentY)) return Action::SetupPath;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 116), hitW, 32}, x, contentY)) return Action::SetupAutoRotate;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 150), hitW, 32}, x, contentY)) return Action::SetupOrientation;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 184), hitW, 32}, x, contentY)) {
          if (x < fieldX + stepZoneW) return Action::SetupIdleDown;
          if (x >= fieldX + fieldW - stepZoneW) return Action::SetupIdle;
          return Action::None;
        }
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 218), hitW, 32}, x, contentY)) {
          if (x < fieldX + stepZoneW) return Action::SetupTempOffsetDown;
          if (x >= fieldX + fieldW - stepZoneW) return Action::SetupTempOffset;
          return Action::None;
        }
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 252), hitW, 32}, x, contentY)) return Action::SetupFilename;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 286), hitW, 32}, x, contentY)) return Action::SetupRaw;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 320), hitW, 32}, x, contentY)) return Action::SetupAutoFfc;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 354), hitW, 32}, x, contentY)) {
          if (x < fieldX + stepZoneW) return Action::SetupClipDurationDown;
          if (x >= fieldX + fieldW - stepZoneW) return Action::SetupClipDuration;
          return Action::None;
        }
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 388), hitW, 32}, x, contentY)) {
          if (x < fieldX + stepZoneW) return Action::SetupSoundVolumeDown;
          if (x >= fieldX + fieldW - stepZoneW) return Action::SetupSoundVolume;
          return Action::None;
        }
      }
    }
    return Action::None;
  }

  const uint16_t orientW = orientationWidth(landscape);
  const int16_t orientX = 8;
  const int16_t hotColdX = orientX + orientW + 8;
  if (contains({static_cast<int16_t>(hotColdX - 4), 4, 32, 36}, x, y)) {
    return Action::HotColdDetails;
  }
  const int16_t centerX = hotColdX + 34;
  if (contains({static_cast<int16_t>(centerX - 4), 4, 32, 36}, x, y)) {
    return Action::CenterTemperature;
  }
  const int16_t clearMarkersX = centerX + 34;
  if (contains({static_cast<int16_t>(clearMarkersX - 4), 4, 32, 36}, x, y)) {
    return Action::ClearCustomMarkers;
  }
  const int16_t soundX = clearMarkersX + 34;
  if (contains({static_cast<int16_t>(soundX - 4), 4, 32, 36}, x, y)) {
    return Action::Sound;
  }

  const uint16_t buttonY = screenH - (landscape ? 52 : 54);
  const uint16_t buttonW = landscape ? 42 : 32;
  const uint16_t gap = landscape ? 8 : 7;
  if (contains({static_cast<int16_t>(screenW - buttonW - 8), static_cast<int16_t>(buttonY - 10), static_cast<uint16_t>(buttonW + 8), 52}, x, y)) {
    return Action::SoftPower;
  }
  const uint16_t mainButtonCount = 6;
  const uint16_t reservedPowerGap = landscape ? 0 : buttonW;
  const uint16_t totalButtonW = mainButtonCount * buttonW + (mainButtonCount - 1) * gap + reservedPowerGap;
  const uint16_t startX = totalButtonW < screenW ? (screenW - totalButtonW) / 2 : 0;
  const uint16_t hitW = buttonW + gap;
  const uint16_t hitH = 52;
  if (contains({static_cast<int16_t>(startX - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Palette;
  }
  if (contains({static_cast<int16_t>(startX + buttonW + gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Noise;
  }
  if (contains({static_cast<int16_t>(startX + 2 * (buttonW + gap) - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Capture;
  }
  if (contains({static_cast<int16_t>(startX + 3 * (buttonW + gap) - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::VideoClip;
  }
  if (contains({static_cast<int16_t>(startX + 4 * (buttonW + gap) - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Playback;
  }
  if (contains({static_cast<int16_t>(startX + 5 * (buttonW + gap) - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Setup;
  }
  return Action::None;
}

void ThermalUi::cyclePalette(AppSettings& settings) {
  switch (palette_) {
    case PaletteMode::Ironbow:
      palette_ = PaletteMode::WhiteHot;
      break;
    case PaletteMode::WhiteHot:
      palette_ = PaletteMode::BlackHot;
      break;
    case PaletteMode::BlackHot:
      palette_ = PaletteMode::Histogram;
      break;
    case PaletteMode::Histogram:
      palette_ = PaletteMode::Lava;
      break;
    case PaletteMode::Lava:
      palette_ = PaletteMode::HotIron;
      break;
    case PaletteMode::HotIron:
      palette_ = PaletteMode::Medical;
      break;
    case PaletteMode::Medical:
      palette_ = PaletteMode::Arctic;
      break;
    case PaletteMode::Arctic:
      palette_ = PaletteMode::Rainbow;
      break;
    case PaletteMode::Rainbow:
      palette_ = PaletteMode::RedHot;
      break;
    case PaletteMode::RedHot:
    default:
      palette_ = PaletteMode::Ironbow;
      break;
  }
  settings.paletteMode = paletteToIndex(palette_);
}

void ThermalUi::cycleImageQuality(AppSettings& settings) {
  settings.imageQualityMode = settings.imageQualityMode >= 2 ? 0 : settings.imageQualityMode + 1;
}

void ThermalUi::cyclePath(AppSettings& settings) {
  if (strcmp(settings.savePath, "/flir") == 0) {
    strncpy(settings.savePath, "/flir/captures", sizeof(settings.savePath) - 1);
  } else if (strcmp(settings.savePath, "/flir/captures") == 0) {
    strncpy(settings.savePath, "/thermal", sizeof(settings.savePath) - 1);
  } else {
    strncpy(settings.savePath, "/flir", sizeof(settings.savePath) - 1);
  }
  settings.savePath[sizeof(settings.savePath) - 1] = '\0';
}

void ThermalUi::cycleIdleSleep(AppSettings& settings) {
  switch (settings.inactivitySleepSeconds) {
    case 0:
      settings.inactivitySleepSeconds = 30;
      break;
    case 30:
      settings.inactivitySleepSeconds = 60;
      break;
    case 60:
      settings.inactivitySleepSeconds = 120;
      break;
    case 120:
      settings.inactivitySleepSeconds = 300;
      break;
    case 300:
      settings.inactivitySleepSeconds = 600;
      break;
    default:
      settings.inactivitySleepSeconds = 0;
      break;
  }
}

void ThermalUi::decreaseIdleSleep(AppSettings& settings) {
  switch (settings.inactivitySleepSeconds) {
    case 0:
      settings.inactivitySleepSeconds = 600;
      break;
    case 30:
      settings.inactivitySleepSeconds = 0;
      break;
    case 60:
      settings.inactivitySleepSeconds = 30;
      break;
    case 120:
      settings.inactivitySleepSeconds = 60;
      break;
    case 300:
      settings.inactivitySleepSeconds = 120;
      break;
    case 600:
      settings.inactivitySleepSeconds = 300;
      break;
    default:
      settings.inactivitySleepSeconds = 0;
      break;
  }
}

void ThermalUi::cycleTemperatureOffset(AppSettings& settings) {
  if (settings.temperatureOffsetTenths < -50 || settings.temperatureOffsetTenths > 50) {
    settings.temperatureOffsetTenths = 0;
    return;
  }
  if (settings.temperatureOffsetTenths >= 50) {
    return;
  }
  settings.temperatureOffsetTenths += 5;
}

void ThermalUi::decreaseTemperatureOffset(AppSettings& settings) {
  if (settings.temperatureOffsetTenths < -50 || settings.temperatureOffsetTenths > 50) {
    settings.temperatureOffsetTenths = 0;
    return;
  }
  if (settings.temperatureOffsetTenths <= -50) {
    return;
  }
  settings.temperatureOffsetTenths -= 5;
}

void ThermalUi::toggleFilenameFooter(AppSettings& settings) {
  settings.includeFilenameInCapture = !settings.includeFilenameInCapture;
}

void ThermalUi::toggleRawCapture(AppSettings& settings) {
  settings.saveRawCapture = !settings.saveRawCapture;
}

void ThermalUi::toggleAutoFfc(AppSettings& settings) {
  settings.autoFfcEnabled = !settings.autoFfcEnabled;
}

void ThermalUi::cycleClipDuration(AppSettings& settings) {
  static constexpr uint8_t kDurations[] = {3, 5, 10, 15, 20};
  for (uint8_t i = 0; i < sizeof(kDurations); ++i) {
    if (settings.clipDurationSeconds == kDurations[i]) {
      settings.clipDurationSeconds = kDurations[(i + 1) % sizeof(kDurations)];
      return;
    }
  }
  settings.clipDurationSeconds = 3;
}

void ThermalUi::decreaseClipDuration(AppSettings& settings) {
  static constexpr uint8_t kDurations[] = {3, 5, 10, 15, 20};
  for (uint8_t i = 0; i < sizeof(kDurations); ++i) {
    if (settings.clipDurationSeconds == kDurations[i]) {
      settings.clipDurationSeconds = kDurations[i == 0 ? sizeof(kDurations) - 1 : i - 1];
      return;
    }
  }
  settings.clipDurationSeconds = 3;
}

void ThermalUi::cycleSoundVolume(AppSettings& settings) {
  static constexpr uint8_t kVolumes[] = {0, 25, 50, 75, 80, 90, 100};
  for (uint8_t i = 0; i < sizeof(kVolumes); ++i) {
    if (settings.soundVolume == kVolumes[i]) {
      settings.soundVolume = kVolumes[(i + 1) % sizeof(kVolumes)];
      return;
    }
  }
  settings.soundVolume = 80;
}

void ThermalUi::decreaseSoundVolume(AppSettings& settings) {
  static constexpr uint8_t kVolumes[] = {0, 25, 50, 75, 80, 90, 100};
  for (uint8_t i = 0; i < sizeof(kVolumes); ++i) {
    if (settings.soundVolume == kVolumes[i]) {
      settings.soundVolume = kVolumes[i == 0 ? sizeof(kVolumes) - 1 : i - 1];
      return;
    }
  }
  settings.soundVolume = 80;
}

void ThermalUi::toggleAutoRotate(AppSettings& settings) {
  settings.autoRotate = !settings.autoRotate;
}

void ThermalUi::toggleManualOrientation(AppSettings& settings) {
  if (!settings.autoRotate) {
    settings.landscape = !settings.landscape;
    settings.displayRotation = settings.landscape ? 1 : 0;
  }
}

int16_t ThermalUi::maxSetupScroll(bool landscape) const {
  return landscape ? kLandscapeSetupMaxScroll : kPortraitSetupMaxScroll;
}

bool ThermalUi::executeSetupAction(Action action,
                                   AppSettings& settings,
                                   CaptureStorage& storage) {
  const uint32_t now = millis();
  switch (action) {
    case Action::SetupScrollUp:
      pendingSoundEvent_ = SoundEvent::Scroll;
      setupScrollY_ -= 44;
      if (setupScrollY_ < 0) {
        setupScrollY_ = 0;
      }
      showStatus("Setup scroll up");
      return true;
    case Action::SetupScrollDown:
      pendingSoundEvent_ = SoundEvent::Scroll;
      setupScrollY_ += 44;
      {
        const int16_t maxScroll = maxSetupScroll(settings.landscape);
        if (setupScrollY_ > maxScroll) {
          setupScrollY_ = maxScroll;
        }
      }
      showStatus("Setup scroll down");
      return true;
    case Action::SetupCancel:
      pendingSoundEvent_ = SoundEvent::Click;
      setupActive_ = false;
      ignoreTouchUntilMs_ = now + 350;
      showStatus("Setup canceled");
      return true;
    case Action::SetupSave:
      pendingSoundEvent_ = SoundEvent::Click;
      if (storage.validateSavePath(setupDraftSettings_.savePath)) {
        settings = setupDraftSettings_;
        setupActive_ = false;
        ignoreTouchUntilMs_ = now + 350;
        showStatus("Setup saved");
      } else {
        showStatus("Invalid path");
      }
      return true;
    case Action::SetupPath:
      pendingSoundEvent_ = SoundEvent::Click;
      cyclePath(setupDraftSettings_);
      showStatus(setupDraftSettings_.savePath);
      return true;
    case Action::SetupIdle:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleIdleSleep(setupDraftSettings_);
      {
        char message[40] = {};
        if (setupDraftSettings_.inactivitySleepSeconds == 0) {
          snprintf(message, sizeof(message), "Idle sleep off");
        } else {
          snprintf(message, sizeof(message), "Idle sleep %us", setupDraftSettings_.inactivitySleepSeconds);
        }
        showStatus(message);
      }
      return true;
    case Action::SetupIdleDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseIdleSleep(setupDraftSettings_);
      {
        char message[40] = {};
        if (setupDraftSettings_.inactivitySleepSeconds == 0) {
          snprintf(message, sizeof(message), "Idle sleep off");
        } else {
          snprintf(message, sizeof(message), "Idle sleep %us", setupDraftSettings_.inactivitySleepSeconds);
        }
        showStatus(message);
      }
      return true;
    case Action::SetupTempOffset:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleTemperatureOffset(setupDraftSettings_);
      {
        char offsetText[16] = {};
        char message[40] = {};
        formatOffset(offsetText, sizeof(offsetText), setupDraftSettings_.temperatureOffsetTenths);
        snprintf(message, sizeof(message), "Temp offset %s", offsetText);
        showStatus(message);
      }
      return true;
    case Action::SetupTempOffsetDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseTemperatureOffset(setupDraftSettings_);
      {
        char offsetText[16] = {};
        char message[40] = {};
        formatOffset(offsetText, sizeof(offsetText), setupDraftSettings_.temperatureOffsetTenths);
        snprintf(message, sizeof(message), "Temp offset %s", offsetText);
        showStatus(message);
      }
      return true;
    case Action::SetupAutoRotate:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleAutoRotate(setupDraftSettings_);
      showStatus(setupDraftSettings_.autoRotate ? "Auto rotate on" : "Auto rotate off");
      return true;
    case Action::SetupOrientation:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleManualOrientation(setupDraftSettings_);
      showStatus(setupDraftSettings_.landscape ? "Manual landscape" : "Manual portrait");
      return true;
    case Action::SetupFilename:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleFilenameFooter(setupDraftSettings_);
      showStatus(setupDraftSettings_.includeFilenameInCapture ? "Capture name on" : "Capture name off");
      return true;
    case Action::SetupRaw:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleRawCapture(setupDraftSettings_);
      showStatus(setupDraftSettings_.saveRawCapture ? "Raw save on" : "Raw save off");
      return true;
    case Action::SetupAutoFfc:
      pendingSoundEvent_ = SoundEvent::Click;
      toggleAutoFfc(setupDraftSettings_);
      showStatus(setupDraftSettings_.autoFfcEnabled ? "Auto FFC on" : "Auto FFC off");
      return true;
    case Action::SetupClipDuration:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleClipDuration(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Clip %us", setupDraftSettings_.clipDurationSeconds);
        showStatus(message);
      }
      return true;
    case Action::SetupClipDurationDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseClipDuration(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Clip %us", setupDraftSettings_.clipDurationSeconds);
        showStatus(message);
      }
      return true;
    case Action::SetupSoundVolume:
      pendingSoundEvent_ = SoundEvent::Click;
      cycleSoundVolume(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Volume %u%%", setupDraftSettings_.soundVolume);
        showStatus(message);
      }
      return true;
    case Action::SetupSoundVolumeDown:
      pendingSoundEvent_ = SoundEvent::Click;
      decreaseSoundVolume(setupDraftSettings_);
      {
        char message[32] = {};
        snprintf(message, sizeof(message), "Volume %u%%", setupDraftSettings_.soundVolume);
        showStatus(message);
      }
      return true;
    case Action::None:
    default:
      pendingSoundEvent_ = SoundEvent::Alert;
      return false;
  }
}

bool ThermalUi::handleSetupTouch(const TouchPoint& touch,
                                 AppSettings& settings,
                                 CaptureStorage& storage) {
  const bool landscape = settings.landscape;
  const uint16_t screenW = landscape ? 480 : 320;
  const uint16_t panelX = landscape ? 40 : 20;
  const uint16_t panelY = landscape ? 34 : 56;
  const uint16_t panelW = screenW - (panelX * 2);
  const uint16_t panelH = landscape ? 280 : 368;
  const Rect scrollArea{
      static_cast<int16_t>(panelX + 4),
      static_cast<int16_t>(panelY + 56),
      static_cast<uint16_t>(panelW - 8),
      static_cast<uint16_t>(panelH - 116),
  };
  const uint32_t now = millis();

  if (!touch.pressed) {
    const Action pending = setupPendingAction_;
    const bool dragged = setupDragMoved_;
    setupDragActive_ = false;
    setupDragMoved_ = false;
    setupControlHeld_ = false;
    setupPendingAction_ = Action::None;
    if (!dragged && pending != Action::None && now >= ignoreSetupTouchUntilMs_) {
      ignoreSetupTouchUntilMs_ = now + 80;
      return executeSetupAction(pending, settings, storage);
    }
    return false;
  }

  if (now < ignoreSetupTouchUntilMs_) {
    return true;
  }

  const Action immediateAction = hitTest(touch.x, touch.y, landscape);
  if (immediateAction == Action::SetupScrollUp || immediateAction == Action::SetupScrollDown) {
    setupDragActive_ = false;
    setupDragMoved_ = false;
    setupPendingAction_ = Action::None;
    ignoreSetupTouchUntilMs_ = now + 110;
    return executeSetupAction(immediateAction, settings, storage);
  }

  if (!setupDragActive_) {
    setupDragActive_ = true;
    setupDragMoved_ = false;
    setupControlHeld_ = false;
    setupPendingAction_ = immediateAction;
    setupDragStartY_ = touch.y;
    setupDragLastY_ = touch.y;
    setupDragLastMs_ = now;
    return true;
  }

  const bool inScrollArea = contains(scrollArea, touch.x, touch.y);

  const int16_t dy = static_cast<int16_t>(touch.y) - static_cast<int16_t>(setupDragLastY_);
  const int16_t totalDy = static_cast<int16_t>(touch.y) - static_cast<int16_t>(setupDragStartY_);
  if (!inScrollArea || abs(totalDy) < 3 || now - setupDragLastMs_ < 6) {
    return true;
  }

  setupDragMoved_ = true;
  setupPendingAction_ = Action::None;
  setupDragLastY_ = touch.y;
  setupDragLastMs_ = now;
  pendingSoundEvent_ = SoundEvent::Scroll;
  setupScrollY_ -= dy;
  const int16_t maxScroll = maxSetupScroll(landscape);
  if (setupScrollY_ < 0) {
    setupScrollY_ = 0;
  }
  if (setupScrollY_ > maxScroll) {
    setupScrollY_ = maxScroll;
  }
  return true;
}

bool ThermalUi::handleSetupDrag(const TouchPoint& touch, bool landscape) {
  const uint16_t screenW = landscape ? 480 : 320;
  const uint16_t panelX = landscape ? 40 : 20;
  const uint16_t panelY = landscape ? 34 : 56;
  const uint16_t panelW = screenW - (panelX * 2);
  const uint16_t panelH = landscape ? 280 : 368;
  const uint16_t scrollGutterW = 46;
  const Rect fieldArea{
      static_cast<int16_t>(panelX + 4),
      static_cast<int16_t>(panelY + 56),
      static_cast<uint16_t>(panelW - scrollGutterW - 8),
      static_cast<uint16_t>(panelH - 100),
  };
  const uint32_t now = millis();
  if (!setupDragActive_) {
    setupDragActive_ = contains(fieldArea, touch.x, touch.y);
    setupDragMoved_ = false;
    setupDragStartY_ = touch.y;
    setupDragLastY_ = touch.y;
    setupDragLastMs_ = now;
    return false;
  }

  if (!contains(fieldArea, touch.x, touch.y)) {
    setupDragActive_ = false;
    return false;
  }
  const int16_t dy = static_cast<int16_t>(touch.y) - static_cast<int16_t>(setupDragLastY_);
  const int16_t totalDy = static_cast<int16_t>(touch.y) - static_cast<int16_t>(setupDragStartY_);
  if (abs(totalDy) < 8 || now - setupDragLastMs_ < 20) {
    return false;
  }
  setupDragMoved_ = true;
  setupDragLastY_ = touch.y;
  setupDragLastMs_ = now;
  setupScrollY_ -= dy;
  const int16_t maxScroll = maxSetupScroll(landscape);
  if (setupScrollY_ < 0) {
    setupScrollY_ = 0;
  }
  if (setupScrollY_ > maxScroll) {
    setupScrollY_ = maxScroll;
  }
  return true;
}

bool ThermalUi::handlePinchZoom(AppSettings& settings, const TouchPoint& touch) {
  const uint16_t dx = touch.x > touch.x2 ? touch.x - touch.x2 : touch.x2 - touch.x;
  const uint16_t dy = touch.y > touch.y2 ? touch.y - touch.y2 : touch.y2 - touch.y;
  const uint16_t distance = dx + dy;
  if (!pinchActive_) {
    pinchActive_ = true;
    pinchChanged_ = false;
    pinchStartZoomed_ = zoomed_;
    pinchStartDistance_ = distance;
    return true;
  }

  const int16_t delta = static_cast<int16_t>(distance) - static_cast<int16_t>(pinchStartDistance_);
  if (!pinchChanged_ && delta > 22 && !pinchStartZoomed_) {
    zoomed_ = true;
    settings.zoomed = true;
    pinchChanged_ = true;
    showStatus("Pinch zoom in");
  } else if (!pinchChanged_ && delta < -22 && pinchStartZoomed_) {
    zoomed_ = false;
    settings.zoomed = false;
    pinchChanged_ = true;
    showStatus("Pinch zoom out");
  }
  return true;
}

void ThermalUi::storeCapturePreview(const uint16_t* viewportPixels,
                                    uint16_t viewportWidth,
                                    uint16_t viewportHeight) {
  if (viewportPixels == nullptr || viewportWidth == 0 || viewportHeight == 0) {
    return;
  }

  if (!ensureCapturePreviewBuffer(viewportWidth, viewportHeight)) {
    hasCapturePreview_ = false;
    showStatus("Preview memory failed");
    return;
  }

  const size_t bytes = static_cast<size_t>(viewportWidth) * viewportHeight * sizeof(uint16_t);
  memcpy(capturePreviewPixels_, viewportPixels, bytes);
  hasCapturePreview_ = true;
}

bool ThermalUi::ensureCapturePreviewBuffer(uint16_t viewportWidth, uint16_t viewportHeight) {
  if (viewportWidth == 0 || viewportHeight == 0) {
    return false;
  }

  const size_t bytes = static_cast<size_t>(viewportWidth) * viewportHeight * sizeof(uint16_t);
  if (capturePreviewPixels_ == nullptr ||
      capturePreviewWidth_ != viewportWidth ||
      capturePreviewHeight_ != viewportHeight) {
    if (capturePreviewPixels_ != nullptr) {
      heap_caps_free(capturePreviewPixels_);
      capturePreviewPixels_ = nullptr;
    }
    capturePreviewPixels_ = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    capturePreviewWidth_ = viewportWidth;
    capturePreviewHeight_ = viewportHeight;
  }

  return capturePreviewPixels_ != nullptr;
}

bool ThermalUi::ensurePlaybackClipFrame() {
  if (playbackClipFrame_ != nullptr) {
    return true;
  }
  playbackClipFrame_ = static_cast<ThermalFrame*>(
      heap_caps_malloc(sizeof(ThermalFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (playbackClipFrame_ == nullptr) {
    playbackClipFrame_ = static_cast<ThermalFrame*>(
        heap_caps_malloc(sizeof(ThermalFrame), MALLOC_CAP_8BIT));
  }
  if (playbackClipFrame_ != nullptr) {
    memset(playbackClipFrame_, 0, sizeof(ThermalFrame));
  }
  return playbackClipFrame_ != nullptr;
}

bool ThermalUi::ensureCaptureThumbnailBuffer(uint16_t thumbW, uint16_t thumbH) {
  if (thumbW == 0 || thumbH == 0) {
    return false;
  }

  if (captureThumbnailPixels_ == nullptr ||
      captureThumbnailWidth_ != thumbW ||
      captureThumbnailHeight_ != thumbH) {
    if (captureThumbnailPixels_ != nullptr) {
      heap_caps_free(captureThumbnailPixels_);
      captureThumbnailPixels_ = nullptr;
    }
    captureThumbnailPixels_ = static_cast<uint16_t*>(
        heap_caps_malloc(static_cast<size_t>(thumbW) * thumbH * sizeof(uint16_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    captureThumbnailWidth_ = thumbW;
    captureThumbnailHeight_ = thumbH;
  }
  return captureThumbnailPixels_ != nullptr;
}

void ThermalUi::releasePlaybackClipFrame() {
  if (playbackClipFrame_ != nullptr) {
    heap_caps_free(playbackClipFrame_);
    playbackClipFrame_ = nullptr;
  }
  clipPlaying_ = false;
  clipAtEnd_ = false;
  hasPlaybackClipStats_ = false;
  clipFrameIndex_ = 0;
  clipFrameCount_ = 0;
}

bool ThermalUi::loadBrowserPreview(CaptureStorage& storage, uint16_t viewportWidth, uint16_t viewportHeight) {
  hasPlaybackClipStats_ = false;
  if (captureBrowserBasePath_[0] == '\0') {
    hasCapturePreview_ = false;
    hasCaptureThumbnail_ = false;
    return false;
  }
  const bool landscapePreview = viewportWidth > viewportHeight;
  const uint16_t thumbW = landscapePreview ? kLandscapeCaptureThumbW : kPortraitCaptureThumbW;
  const uint16_t thumbH = landscapePreview ? kLandscapeCaptureThumbH : kPortraitCaptureThumbH;
  hasCaptureThumbnail_ = ensureCaptureThumbnailBuffer(thumbW, thumbH) &&
                         storage.loadCaptureBmpScaled(captureBrowserBasePath_,
                                                      captureThumbnailPixels_,
                                                      captureThumbnailWidth_,
                                                      captureThumbnailHeight_);
  hasCapturePreview_ = false;
  return hasCaptureThumbnail_;
}

bool ThermalUi::loadClipPreview(CaptureStorage& storage,
                                ThermalProcessor& thermal,
                                const AppSettings& settings,
                                uint16_t viewportWidth,
                                uint16_t viewportHeight) {
  if (captureBrowserBasePath_[0] == '\0') {
    hasCaptureThumbnail_ = false;
    clipFrameCount_ = 0;
    hasPlaybackClipStats_ = false;
    return false;
  }
  const bool landscapePreview = viewportWidth > viewportHeight;
  const uint16_t thumbW = landscapePreview ? kLandscapeCaptureThumbW : kPortraitCaptureThumbW;
  const uint16_t thumbH = landscapePreview ? kLandscapeClipThumbH : kPortraitClipThumbH;
  if (!ensureCaptureThumbnailBuffer(thumbW, thumbH)) {
    hasCaptureThumbnail_ = false;
    hasPlaybackClipStats_ = false;
    return false;
  }
  if (!ensurePlaybackClipFrame()) {
    hasCaptureThumbnail_ = false;
    hasPlaybackClipStats_ = false;
    showStatus("Clip memory failed");
    return false;
  }
  uint32_t count = 0;
  if (!storage.loadThermalClipFrame(captureBrowserBasePath_, clipFrameIndex_, *playbackClipFrame_, &count)) {
    hasCaptureThumbnail_ = false;
    clipFrameCount_ = 0;
    hasPlaybackClipStats_ = false;
    return false;
  }
  clipFrameCount_ = count;
  if (clipFrameIndex_ >= clipFrameCount_) {
    clipAtEnd_ = true;
  }
  ThermalStats clipStats;
  hasCaptureThumbnail_ = thermal.renderRgb565(*playbackClipFrame_,
                                              palette_,
                                              captureThumbnailPixels_,
                                              captureThumbnailWidth_,
                                              captureThumbnailHeight_,
                                              &clipStats,
                                              false,
                                              settings.landscape,
                                              settings.displayRotation,
                                              settings.imageQualityMode);
  if (hasCaptureThumbnail_) {
    playbackClipStats_ = clipStats;
    hasPlaybackClipStats_ = true;
  } else {
    hasPlaybackClipStats_ = false;
  }
  hasCapturePreview_ = false;
  return hasCaptureThumbnail_;
}

void ThermalUi::releaseCaptureThumbnail() {
  hasCaptureThumbnail_ = false;
  if (captureThumbnailPixels_ != nullptr) {
    heap_caps_free(captureThumbnailPixels_);
    captureThumbnailPixels_ = nullptr;
  }
  captureThumbnailWidth_ = 0;
  captureThumbnailHeight_ = 0;
}

void ThermalUi::refreshCaptureBrowser(CaptureStorage& storage, const AppSettings& settings) {
  captureBrowserCount_ = storage.captureCount(settings.savePath);
  if (captureBrowserCount_ == 0) {
    captureBrowserIndex_ = 0;
    captureBrowserBasePath_[0] = '\0';
    hasCaptureThumbnail_ = false;
    return;
  }
  if (captureBrowserIndex_ >= captureBrowserCount_) {
    captureBrowserIndex_ = captureBrowserCount_ - 1;
  }
  if (!storage.captureBaseAt(settings.savePath,
                             captureBrowserIndex_,
                             captureBrowserBasePath_,
                             sizeof(captureBrowserBasePath_))) {
    captureBrowserIndex_ = 0;
    storage.captureBaseAt(settings.savePath, 0, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
  }
}

void ThermalUi::drawPlaybackClipMarkers(DisplayDriver& display,
                                        int16_t x,
                                        int16_t y,
                                        uint16_t w,
                                        uint16_t h,
                                        const AppSettings& settings) {
  if (!playbackClipMode_ || playbackClipFrame_ == nullptr || !hasPlaybackClipStats_) {
    return;
  }

  const uint16_t markerW = playbackClipStats_.markerWidth > 0 ? playbackClipStats_.markerWidth : playbackClipFrame_->width;
  const uint16_t markerH = playbackClipStats_.markerHeight > 0 ? playbackClipStats_.markerHeight : playbackClipFrame_->height;
  if (markerW == 0 || markerH == 0) {
    return;
  }

  char tempText[16] = {};
  if (settings.showHotColdDetails) {
    const uint16_t hotX = x + static_cast<uint32_t>(playbackClipStats_.hotX) * w / markerW;
    const uint16_t hotY = y + static_cast<uint32_t>(playbackClipStats_.hotY) * h / markerH;
    const uint16_t coldX = x + static_cast<uint32_t>(playbackClipStats_.coldX) * w / markerW;
    const uint16_t coldY = y + static_cast<uint32_t>(playbackClipStats_.coldY) * h / markerH;
    display.drawRect(hotX > 5 ? hotX - 5 : hotX, hotY > 5 ? hotY - 5 : hotY, 11, 11, kHot);
    display.drawRect(coldX > 5 ? coldX - 5 : coldX, coldY > 5 ? coldY - 5 : coldY, 11, 11, kCold);
    formatTemp(tempText, sizeof(tempText), playbackClipStats_.maxC);
    drawTempLabel(display,
                  hotX + 68 < x + w ? hotX + 9 : (hotX > 68 ? hotX - 68 : x),
                  hotY > y + 18 ? hotY - 18 : hotY + 10,
                  tempText,
                  kHot);
    formatTemp(tempText, sizeof(tempText), playbackClipStats_.minC);
    drawTempLabel(display,
                  coldX + 68 < x + w ? coldX + 9 : (coldX > 68 ? coldX - 68 : x),
                  coldY > y + 18 ? coldY - 18 : coldY + 10,
                  tempText,
                  kCold);
  }

  if (settings.showCenterTemperature) {
    const uint16_t centerX = x + w / 2;
    const uint16_t centerY = y + h / 2;
    display.fillRect(centerX > 10 ? centerX - 10 : centerX, centerY, 21, 2, kText);
    display.fillRect(centerX, centerY > 10 ? centerY - 10 : centerY, 2, 21, kText);
    formatTemp(tempText, sizeof(tempText), playbackClipStats_.centerC);
    drawTempLabel(display,
                  centerX + 68 < x + w ? centerX + 11 : (centerX > 68 ? centerX - 68 : x),
                  centerY > y + 20 ? centerY - 20 : centerY + 12,
                  tempText,
                  kText);
  }

  for (uint8_t i = 0; i < customMarkerCount_; ++i) {
    if (!customMarkers_[i].active) {
      continue;
    }
    const uint16_t markerX = x + static_cast<uint32_t>(customMarkers_[i].xPermil) * w / 1000;
    const uint16_t markerY = y + static_cast<uint32_t>(customMarkers_[i].yPermil) * h / 1000;
    display.drawRect(markerX > 6 ? markerX - 6 : markerX, markerY > 6 ? markerY - 6 : markerY, 13, 13, kCustomMarker);
    display.fillRect(markerX > 11 ? markerX - 11 : markerX, markerY, 23, 2, kCustomMarker);
    display.fillRect(markerX, markerY > 11 ? markerY - 11 : markerY, 2, 23, kCustomMarker);
    char markerText[20] = {};
    formatTemp(tempText, sizeof(tempText), customMarkerTemperature(*playbackClipFrame_, settings, customMarkers_[i]));
    snprintf(markerText, sizeof(markerText), "M%u %s", static_cast<unsigned>(i + 1), tempText);
    drawTempLabel(display,
                  markerX + 82 < x + w ? markerX + 12 : (markerX > 82 ? markerX - 82 : x),
                  markerY > y + 20 ? markerY - 20 : markerY + 12,
                  markerText,
                  kCustomMarker);
  }
}

void ThermalUi::renderSetup(DisplayDriver& display, const AppSettings& settings) {
  const DisplayInfo info = display.info();
  const bool landscape = info.width > info.height;
  const uint16_t panelX = landscape ? 40 : 20;
  const uint16_t panelY = landscape ? 34 : 56;
  const uint16_t panelW = info.width - (panelX * 2);
  const uint16_t panelH = landscape ? 280 : 368;
  const int16_t maxScroll = landscape ? kLandscapeSetupMaxScroll : kPortraitSetupMaxScroll;
  if (setupScrollY_ > maxScroll) {
    setupScrollY_ = maxScroll;
  }
  display.fillRect(panelX, panelY, panelW, panelH, DisplayDriver::rgb565(245, 247, 250));
  display.drawRect(panelX, panelY, panelW, panelH, kPrimary);
  display.drawText(panelX + 14, panelY + 14, "SETUP", DisplayDriver::rgb565(15, 23, 42), 2);
  if (maxScroll > 0) {
    const int16_t arrowX = static_cast<int16_t>(panelX + panelW - 28);
    display.fillRoundRect(arrowX - 4, panelY + 42, 28, 28, 6, DisplayDriver::rgb565(226, 232, 240));
    display.drawRoundRect(arrowX - 4, panelY + 42, 28, 28, 6, DisplayDriver::rgb565(148, 163, 184));
    display.drawText(arrowX + 4, panelY + 48, "^", DisplayDriver::rgb565(15, 23, 42), 2);
    display.fillRoundRect(arrowX - 4, panelY + panelH - 78, 28, 28, 6, DisplayDriver::rgb565(226, 232, 240));
    display.drawRoundRect(arrowX - 4, panelY + panelH - 78, 28, 28, 6, DisplayDriver::rgb565(148, 163, 184));
    display.drawText(arrowX + 4, panelY + panelH - 72, "v", DisplayDriver::rgb565(15, 23, 42), 2);
  }

  char idleText[16] = {};
  if (settings.inactivitySleepSeconds == 0) {
    snprintf(idleText, sizeof(idleText), "Off");
  } else {
    snprintf(idleText, sizeof(idleText), "%u sec", settings.inactivitySleepSeconds);
  }
  char idleControlText[24] = {};
  snprintf(idleControlText, sizeof(idleControlText), "- %s +", idleText);
  char offsetText[16] = {};
  formatOffset(offsetText, sizeof(offsetText), settings.temperatureOffsetTenths);
  char offsetControlText[24] = {};
  snprintf(offsetControlText, sizeof(offsetControlText), "- %s +", offsetText);
  char clipText[16] = {};
  snprintf(clipText, sizeof(clipText), "%u sec", settings.clipDurationSeconds);
  char clipControlText[24] = {};
  snprintf(clipControlText, sizeof(clipControlText), "- %s +", clipText);
  char volumeText[16] = {};
  snprintf(volumeText, sizeof(volumeText), "%u%%", settings.soundVolume);
  char volumeControlText[24] = {};
  snprintf(volumeControlText, sizeof(volumeControlText), "- %s +", volumeText);
  const char* orientText = settings.autoRotate ? "Auto" : (settings.landscape ? "Land" : "Port");
  const int16_t rowBaseY = static_cast<int16_t>(panelY) - setupScrollY_;
  const int16_t contentTop = static_cast<int16_t>(panelY + 56);
  const int16_t contentBottom = static_cast<int16_t>(panelY + panelH - 44);
  if (maxScroll > 0) {
    const int16_t trackX = static_cast<int16_t>(panelX + panelW - 28);
    const int16_t upArrowBottom = static_cast<int16_t>(panelY + 70);
    const int16_t downArrowTop = static_cast<int16_t>(panelY + panelH - 78);
    const int16_t trackY = upArrowBottom + 8;
    const uint16_t trackH = downArrowTop > trackY + 8 ? static_cast<uint16_t>(downArrowTop - trackY - 8) : 1;
    display.fillRoundRect(trackX, trackY, 22, trackH, 6, DisplayDriver::rgb565(226, 232, 240));
    display.drawRoundRect(trackX, trackY, 22, trackH, 6, DisplayDriver::rgb565(203, 213, 225));
    const uint16_t thumbH = trackH > 60 ? trackH - 24 : trackH / 2;
    const int16_t thumbY = trackY + static_cast<int32_t>(setupScrollY_) * (trackH - thumbH) / maxScroll;
    display.fillRoundRect(trackX + 3, thumbY, 16, thumbH, 5, DisplayDriver::rgb565(100, 116, 139));
  }

  if (landscape) {
    const uint16_t leftLabelX = panelX + 16;
    const uint16_t leftFieldX = panelX + 66;
    const uint16_t rightLabelX = panelX + 212;
    const uint16_t rightFieldX = panelX + 252;
    const uint16_t fieldW = 84;
    drawSetupLabel(display, leftLabelX, rowBaseY + 88, "Path", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupButton(display, {static_cast<int16_t>(leftFieldX), static_cast<int16_t>(rowBaseY + 78), fieldW, 26}, settings.savePath, contentTop, contentBottom);
    drawSetupLabel(display, leftLabelX, rowBaseY + 126, "Idle", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(leftFieldX), static_cast<int16_t>(rowBaseY + 116), fieldW, 26}, idleText, contentTop, contentBottom);
    drawSetupLabel(display, leftLabelX, rowBaseY + 164, "Cal", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(leftFieldX), static_cast<int16_t>(rowBaseY + 154), fieldW, 26}, offsetText, contentTop, contentBottom);
    drawSetupLabel(display, rightLabelX, rowBaseY + 88, "Auto", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 78), fieldW, 26}, settings.autoRotate, contentTop, contentBottom);
    drawSetupLabel(display, rightLabelX, rowBaseY + 126, "Orient", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupButton(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 116), fieldW, 26}, orientText, contentTop, contentBottom);
    drawSetupLabel(display, rightLabelX, rowBaseY + 164, "Show Name", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 154), fieldW, 26}, settings.includeFilenameInCapture, contentTop, contentBottom);
    drawSetupLabel(display, rightLabelX, rowBaseY + 202, "Raw", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 192), fieldW, 26}, settings.saveRawCapture, contentTop, contentBottom);
    drawSetupLabel(display, rightLabelX, rowBaseY + 240, "Auto FFC", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 230), fieldW, 26}, settings.autoFfcEnabled, contentTop, contentBottom);
    drawSetupLabel(display, rightLabelX, rowBaseY + 278, "Clip", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 268), fieldW, 26}, clipText, contentTop, contentBottom);
    drawSetupLabel(display, leftLabelX, rowBaseY + 202, "Volume", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(leftFieldX), static_cast<int16_t>(rowBaseY + 192), fieldW, 26}, volumeText, contentTop, contentBottom);
  } else {
    const uint16_t labelX = panelX + 16;
    const uint16_t fieldX = panelX + 108;
    const uint16_t fieldW = panelW - 198;
    drawSetupLabel(display, labelX, rowBaseY + 92, "Path", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupButton(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 82), fieldW, 26}, settings.savePath, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 126, "Auto", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 116), fieldW, 26}, settings.autoRotate, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 160, "Orientation", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupButton(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 150), fieldW, 26}, orientText, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 194, "Idle", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 184), fieldW, 26}, idleText, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 228, "Cal", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 218), fieldW, 26}, offsetText, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 262, "Show Name", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 252), fieldW, 26}, settings.includeFilenameInCapture, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 296, "Raw", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 286), fieldW, 26}, settings.saveRawCapture, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 330, "Auto FFC", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupToggle(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 320), fieldW, 26}, settings.autoFfcEnabled, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 364, "Clip", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 354), fieldW, 26}, clipText, contentTop, contentBottom);
    drawSetupLabel(display, labelX, rowBaseY + 398, "Volume", DisplayDriver::rgb565(51, 65, 85), contentTop, contentBottom);
    drawSetupStepper(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 388), fieldW, 26}, volumeText, contentTop, contentBottom);
  }
  display.fillRect(panelX + 1, panelY + panelH - 44, panelW - 2, 43, DisplayDriver::rgb565(226, 232, 240));
  drawIconButton(display, {static_cast<int16_t>(panelX + 24), static_cast<int16_t>(panelY + panelH - 36), 44, 28}, Action::SetupCancel);
  drawIconButton(display, {static_cast<int16_t>(panelX + panelW - 68), static_cast<int16_t>(panelY + panelH - 36), 44, 28}, Action::SetupSave, true);
}

void ThermalUi::renderPlayback(DisplayDriver& display, const AppSettings& settings, CaptureStorage& storage) {
  const DisplayInfo info = display.info();
  const bool landscape = info.width > info.height;
  const uint16_t panelX = 0;
  const uint16_t panelY = 0;
  const uint16_t panelW = info.width - (panelX * 2);
  const uint16_t panelH = info.height;

  display.fillRect(panelX, panelY, panelW, panelH, DisplayDriver::rgb565(245, 247, 250));
  display.fillRect(0, panelH - 42, panelW, 42, DisplayDriver::rgb565(226, 232, 240));
  display.drawRect(panelX, panelY, panelW, panelH, kPrimary);
  display.drawText(panelX + 14, panelY + 14, "CAPTURES", DisplayDriver::rgb565(15, 23, 42), 2);
  drawIconButton(display, {static_cast<int16_t>(panelX + 128), 8, 44, 28}, Action::Capture, !playbackClipMode_);
  drawIconButton(display, {static_cast<int16_t>(panelX + 182), 8, 44, 28}, Action::VideoClip, playbackClipMode_);

  char countText[28] = {};
  if (captureBrowserCount_ == 0) {
    snprintf(countText, sizeof(countText), "No files");
  } else {
    snprintf(countText, sizeof(countText), "%u/%u", captureBrowserIndex_ + 1, captureBrowserCount_);
  }
  display.drawTextRight(panelX + panelW - 14, panelY + 8, countText, DisplayDriver::rgb565(15, 23, 42), 1);

  char fittedPath[48] = {};
  char dirLine[64] = {};
  fitText(fittedPath, sizeof(fittedPath), settings.savePath, landscape ? 122 : panelW - 32);
  snprintf(dirLine, sizeof(dirLine), "DIR %s", fittedPath);

  char usedText[16] = {};
  char totalText[16] = {};
  char freeText[16] = {};
  char sdLine[64] = {};
  if (storage.isMounted()) {
    formatBytes(usedText, sizeof(usedText), storage.usedBytes());
    formatBytes(totalText, sizeof(totalText), storage.totalBytes());
    formatBytes(freeText, sizeof(freeText), storage.freeBytes());
    snprintf(sdLine, sizeof(sdLine), "SD %s/%s  FREE %s", usedText, totalText, freeText);
  } else {
    snprintf(sdLine, sizeof(sdLine), "SD not mounted");
  }

  if (landscape) {
    display.drawText(panelX + 236, panelY + 10, dirLine, DisplayDriver::rgb565(71, 85, 105), 1);
    display.drawText(panelX + 236, panelY + 24, sdLine, DisplayDriver::rgb565(71, 85, 105), 1);
  } else {
    display.drawText(panelX + 16, panelY + 40, dirLine, DisplayDriver::rgb565(71, 85, 105), 1);
    display.drawText(panelX + 16, panelY + 52, sdLine, DisplayDriver::rgb565(71, 85, 105), 1);
  }

  const char* path = captureBrowserBasePath_[0] != '\0' ? captureBrowserBasePath_ : (playbackClipMode_ ? "No clip files" : "No capture files");
  char fittedFile[64] = {};
  fitText(fittedFile, sizeof(fittedFile), path, panelW - 32);
  const int16_t pathY = landscape ? panelY + 42 : panelY + 64;
  display.fillRect(panelX + 12, pathY - 2, panelW - 24, 12, DisplayDriver::rgb565(245, 247, 250));
  display.drawText(panelX + 16, pathY, fittedFile, DisplayDriver::rgb565(15, 23, 42), 1);

  const uint16_t thumbW = captureThumbnailWidth_ > 0 ? captureThumbnailWidth_ : (landscape ? kLandscapeCaptureThumbW : kPortraitCaptureThumbW);
  const uint16_t thumbH = captureThumbnailHeight_ > 0 ? captureThumbnailHeight_ : (landscape ? kLandscapeCaptureThumbH : kPortraitCaptureThumbH);
  const uint16_t thumbX = panelX + (panelW > thumbW ? (panelW - thumbW) / 2 : 8);
  const uint16_t thumbY = panelY + (playbackClipMode_ ? (landscape ? 72 : 94) : (landscape ? 58 : 78));
  if (playbackClipMode_) {
    const uint16_t barX = thumbX;
    const uint16_t barY = thumbY > 10 ? thumbY - 10 : thumbY;
    const uint16_t barW = thumbW;
    const uint16_t progress = clipFrameCount_ == 0
                                  ? 0
                                  : static_cast<uint32_t>(clipFrameIndex_ > clipFrameCount_ ? clipFrameCount_ : clipFrameIndex_) *
                                        barW / clipFrameCount_;
    display.fillRect(barX, barY, barW, 5, DisplayDriver::rgb565(203, 213, 225));
    display.fillRect(barX, barY, progress, 5, kPrimary);
  }
  if (hasCaptureThumbnail_ && captureThumbnailPixels_ != nullptr) {
    display.drawBitmap(thumbX, thumbY, thumbW, thumbH, captureThumbnailPixels_);
    display.drawRect(thumbX, thumbY, thumbW, thumbH, DisplayDriver::rgb565(15, 23, 42));
    drawPlaybackClipMarkers(display, thumbX, thumbY, thumbW, thumbH, settings);
    if (playbackClipMode_) {
      char elapsedText[8] = {};
      char totalText[8] = {};
      const uint32_t shownFrame = clipFrameIndex_ > clipFrameCount_ ? clipFrameCount_ : clipFrameIndex_;
      formatClipTime(elapsedText, sizeof(elapsedText), shownFrame * kClipFrameIntervalMs);
      formatClipTime(totalText, sizeof(totalText), clipFrameCount_ * kClipFrameIntervalMs);
      char durationText[20] = {};
      snprintf(durationText, sizeof(durationText), "%s / %s", elapsedText, totalText);
      const uint16_t labelW = strlen(durationText) * 6 + 12;
      const uint16_t labelH = 18;
      const int16_t labelX = thumbX + (static_cast<int16_t>(thumbW) - static_cast<int16_t>(labelW)) / 2;
      const int16_t labelY = thumbY + static_cast<int16_t>(thumbH) - labelH - 8;
      display.fillRoundRect(labelX, labelY, labelW, labelH, 6, DisplayDriver::rgb565(15, 23, 42));
      display.drawRoundRect(labelX, labelY, labelW, labelH, 6, DisplayDriver::rgb565(148, 163, 184));
      display.drawText(labelX + 6, labelY + 5, durationText, kText, 1);
    }
  } else {
    display.fillRect(thumbX, thumbY, thumbW, thumbH, DisplayDriver::rgb565(226, 232, 240));
    display.drawRect(thumbX, thumbY, thumbW, thumbH, DisplayDriver::rgb565(100, 116, 139));
    display.drawText(thumbX + 42, thumbY + 36, "No preview", DisplayDriver::rgb565(71, 85, 105), 1);
  }

  const uint16_t bottomY = panelY + panelH - 36;
  drawIconButton(display, {static_cast<int16_t>(panelX + 14), static_cast<int16_t>(bottomY), 44, 28}, Action::PlaybackPrev);
  drawIconButton(display, {static_cast<int16_t>(panelX + 70), static_cast<int16_t>(bottomY), 44, 28}, Action::PlaybackNext);
  if (playbackClipMode_) {
    drawIconButton(display, {static_cast<int16_t>(panelX + panelW / 2 - 22), static_cast<int16_t>(bottomY), 44, 28}, Action::PlaybackPlayPause, clipPlaying_);
  }
  drawIconButton(display, {static_cast<int16_t>(panelX + panelW - 114), static_cast<int16_t>(bottomY), 44, 28}, Action::PlaybackDelete);
  drawIconButton(display, {static_cast<int16_t>(panelX + panelW - 58), static_cast<int16_t>(bottomY), 44, 28}, Action::PlaybackClose, true);
}

void ThermalUi::renderWaiting(DisplayDriver& display, const AppSettings& settings, bool storageReady) {
  const DisplayInfo info = display.info();
  display.fillScreen(DisplayDriver::rgb565(15, 23, 42));
  display.fillRect(0, 0, info.width, 44, DisplayDriver::rgb565(250, 204, 21));

  const uint16_t orientW = orientationWidth(settings.landscape);
  const int16_t orientX = 8;
  drawOrientationIcon(display, orientX, 10, settings.landscape);

  const int16_t hotColdX = orientX + orientW + 8;
  drawHotColdIcon(display, hotColdX + 12, 21, settings.showHotColdDetails);
  const int16_t centerX = hotColdX + 34;
  drawCenterTempIcon(display, centerX + 12, 21, settings.showCenterTemperature);

  const int16_t clearMarkersX = centerX + 34;
  drawClearMarkersIcon(display, clearMarkersX + 12, 21, customMarkerCount_ > 0);

  const int16_t soundX = clearMarkersX + 34;
  drawSoundIcon(display, soundX + 12, 21, settings.soundEnabled);

  const int16_t zoomTextX = soundX + 36;
  display.drawText(zoomTextX, 14, zoomed_ ? "Z2X" : "Z1X", DisplayDriver::rgb565(15, 23, 42), 1);

  const int16_t waitX = zoomTextX + 34;
  display.drawText(waitX, 14, "WAIT", DisplayDriver::rgb565(15, 23, 42), 1);
  display.drawTextRight(info.width - 10, 14, storageReady ? "TF OK" : "NO TF", DisplayDriver::rgb565(15, 23, 42), 1);

  const uint16_t panelX = settings.landscape ? 54 : 24;
  const uint16_t panelY = settings.landscape ? 76 : 120;
  const uint16_t panelW = info.width - (panelX * 2);
  const uint16_t panelH = settings.landscape ? 130 : 160;
  display.fillRect(panelX, panelY, panelW, panelH, DisplayDriver::rgb565(248, 250, 252));
  display.drawRect(panelX, panelY, panelW, panelH, DisplayDriver::rgb565(14, 165, 233));
  display.drawText(panelX + 18, panelY + 24, "DISPLAY OK", DisplayDriver::rgb565(2, 132, 199), 2);
  display.drawText(panelX + 18, panelY + 58, "WAITING FOR", DisplayDriver::rgb565(15, 23, 42), 2);
  display.drawText(panelX + 18, panelY + 88, "LEPTON VOSPI", DisplayDriver::rgb565(15, 23, 42), 2);
  display.drawText(panelX + 18, panelY + panelH - 26, "Check FLIR wiring/power if this stays here.", DisplayDriver::rgb565(71, 85, 105), 1);

  const uint16_t colorY = info.height - 60;
  const uint16_t swatchW = info.width / 4;
  display.fillRect(0, colorY, swatchW, 32, DisplayDriver::rgb565(239, 68, 68));
  display.fillRect(swatchW, colorY, swatchW, 32, DisplayDriver::rgb565(34, 197, 94));
  display.fillRect(swatchW * 2, colorY, swatchW, 32, DisplayDriver::rgb565(59, 130, 246));
  display.fillRect(swatchW * 3, colorY, info.width - (swatchW * 3), 32, DisplayDriver::rgb565(255, 255, 255));
}

void ThermalUi::drawButton(DisplayDriver& display, const Rect& rect, const char* label, bool primary) {
  display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, primary ? kPrimary : kButton);
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 6, DisplayDriver::rgb565(100, 116, 139));
  const uint16_t labelWidth = strlen(label) * 6;
  const int16_t textX = rect.x + (static_cast<int16_t>(rect.w) - static_cast<int16_t>(labelWidth)) / 2;
  const int16_t textY = rect.y + (static_cast<int16_t>(rect.h) - 8) / 2;
  display.drawText(textX, textY, label, kText, 1);
}

void ThermalUi::drawToggle(DisplayDriver& display, const Rect& rect, bool enabled) {
  const uint16_t trackColor = enabled ? DisplayDriver::rgb565(34, 197, 94)
                                      : DisplayDriver::rgb565(100, 116, 139);
  const uint16_t knobColor = DisplayDriver::rgb565(248, 250, 252);
  display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, trackColor);
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, DisplayDriver::rgb565(71, 85, 105));
  const uint16_t knob = rect.h > 6 ? rect.h - 6 : rect.h;
  const int16_t knobX = enabled ? rect.x + static_cast<int16_t>(rect.w - knob - 3) : rect.x + 3;
  display.fillRoundRect(knobX, rect.y + 3, knob, knob, knob / 2, knobColor);
  display.drawRoundRect(knobX, rect.y + 3, knob, knob, knob / 2, DisplayDriver::rgb565(203, 213, 225));
  display.drawText(enabled ? rect.x + 10 : rect.x + static_cast<int16_t>(rect.w) - 28,
                   rect.y + (static_cast<int16_t>(rect.h) - 8) / 2,
                   enabled ? "ON" : "OFF",
                   kText,
                   1);
}

void ThermalUi::drawIconButton(DisplayDriver& display, const Rect& rect, Action action, bool primary) {
  display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 6, primary ? kPrimary : kButton);
  display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 6, DisplayDriver::rgb565(100, 116, 139));
  const uint16_t iconColor = action == Action::SoftPower ? DisplayDriver::rgb565(248, 113, 113) : kText;
  drawActionIcon(display,
                 rect.x + static_cast<int16_t>(rect.w / 2),
                 rect.y + static_cast<int16_t>(rect.h / 2),
                 action,
                 iconColor);
}

void ThermalUi::drawActionIcon(DisplayDriver& display, int16_t cx, int16_t cy, Action action, uint16_t color) {
  switch (action) {
    case Action::Palette:
      display.fillRect(cx - 11, cy - 7, 9, 6, DisplayDriver::rgb565(239, 68, 68));
      display.fillRect(cx + 2, cy - 7, 9, 6, DisplayDriver::rgb565(250, 204, 21));
      display.fillRect(cx - 11, cy + 1, 9, 6, DisplayDriver::rgb565(34, 197, 94));
      display.fillRect(cx + 2, cy + 1, 9, 6, DisplayDriver::rgb565(59, 130, 246));
      display.drawRect(cx - 12, cy - 8, 24, 16, color);
      break;
    case Action::Noise:
      display.drawRect(cx - 12, cy - 8, 24, 16, color);
      display.fillRect(cx - 9, cy + 4, 4, 4, DisplayDriver::rgb565(100, 116, 139));
      display.fillRect(cx - 3, cy, 4, 8, color);
      display.fillRect(cx + 3, cy - 5, 4, 13, kHot);
      display.fillRect(cx + 9, cy - 2, 2, 10, kCold);
      break;
    case Action::Capture:
      display.drawRect(cx - 13, cy - 6, 26, 15, color);
      display.fillRect(cx - 8, cy - 10, 9, 4, color);
      display.drawRect(cx - 4, cy - 2, 8, 8, color);
      display.fillRect(cx + 8, cy - 3, 2, 2, color);
      break;
    case Action::VideoClip:
      display.drawRect(cx - 13, cy - 8, 22, 16, color);
      display.fillRect(cx - 8, cy - 3, 8, 8, videoClipActive_ ? kHot : color);
      display.fillRect(cx + 10, cy - 4, 3, 8, color);
      display.fillRect(cx + 13, cy - 7, 3, 14, color);
      display.fillRect(cx - 10, cy - 11, 3, 3, color);
      display.fillRect(cx - 4, cy - 11, 3, 3, color);
      display.fillRect(cx + 2, cy - 11, 3, 3, color);
      break;
    case Action::Playback:
      display.drawRect(cx - 13, cy - 9, 26, 18, color);
      display.fillRect(cx - 9, cy - 5, 2, 10, color);
      display.fillRect(cx - 6, cy - 4, 3, 8, color);
      display.fillRect(cx - 3, cy - 2, 3, 4, color);
      display.drawRect(cx + 5, cy - 5, 7, 10, color);
      display.fillRect(cx + 7, cy + 6, 4, 3, color);
      break;
    case Action::PlaybackPrev:
      display.fillRect(cx + 5, cy - 8, 3, 16, color);
      display.fillRect(cx + 2, cy - 6, 3, 12, color);
      display.fillRect(cx - 1, cy - 4, 3, 8, color);
      display.fillRect(cx - 4, cy - 2, 3, 4, color);
      break;
    case Action::PlaybackNext:
      display.fillRect(cx - 8, cy - 8, 3, 16, color);
      display.fillRect(cx - 5, cy - 6, 3, 12, color);
      display.fillRect(cx - 2, cy - 4, 3, 8, color);
      display.fillRect(cx + 1, cy - 2, 3, 4, color);
      break;
    case Action::PlaybackPlayPause:
      if (clipAtEnd_) {
        display.drawRect(cx - 10, cy - 9, 18, 18, color);
        display.fillRect(cx + 6, cy - 11, 6, 3, color);
        display.fillRect(cx + 9, cy - 8, 3, 6, color);
        display.fillRect(cx - 4, cy - 5, 4, 14, color);
        display.fillRect(cx, cy - 3, 4, 10, color);
        display.fillRect(cx + 4, cy - 1, 4, 6, color);
      } else if (clipPlaying_) {
        display.fillRect(cx - 7, cy - 9, 5, 18, color);
        display.fillRect(cx + 3, cy - 9, 5, 18, color);
      } else {
        display.fillRect(cx - 7, cy - 9, 4, 18, color);
        display.fillRect(cx - 3, cy - 7, 4, 14, color);
        display.fillRect(cx + 1, cy - 5, 4, 10, color);
        display.fillRect(cx + 5, cy - 3, 4, 6, color);
      }
      break;
    case Action::PlaybackDelete:
      display.drawRect(cx - 8, cy - 5, 16, 13, color);
      display.fillRect(cx - 10, cy - 8, 20, 3, color);
      display.fillRect(cx - 4, cy - 11, 8, 3, color);
      display.fillRect(cx - 4, cy - 2, 2, 7, color);
      display.fillRect(cx + 2, cy - 2, 2, 7, color);
      break;
    case Action::PlaybackClose:
      display.fillRect(cx - 8, cy - 8, 3, 3, color);
      display.fillRect(cx + 5, cy - 8, 3, 3, color);
      display.fillRect(cx - 5, cy - 5, 3, 3, color);
      display.fillRect(cx + 2, cy - 5, 3, 3, color);
      display.fillRect(cx - 2, cy - 2, 4, 4, color);
      display.fillRect(cx - 5, cy + 2, 3, 3, color);
      display.fillRect(cx + 2, cy + 2, 3, 3, color);
      display.fillRect(cx - 8, cy + 5, 3, 3, color);
      display.fillRect(cx + 5, cy + 5, 3, 3, color);
      break;
    case Action::SoftPower:
      display.drawRect(cx - 8, cy - 5, 16, 12, color);
      display.fillRect(cx - 10, cy - 2, 3, 6, color);
      display.fillRect(cx + 8, cy - 2, 3, 6, color);
      display.fillRect(cx - 5, cy + 7, 10, 3, color);
      display.fillRect(cx - 1, cy - 11, 3, 11, color);
      display.fillRect(cx - 5, cy - 7, 3, 3, color);
      display.fillRect(cx + 3, cy - 7, 3, 3, color);
      break;
    case Action::SetupCancel:
      display.fillRect(cx - 9, cy - 9, 3, 3, color);
      display.fillRect(cx + 6, cy - 9, 3, 3, color);
      display.fillRect(cx - 6, cy - 6, 3, 3, color);
      display.fillRect(cx + 3, cy - 6, 3, 3, color);
      display.fillRect(cx - 3, cy - 3, 6, 6, color);
      display.fillRect(cx - 6, cy + 3, 3, 3, color);
      display.fillRect(cx + 3, cy + 3, 3, 3, color);
      display.fillRect(cx - 9, cy + 6, 3, 3, color);
      display.fillRect(cx + 6, cy + 6, 3, 3, color);
      break;
    case Action::SetupSave:
      display.fillRect(cx - 10, cy + 1, 3, 3, color);
      display.fillRect(cx - 7, cy + 4, 3, 3, color);
      display.fillRect(cx - 4, cy + 7, 3, 3, color);
      display.fillRect(cx - 1, cy + 4, 3, 3, color);
      display.fillRect(cx + 2, cy + 1, 3, 3, color);
      display.fillRect(cx + 5, cy - 2, 3, 3, color);
      display.fillRect(cx + 8, cy - 5, 3, 3, color);
      break;
    case Action::Setup:
      display.drawRect(cx - 5, cy - 5, 10, 10, color);
      display.fillRect(cx - 2, cy - 11, 4, 5, color);
      display.fillRect(cx - 2, cy + 6, 4, 5, color);
      display.fillRect(cx - 11, cy - 2, 5, 4, color);
      display.fillRect(cx + 6, cy - 2, 5, 4, color);
      display.fillRect(cx - 8, cy - 8, 4, 4, color);
      display.fillRect(cx + 4, cy - 8, 4, 4, color);
      display.fillRect(cx - 8, cy + 4, 4, 4, color);
      display.fillRect(cx + 4, cy + 4, 4, 4, color);
      display.fillRect(cx - 2, cy - 2, 4, 4, kButton);
      break;
    default:
      display.drawRect(cx - 8, cy - 8, 16, 16, color);
      break;
  }
}

void ThermalUi::drawHotColdIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled) {
  const uint16_t color = enabled ? kText : DisplayDriver::rgb565(100, 116, 139);
  display.drawRect(cx - 11, cy - 8, 8, 16, color);
  display.fillRect(cx - 9, cy + 1, 4, 5, kHot);
  display.drawRect(cx + 3, cy - 8, 8, 16, color);
  display.fillRect(cx + 5, cy + 1, 4, 5, kCold);
  display.fillRect(cx - 7, cy - 11, 2, 4, kHot);
  display.fillRect(cx + 5, cy - 11, 2, 4, kCold);
  if (!enabled) {
    display.fillRect(cx - 12, cy + 8, 24, 2, color);
  }
}

void ThermalUi::drawCenterTempIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled) {
  const uint16_t color = enabled ? kText : DisplayDriver::rgb565(100, 116, 139);
  display.drawRect(cx - 8, cy - 8, 16, 16, color);
  display.fillRect(cx - 12, cy, 7, 2, color);
  display.fillRect(cx + 6, cy, 7, 2, color);
  display.fillRect(cx, cy - 12, 2, 7, color);
  display.fillRect(cx, cy + 6, 2, 7, color);
  display.fillRect(cx - 1, cy - 1, 3, 3, color);
  if (!enabled) {
    display.fillRect(cx - 10, cy + 8, 20, 2, color);
  }
}

void ThermalUi::drawClearMarkersIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled) {
  const uint16_t color = enabled ? kCustomMarker : DisplayDriver::rgb565(100, 116, 139);
  display.drawRect(cx - 8, cy - 8, 16, 16, color);
  display.fillRect(cx - 12, cy, 7, 2, color);
  display.fillRect(cx + 6, cy, 7, 2, color);
  display.fillRect(cx, cy - 12, 2, 7, color);
  display.fillRect(cx, cy + 6, 2, 7, color);
  display.fillRect(cx - 2, cy - 2, 5, 5, color);
  display.fillRect(cx + 6, cy - 11, 3, 3, color);
  display.fillRect(cx + 3, cy - 8, 3, 3, color);
  display.fillRect(cx - 3, cy - 2, 3, 3, color);
  display.fillRect(cx - 6, cy + 1, 3, 3, color);
  display.fillRect(cx - 9, cy + 4, 3, 3, color);
}

void ThermalUi::drawSoundIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled) {
  const uint16_t color = enabled ? kText : DisplayDriver::rgb565(100, 116, 139);
  display.drawRect(cx - 11, cy - 5, 5, 10, color);
  display.fillRect(cx - 6, cy - 8, 3, 16, color);
  display.fillRect(cx - 3, cy - 5, 3, 10, color);
  if (enabled) {
    display.drawRect(cx + 3, cy - 7, 4, 14, color);
    display.drawRect(cx + 8, cy - 10, 4, 20, color);
  } else {
    display.fillRect(cx + 3, cy - 7, 3, 3, kError);
    display.fillRect(cx + 6, cy - 4, 3, 3, kError);
    display.fillRect(cx + 9, cy - 1, 3, 3, kError);
    display.fillRect(cx + 6, cy + 2, 3, 3, kError);
    display.fillRect(cx + 3, cy + 5, 3, 3, kError);
  }
}

void ThermalUi::drawOrientationIcon(DisplayDriver& display, int16_t x, int16_t y, bool landscape) {
  const uint16_t color = kText;
  const uint16_t muted = DisplayDriver::rgb565(100, 116, 139);
  const uint16_t w = landscape ? 28 : 18;
  const uint16_t h = landscape ? 18 : 28;
  const int16_t bodyX = x + (landscape ? 0 : 5);
  const int16_t bodyY = y + (landscape ? 5 : 0);

  display.drawRoundRect(bodyX, bodyY, w, h, 4, color);
  display.fillRect(bodyX + 3, bodyY + 3, w - 6, h - 6, muted);
  if (landscape) {
    display.fillRect(bodyX + w - 3, bodyY + 7, 1, 4, color);
  } else {
    display.fillRect(bodyX + 7, bodyY + h - 3, 4, 1, color);
  }
}

void ThermalUi::drawTempLabel(DisplayDriver& display, int16_t x, int16_t y, const char* text, uint16_t color) {
  const int16_t w = static_cast<int16_t>(strlen(text) * 12 + 6);
  display.fillRect(x - 3, y - 3, w, 22, kLabelBg);
  display.drawRect(x - 3, y - 3, w, 22, color);
  display.drawText(x, y, text, color, 2);
}

void ThermalUi::addCustomMarker(uint16_t localX, uint16_t localY, uint16_t viewportWidth, uint16_t viewportHeight) {
  if (viewportWidth == 0 || viewportHeight == 0) {
    return;
  }
  if (customMarkerCount_ >= 3) {
    customMarkers_[0] = customMarkers_[1];
    customMarkers_[1] = customMarkers_[2];
    customMarkerCount_ = 2;
  }
  CustomMarker& marker = customMarkers_[customMarkerCount_];
  marker.active = true;
  marker.xPermil = static_cast<uint32_t>(localX) * 1000 / viewportWidth;
  marker.yPermil = static_cast<uint32_t>(localY) * 1000 / viewportHeight;
  if (marker.xPermil > 1000) {
    marker.xPermil = 1000;
  }
  if (marker.yPermil > 1000) {
    marker.yPermil = 1000;
  }
  ++customMarkerCount_;
}

void ThermalUi::clearCustomMarkers() {
  for (CustomMarker& marker : customMarkers_) {
    marker.active = false;
    marker.xPermil = 0;
    marker.yPermil = 0;
  }
  customMarkerCount_ = 0;
}

float ThermalUi::customMarkerTemperature(const ThermalFrame& frame,
                                         const AppSettings& settings,
                                         const CustomMarker& marker) const {
  if (!marker.active || frame.width == 0 || frame.height == 0) {
    return 0.0f;
  }

  const uint8_t rotation = settings.displayRotation & 0x03;
  const uint16_t viewW = transformedWidth(frame, rotation);
  const uint16_t viewH = transformedHeight(frame, rotation);
  const uint16_t cropW = zoomed_ ? viewW / 2 : viewW;
  const uint16_t cropH = zoomed_ ? viewH / 2 : viewH;
  const uint16_t cropX0 = (viewW - cropW) / 2;
  const uint16_t cropY0 = (viewH - cropH) / 2;

  const uint16_t displayX = static_cast<uint32_t>(marker.xPermil) * viewW / 1000;
  const uint16_t displayY = static_cast<uint32_t>(marker.yPermil) * viewH / 1000;
  const uint16_t unflippedX = displayX >= viewW ? 0 : viewW - 1 - displayX;
  const uint16_t unflippedY = displayY >= viewH ? 0 : viewH - 1 - displayY;
  uint16_t logicalX = cropX0 + static_cast<uint32_t>(unflippedX) * cropW / viewW;
  uint16_t logicalY = cropY0 + static_cast<uint32_t>(unflippedY) * cropH / viewH;
  if (logicalX >= viewW) {
    logicalX = viewW - 1;
  }
  if (logicalY >= viewH) {
    logicalY = viewH - 1;
  }

  uint16_t sourceX = sourceXFromTransformed(frame, logicalX, logicalY, rotation);
  uint16_t sourceY = sourceYFromTransformed(frame, logicalX, logicalY, rotation);
  if (sourceX >= frame.width) {
    sourceX = frame.width - 1;
  }
  if (sourceY >= frame.height) {
    sourceY = frame.height - 1;
  }
  return rawToCelsius(frame.raw[sourceY * frame.width + sourceX], settings);
}

void ThermalUi::drawPaletteScale(DisplayDriver& display,
                                 int16_t x,
                                 int16_t y,
                                 uint16_t h,
                                 float maxC,
                                 float minC,
                                 bool labelsLeft) {
  const uint16_t scaleW = 16;
  for (uint16_t row = 0; row < h; ++row) {
    const uint16_t denom = h > 1 ? h - 1 : 1;
    const uint8_t t = static_cast<uint8_t>(255 - (static_cast<uint32_t>(row) * 255 / denom));
    const uint16_t color = DisplayDriver::rgb565(t, t > 128 ? 255 : t * 2, 255 - t);
    display.fillRect(x, y + row, scaleW, 1, color);
  }
  display.drawRect(x, y, scaleW, h, kText);

  char tempText[16];
  const int16_t labelX = labelsLeft ? x - 58 : x + scaleW + 4;
  formatTemp(tempText, sizeof(tempText), maxC);
  display.fillRect(labelX - 2, y, 52, 14, kLabelBg);
  display.drawText(labelX, y + 3, tempText, kText, 1);
  formatTemp(tempText, sizeof(tempText), minC);
  display.fillRect(labelX - 2, y + static_cast<int16_t>(h) - 15, 52, 14, kLabelBg);
  display.drawText(labelX, y + static_cast<int16_t>(h) - 12, tempText, kText, 1);
}

void ThermalUi::drawPaletteScaleEndLabels(DisplayDriver& display,
                                          int16_t x,
                                          int16_t y,
                                          uint16_t h,
                                          float maxC,
                                          float minC) {
  const uint16_t scaleW = 16;
  for (uint16_t row = 0; row < h; ++row) {
    const uint16_t denom = h > 1 ? h - 1 : 1;
    const uint8_t t = static_cast<uint8_t>(255 - (static_cast<uint32_t>(row) * 255 / denom));
    const uint16_t color = DisplayDriver::rgb565(t, t > 128 ? 255 : t * 2, 255 - t);
    display.fillRect(x, y + row, scaleW, 1, color);
  }
  display.drawRect(x, y, scaleW, h, kText);

  char tempText[16];
  formatTemp(tempText, sizeof(tempText), maxC);
  uint16_t labelWidth = strlen(tempText) * 6;
  int16_t labelX = x + (static_cast<int16_t>(scaleW) - static_cast<int16_t>(labelWidth)) / 2;
  display.fillRect(labelX - 2, y + 2, labelWidth + 4, 12, kLabelBg);
  display.drawText(labelX, y + 4, tempText, kText, 1);

  formatTemp(tempText, sizeof(tempText), minC);
  labelWidth = strlen(tempText) * 6;
  labelX = x + (static_cast<int16_t>(scaleW) - static_cast<int16_t>(labelWidth)) / 2;
  display.fillRect(labelX - 2, y + static_cast<int16_t>(h) - 14, labelWidth + 4, 12, kLabelBg);
  display.drawText(labelX, y + static_cast<int16_t>(h) - 12, tempText, kText, 1);
}

void ThermalUi::drawFeedback(DisplayDriver& display, bool landscape) {
  const DisplayInfo info = display.info();
  const uint16_t h = landscape ? 18 : 20;
  const int16_t y = static_cast<int16_t>(info.height - h);
  const uint16_t bg = millis() < feedbackUntilMs_ ? DisplayDriver::rgb565(15, 23, 42) : kBg;
  display.fillRect(0, y, info.width, h, bg);
  display.fillRect(0, y, info.width, 1, DisplayDriver::rgb565(51, 65, 85));
  display.drawText(8, y + 5, feedback_, kMuted, 1);
}

bool ThermalUi::contains(const Rect& rect, uint16_t x, uint16_t y) const {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}
