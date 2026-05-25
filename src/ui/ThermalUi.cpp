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
constexpr uint16_t kError = 0xF800;
constexpr uint16_t kLabelBg = 0x0000;
constexpr uint16_t kLandscapeCaptureThumbW = 464;
constexpr uint16_t kLandscapeCaptureThumbH = 226;
constexpr uint16_t kPortraitCaptureThumbW = 304;
constexpr uint16_t kPortraitCaptureThumbH = 376;

const char* paletteName(PaletteMode palette) {
  switch (palette) {
    case PaletteMode::WhiteHot:
      return "WHITE";
    case PaletteMode::BlackHot:
      return "BLACK";
    case PaletteMode::Histogram:
      return "HIST";
    case PaletteMode::Ironbow:
    default:
      return "IRON";
  }
}

uint16_t orientationWidth(bool landscape) {
  return 34;
}

uint16_t zoomWidth(bool landscape) {
  return landscape ? 70 : 56;
}

void formatTemp(char* out, size_t outSize, float temp) {
  snprintf(out, outSize, "%.1fC", temp);
}

void formatOffset(char* out, size_t outSize, int8_t offsetTenths) {
  snprintf(out, outSize,
           "%+.1fC",
           static_cast<float>(offsetTenths) / 10.0f);
}

}  // namespace

bool ThermalUi::begin(DisplayDriver& display) {
  display.fillScreen(kBg);
  return true;
}

void ThermalUi::showStatus(const char* message) {
  if (message == nullptr || message[0] == '\0') {
    return;
  }
  strncpy(feedback_, message, sizeof(feedback_) - 1);
  feedback_[sizeof(feedback_) - 1] = '\0';
  feedbackUntilMs_ = millis() + 3500;
}

void ThermalUi::render(DisplayDriver& display,
                       const ThermalFrame& frame,
                       const ThermalStats& stats,
                       const uint16_t* viewportPixels,
                       uint16_t viewportWidth,
                       uint16_t viewportHeight,
                       const AppSettings& settings,
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

  const uint16_t zoomW = zoomWidth(settings.landscape);
  const int16_t zoomX = orientX + orientW + 8;
  display.fillRoundRect(zoomX, 8, zoomW, 26, 6, zoomed_ ? kPrimary : kButton);
  display.drawRoundRect(zoomX, 8, zoomW, 26, 6, DisplayDriver::rgb565(100, 116, 139));
  drawZoomIcon(display, zoomX + static_cast<int16_t>(zoomW / 2), 21, zoomed_, kText);

  const int16_t liveX = zoomX + zoomW + 16;
  display.drawText(liveX, 14, "LIVE", kText, 1);
  display.fillRoundRect(liveX + 36, 16, 10, 10, 5, DisplayDriver::rgb565(34, 197, 94));
  display.drawText(liveX + 58, 14, "8.6 fps", kMuted, 1);
  display.drawTextRight(info.width - 10, 14, storageReady ? "TF OK" : "NO TF", storageReady ? kMuted : kError, 1);

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
  display.drawRect(hotX > 5 ? hotX - 5 : hotX, hotY > 5 ? hotY - 5 : hotY, 11, 11, kHot);
  display.drawRect(coldX > 5 ? coldX - 5 : coldX, coldY > 5 ? coldY - 5 : coldY, 11, 11, kCold);

  char tempText[16];
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
  const uint16_t buttonW = settings.landscape ? 52 : 42;
  const uint16_t buttonH = 32;
  const uint16_t gap = settings.landscape ? 12 : 8;
  const uint16_t totalButtonW = 5 * buttonW + 4 * gap;
  const uint16_t startX = totalButtonW < info.width ? (info.width - totalButtonW) / 2 : 0;
  drawIconButton(display, {static_cast<int16_t>(startX), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Palette);
  drawIconButton(display, {static_cast<int16_t>(startX + buttonW + gap), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Ffc);
  drawIconButton(display, {static_cast<int16_t>(startX + 2 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Capture);
  drawIconButton(display, {static_cast<int16_t>(startX + 3 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Playback);
  drawIconButton(display, {static_cast<int16_t>(startX + 4 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, buttonH}, Action::Setup);
  drawFeedback(display, settings.landscape);

  if (playbackActive_) {
    renderPlayback(display);
  }
  if (setupActive_) {
    renderSetup(display, setupDraftSettings_);
  }
}

bool ThermalUi::handleTouch(const TouchPoint& touch,
                            AppSettings& settings,
                            DisplayDriver& display,
                            CaptureStorage& storage,
                            const ThermalFrame& frame,
                            const ThermalStats& stats,
                            const uint16_t* viewportPixels,
                            uint16_t viewportWidth,
                            uint16_t viewportHeight) {
  if (!touch.pressed) {
    return false;
  }

  const uint32_t now = millis();
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
       action == Action::SetupTempOffset || action == Action::SetupAutoRotate ||
       action == Action::SetupOrientation || action == Action::SetupFilename ||
       action == Action::SetupRaw)) {
    return false;
  }

  switch (action) {
    case Action::Palette:
      cyclePalette();
      showStatus(paletteName(palette_));
      return true;
    case Action::Ffc:
      Serial.println("Manual FFC requested; CCI command not implemented yet");
      showStatus("FFC requested");
      return true;
    case Action::Capture:
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
    case Action::Playback:
      refreshCaptureBrowser(storage, settings);
      loadBrowserPreview(storage, viewportWidth, viewportHeight);
      playbackActive_ = true;
      ignoreSetupTouchUntilMs_ = now + 600;
      showStatus(captureBrowserCount_ == 0 ? "No captures found" : "Capture browser");
      return true;
    case Action::PlaybackPrev:
      if (captureBrowserCount_ > 0) {
        captureBrowserIndex_ = captureBrowserIndex_ == 0 ? captureBrowserCount_ - 1 : captureBrowserIndex_ - 1;
        storage.captureBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
        showStatus(loadBrowserPreview(storage, viewportWidth, viewportHeight) ? "Previous capture" : "Preview unavailable");
      }
      ignoreTouchUntilMs_ = now + 90;
      return true;
    case Action::PlaybackNext:
      if (captureBrowserCount_ > 0) {
        captureBrowserIndex_ = (captureBrowserIndex_ + 1) % captureBrowserCount_;
        storage.captureBaseAt(settings.savePath, captureBrowserIndex_, captureBrowserBasePath_, sizeof(captureBrowserBasePath_));
        showStatus(loadBrowserPreview(storage, viewportWidth, viewportHeight) ? "Next capture" : "Preview unavailable");
      }
      ignoreTouchUntilMs_ = now + 90;
      return true;
    case Action::PlaybackClose:
      playbackActive_ = false;
      ignoreTouchUntilMs_ = now + 300;
      showStatus("Review closed");
      return true;
    case Action::PlaybackDelete:
      if (storage.deleteCapture(captureBrowserBasePath_)) {
        hasCapturePreview_ = false;
        hasCaptureThumbnail_ = false;
        refreshCaptureBrowser(storage, settings);
        loadBrowserPreview(storage, viewportWidth, viewportHeight);
        showStatus("Capture deleted");
      } else {
        showStatus("Delete failed");
      }
      ignoreTouchUntilMs_ = now + 350;
      return true;
    case Action::SetupScrollUp:
      setupScrollY_ -= 44;
      if (setupScrollY_ < 0) {
        setupScrollY_ = 0;
      }
      showStatus("Setup scroll up");
      return true;
    case Action::SetupScrollDown:
      setupScrollY_ += 44;
      if (setupScrollY_ > 48) {
        setupScrollY_ = 48;
      }
      showStatus("Setup scroll down");
      return true;
    case Action::Setup:
      playbackActive_ = false;
      setupDraftSettings_ = settings;
      setupActive_ = true;
      setupScrollY_ = 0;
      ignoreSetupTouchUntilMs_ = now + 1500;
      showStatus("Setup opened");
      return true;
    case Action::Zoom:
      zoomed_ = !zoomed_;
      ignoreTouchUntilMs_ = now + 650;
      showStatus(zoomed_ ? "Zoom in" : "Zoom out");
      return true;
    case Action::SetupCancel:
      setupActive_ = false;
      ignoreTouchUntilMs_ = now + 350;
      showStatus("Setup canceled");
      return true;
    case Action::SetupSave:
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
      cyclePath(setupDraftSettings_);
      showStatus(setupDraftSettings_.savePath);
      return true;
    case Action::SetupIdle:
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
    case Action::SetupTempOffset:
      cycleTemperatureOffset(setupDraftSettings_);
      {
        char offsetText[16] = {};
        char message[40] = {};
        formatOffset(offsetText, sizeof(offsetText), setupDraftSettings_.temperatureOffsetTenths);
        snprintf(message, sizeof(message), "Temp offset %s", offsetText);
        showStatus(message);
      }
      return true;
    case Action::SetupAutoRotate:
      toggleAutoRotate(setupDraftSettings_);
      showStatus(setupDraftSettings_.autoRotate ? "Auto rotate on" : "Auto rotate off");
      return true;
    case Action::SetupOrientation:
      toggleManualOrientation(setupDraftSettings_);
      showStatus(setupDraftSettings_.landscape ? "Manual landscape" : "Manual portrait");
      return true;
    case Action::SetupFilename:
      toggleFilenameFooter(setupDraftSettings_);
      showStatus(setupDraftSettings_.includeFilenameInCapture ? "Capture name on" : "Capture name off");
      return true;
    case Action::SetupRaw:
      toggleRawCapture(setupDraftSettings_);
      showStatus(setupDraftSettings_.saveRawCapture ? "Raw save on" : "Raw save off");
      return true;
    case Action::None:
    default:
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
    if (contains({static_cast<int16_t>(panelX + 4), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackPrev;
    if (contains({static_cast<int16_t>(panelX + 62), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackNext;
    if (contains({static_cast<int16_t>(panelX + panelW - 126), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackDelete;
    if (contains({static_cast<int16_t>(panelX + panelW - 68), static_cast<int16_t>(bottomY), 62, 44}, x, y)) return Action::PlaybackClose;
    return Action::None;
  }

  if (setupActive_) {
    const uint16_t panelX = landscape ? 40 : 20;
    const uint16_t panelY = landscape ? 34 : 56;
    const uint16_t panelW = screenW - (panelX * 2);
    const uint16_t panelH = landscape ? 280 : 368;
    const int16_t maxScroll = landscape ? 0 : 48;
    if (contains({static_cast<int16_t>(panelX), static_cast<int16_t>(panelY), panelW, panelH}, x, y)) {
      if (maxScroll > 0 && setupScrollY_ > 0 &&
          contains({static_cast<int16_t>(panelX + panelW - 30), static_cast<int16_t>(panelY + 42), 24, 28}, x, y)) {
        return Action::SetupScrollUp;
      }
      if (maxScroll > 0 && setupScrollY_ < maxScroll &&
          contains({static_cast<int16_t>(panelX + panelW - 30), static_cast<int16_t>(panelY + panelH - 78), 24, 28}, x, y)) {
        return Action::SetupScrollDown;
      }
      const uint16_t contentY = static_cast<uint16_t>(y + setupScrollY_);
      if (landscape) {
        const uint16_t leftFieldX = panelX + 66;
        const uint16_t rightFieldX = panelX + 268;
        const uint16_t fieldW = 120;
        const uint16_t hitW = fieldW + 16;
        if (contains({static_cast<int16_t>(leftFieldX - 8), static_cast<int16_t>(panelY + 78), hitW, 34}, x, contentY)) return Action::SetupPath;
        if (contains({static_cast<int16_t>(leftFieldX - 8), static_cast<int16_t>(panelY + 116), hitW, 34}, x, contentY)) return Action::SetupIdle;
        if (contains({static_cast<int16_t>(leftFieldX - 8), static_cast<int16_t>(panelY + 154), hitW, 34}, x, contentY)) return Action::SetupTempOffset;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 78), hitW, 34}, x, contentY)) return Action::SetupAutoRotate;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 116), hitW, 34}, x, contentY)) return Action::SetupOrientation;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 154), hitW, 34}, x, contentY)) return Action::SetupFilename;
        if (contains({static_cast<int16_t>(rightFieldX - 8), static_cast<int16_t>(panelY + 192), hitW, 34}, x, contentY)) return Action::SetupRaw;
      } else {
        const uint16_t fieldX = panelX + 108;
        const uint16_t fieldW = panelW - 160;
        const uint16_t hitW = fieldW + 16;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 82), hitW, 32}, x, contentY)) return Action::SetupPath;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 116), hitW, 32}, x, contentY)) return Action::SetupAutoRotate;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 150), hitW, 32}, x, contentY)) return Action::SetupOrientation;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 184), hitW, 32}, x, contentY)) return Action::SetupIdle;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 218), hitW, 32}, x, contentY)) return Action::SetupTempOffset;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 252), hitW, 32}, x, contentY)) return Action::SetupFilename;
        if (contains({static_cast<int16_t>(fieldX - 8), static_cast<int16_t>(panelY + 286), hitW, 32}, x, contentY)) return Action::SetupRaw;
      }
      if (contains({static_cast<int16_t>(panelX + 12), static_cast<int16_t>(panelY + panelH - 42), 112, 36}, x, y)) return Action::SetupCancel;
      if (contains({static_cast<int16_t>(panelX + panelW - 124), static_cast<int16_t>(panelY + panelH - 42), 112, 36}, x, y)) return Action::SetupSave;
    }
    return Action::None;
  }

  const uint16_t orientW = orientationWidth(landscape);
  const int16_t orientX = 8;
  const uint16_t zoomW = zoomWidth(landscape);
  const int16_t zoomX = orientX + orientW + 8;
  if (contains({static_cast<int16_t>(zoomX - 6), 4, static_cast<uint16_t>(zoomW + 12), 36}, x, y)) {
    return Action::Zoom;
  }

  const uint16_t buttonY = screenH - (landscape ? 52 : 54);
  const uint16_t buttonW = landscape ? 52 : 42;
  const uint16_t gap = landscape ? 12 : 8;
  const uint16_t totalButtonW = 5 * buttonW + 4 * gap;
  const uint16_t startX = totalButtonW < screenW ? (screenW - totalButtonW) / 2 : 0;
  const uint16_t hitW = buttonW + gap;
  const uint16_t hitH = 52;
  if (contains({static_cast<int16_t>(startX - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Palette;
  }
  if (contains({static_cast<int16_t>(startX + buttonW + gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Ffc;
  }
  if (contains({static_cast<int16_t>(startX + 2 * (buttonW + gap) - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Capture;
  }
  if (contains({static_cast<int16_t>(startX + 3 * (buttonW + gap) - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Playback;
  }
  if (contains({static_cast<int16_t>(startX + 4 * (buttonW + gap) - gap / 2), static_cast<int16_t>(buttonY - 10), hitW, hitH}, x, y)) {
    return Action::Setup;
  }
  return Action::None;
}

void ThermalUi::cyclePalette() {
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
    default:
      palette_ = PaletteMode::Ironbow;
      break;
  }
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

void ThermalUi::cycleTemperatureOffset(AppSettings& settings) {
  if (settings.temperatureOffsetTenths < -50 || settings.temperatureOffsetTenths >= 50) {
    settings.temperatureOffsetTenths = -50;
    return;
  }
  settings.temperatureOffsetTenths += 5;
}

void ThermalUi::toggleFilenameFooter(AppSettings& settings) {
  settings.includeFilenameInCapture = !settings.includeFilenameInCapture;
}

void ThermalUi::toggleRawCapture(AppSettings& settings) {
  settings.saveRawCapture = !settings.saveRawCapture;
}

void ThermalUi::toggleAutoRotate(AppSettings& settings) {
  settings.autoRotate = !settings.autoRotate;
}

void ThermalUi::toggleManualOrientation(AppSettings& settings) {
  if (!settings.autoRotate) {
    settings.landscape = !settings.landscape;
  }
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

bool ThermalUi::loadBrowserPreview(CaptureStorage& storage, uint16_t viewportWidth, uint16_t viewportHeight) {
  if (captureBrowserBasePath_[0] == '\0') {
    hasCapturePreview_ = false;
    hasCaptureThumbnail_ = false;
    return false;
  }
  const bool landscapePreview = viewportWidth > viewportHeight;
  const uint16_t thumbW = landscapePreview ? kLandscapeCaptureThumbW : kPortraitCaptureThumbW;
  const uint16_t thumbH = landscapePreview ? kLandscapeCaptureThumbH : kPortraitCaptureThumbH;
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
  hasCaptureThumbnail_ = captureThumbnailPixels_ != nullptr &&
                         storage.loadCaptureBmpScaled(captureBrowserBasePath_,
                                                      captureThumbnailPixels_,
                                                      captureThumbnailWidth_,
                                                      captureThumbnailHeight_);
  hasCapturePreview_ = false;
  return hasCaptureThumbnail_;
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

void ThermalUi::renderSetup(DisplayDriver& display, const AppSettings& settings) {
  const DisplayInfo info = display.info();
  const bool landscape = info.width > info.height;
  const uint16_t panelX = landscape ? 40 : 20;
  const uint16_t panelY = landscape ? 34 : 56;
  const uint16_t panelW = info.width - (panelX * 2);
  const uint16_t panelH = landscape ? 280 : 368;
  const int16_t maxScroll = landscape ? 0 : 48;
  if (setupScrollY_ > maxScroll) {
    setupScrollY_ = maxScroll;
  }
  display.fillRect(panelX, panelY, panelW, panelH, DisplayDriver::rgb565(245, 247, 250));
  display.drawRect(panelX, panelY, panelW, panelH, kPrimary);
  display.drawText(panelX + 14, panelY + 14, "SETUP", DisplayDriver::rgb565(15, 23, 42), 2);
  if (maxScroll > 0) {
    display.drawText(panelX + panelW - 24, panelY + 48, "^", DisplayDriver::rgb565(15, 23, 42), 2);
    display.drawText(panelX + panelW - 24, panelY + panelH - 72, "v", DisplayDriver::rgb565(15, 23, 42), 2);
  }

  char idleText[16] = {};
  if (settings.inactivitySleepSeconds == 0) {
    snprintf(idleText, sizeof(idleText), "Off");
  } else {
    snprintf(idleText, sizeof(idleText), "%u sec", settings.inactivitySleepSeconds);
  }
  char offsetText[16] = {};
  formatOffset(offsetText, sizeof(offsetText), settings.temperatureOffsetTenths);
  const char* orientText = settings.autoRotate ? "Auto" : (settings.landscape ? "Land" : "Port");
  const int16_t rowBaseY = static_cast<int16_t>(panelY) - setupScrollY_;

  if (landscape) {
    const uint16_t leftLabelX = panelX + 16;
    const uint16_t leftFieldX = panelX + 66;
    const uint16_t rightLabelX = panelX + 212;
    const uint16_t rightFieldX = panelX + 268;
    const uint16_t fieldW = 120;
    display.drawText(leftLabelX, rowBaseY + 88, "Path", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(leftFieldX), static_cast<int16_t>(rowBaseY + 78), fieldW, 26}, settings.savePath);
    display.drawText(leftLabelX, rowBaseY + 126, "Idle", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(leftFieldX), static_cast<int16_t>(rowBaseY + 116), fieldW, 26}, idleText);
    display.drawText(leftLabelX, rowBaseY + 164, "Cal", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(leftFieldX), static_cast<int16_t>(rowBaseY + 154), fieldW, 26}, offsetText);
    display.drawText(rightLabelX, rowBaseY + 88, "Auto", DisplayDriver::rgb565(51, 65, 85), 1);
    drawToggle(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 78), fieldW, 26}, settings.autoRotate);
    display.drawText(rightLabelX, rowBaseY + 126, "Orient", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 116), fieldW, 26}, orientText);
    display.drawText(rightLabelX, rowBaseY + 164, "Show Name", DisplayDriver::rgb565(51, 65, 85), 1);
    drawToggle(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 154), fieldW, 26}, settings.includeFilenameInCapture);
    display.drawText(rightLabelX, rowBaseY + 202, "Raw", DisplayDriver::rgb565(51, 65, 85), 1);
    drawToggle(display, {static_cast<int16_t>(rightFieldX), static_cast<int16_t>(rowBaseY + 192), fieldW, 26}, settings.saveRawCapture);
  } else {
    const uint16_t labelX = panelX + 16;
    const uint16_t fieldX = panelX + 108;
    const uint16_t fieldW = panelW - 160;
    display.drawText(labelX, rowBaseY + 92, "Path", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 82), fieldW, 26}, settings.savePath);
    display.drawText(labelX, rowBaseY + 126, "Auto", DisplayDriver::rgb565(51, 65, 85), 1);
    drawToggle(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 116), fieldW, 26}, settings.autoRotate);
    display.drawText(labelX, rowBaseY + 160, "Orientation", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 150), fieldW, 26}, orientText);
    display.drawText(labelX, rowBaseY + 194, "Idle", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 184), fieldW, 26}, idleText);
    display.drawText(labelX, rowBaseY + 228, "Cal", DisplayDriver::rgb565(51, 65, 85), 1);
    drawButton(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 218), fieldW, 26}, offsetText);
    display.drawText(labelX, rowBaseY + 262, "Show Name", DisplayDriver::rgb565(51, 65, 85), 1);
    drawToggle(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 252), fieldW, 26}, settings.includeFilenameInCapture);
    display.drawText(labelX, rowBaseY + 296, "Raw", DisplayDriver::rgb565(51, 65, 85), 1);
    drawToggle(display, {static_cast<int16_t>(fieldX), static_cast<int16_t>(rowBaseY + 286), fieldW, 26}, settings.saveRawCapture);
  }
  drawButton(display, {static_cast<int16_t>(panelX + 16), static_cast<int16_t>(panelY + panelH - 34), 90, 24}, "Cancel");
  drawButton(display, {static_cast<int16_t>(panelX + panelW - 106), static_cast<int16_t>(panelY + panelH - 34), 90, 24}, "Save", true);
}

void ThermalUi::renderPlayback(DisplayDriver& display) {
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

  char countText[28] = {};
  if (captureBrowserCount_ == 0) {
    snprintf(countText, sizeof(countText), "No files");
  } else {
    snprintf(countText, sizeof(countText), "%u/%u", captureBrowserIndex_ + 1, captureBrowserCount_);
  }
  display.drawTextRight(panelX + panelW - 14, panelY + 18, countText, DisplayDriver::rgb565(15, 23, 42), 1);

  const char* path = captureBrowserBasePath_[0] != '\0' ? captureBrowserBasePath_ : "No capture files";
  display.drawText(panelX + 16, panelY + 38, path, DisplayDriver::rgb565(15, 23, 42), 1);

  const uint16_t thumbW = captureThumbnailWidth_ > 0 ? captureThumbnailWidth_ : (landscape ? kLandscapeCaptureThumbW : kPortraitCaptureThumbW);
  const uint16_t thumbH = captureThumbnailHeight_ > 0 ? captureThumbnailHeight_ : (landscape ? kLandscapeCaptureThumbH : kPortraitCaptureThumbH);
  const uint16_t thumbX = panelX + (panelW > thumbW ? (panelW - thumbW) / 2 : 8);
  const uint16_t thumbY = panelY + (landscape ? 50 : 54);
  if (hasCaptureThumbnail_ && captureThumbnailPixels_ != nullptr) {
    display.drawBitmap(thumbX, thumbY, thumbW, thumbH, captureThumbnailPixels_);
    display.drawRect(thumbX, thumbY, thumbW, thumbH, DisplayDriver::rgb565(15, 23, 42));
  } else {
    display.fillRect(thumbX, thumbY, thumbW, thumbH, DisplayDriver::rgb565(226, 232, 240));
    display.drawRect(thumbX, thumbY, thumbW, thumbH, DisplayDriver::rgb565(100, 116, 139));
    display.drawText(thumbX + 42, thumbY + 36, "No preview", DisplayDriver::rgb565(71, 85, 105), 1);
  }

  const uint16_t bottomY = panelY + panelH - 36;
  drawIconButton(display, {static_cast<int16_t>(panelX + 14), static_cast<int16_t>(bottomY), 44, 28}, Action::PlaybackPrev);
  drawIconButton(display, {static_cast<int16_t>(panelX + 70), static_cast<int16_t>(bottomY), 44, 28}, Action::PlaybackNext);
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

  const uint16_t zoomW = zoomWidth(settings.landscape);
  const int16_t zoomX = orientX + orientW + 8;
  display.fillRoundRect(zoomX, 8, zoomW, 26, 6, DisplayDriver::rgb565(37, 99, 235));
  display.drawRoundRect(zoomX, 8, zoomW, 26, 6, DisplayDriver::rgb565(15, 23, 42));
  drawZoomIcon(display, zoomX + static_cast<int16_t>(zoomW / 2), 21, zoomed_, DisplayDriver::rgb565(15, 23, 42));

  const int16_t waitX = zoomX + zoomW + 16;
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
  drawActionIcon(display,
                 rect.x + static_cast<int16_t>(rect.w / 2),
                 rect.y + static_cast<int16_t>(rect.h / 2),
                 action,
                 kText);
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
    case Action::Ffc:
      display.drawRect(cx - 12, cy - 8, 24, 16, color);
      display.fillRect(cx - 9, cy - 5, 18, 3, color);
      display.fillRect(cx - 9, cy - 1, 18, 3, DisplayDriver::rgb565(100, 116, 139));
      display.fillRect(cx - 9, cy + 4, 18, 3, color);
      break;
    case Action::Capture:
      display.drawRect(cx - 13, cy - 6, 26, 15, color);
      display.fillRect(cx - 8, cy - 10, 9, 4, color);
      display.drawRect(cx - 4, cy - 2, 8, 8, color);
      display.fillRect(cx + 8, cy - 3, 2, 2, color);
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

void ThermalUi::drawZoomIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool zoomed, uint16_t color) {
  display.drawRect(cx - 10, cy - 10, 14, 14, color);
  display.fillRect(cx + 3, cy + 3, 8, 3, color);
  display.fillRect(cx - 6, cy - 4, 6, 2, color);
  if (!zoomed) {
    display.fillRect(cx - 4, cy - 6, 2, 6, color);
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
