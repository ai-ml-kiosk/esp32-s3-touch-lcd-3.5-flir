#include "ui/ThermalUi.h"

#include <cstring>

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
  return landscape ? 112 : 90;
}

uint16_t zoomWidth(bool landscape) {
  return landscape ? 70 : 56;
}

void formatTemp(char* out, size_t outSize, float temp) {
  snprintf(out, outSize, "%.1fC", temp);
}

}  // namespace

bool ThermalUi::begin(DisplayDriver& display) {
  display.fillScreen(kBg);
  return true;
}

void ThermalUi::render(DisplayDriver& display,
                       const ThermalFrame& frame,
                       const ThermalStats& stats,
                       const uint16_t* viewportPixels,
                       uint16_t viewportWidth,
                       uint16_t viewportHeight,
                       const AppSettings& settings,
                       bool storageReady) {
  if (frame.frameNumber == 0) {
    renderWaiting(display, settings, storageReady);
    return;
  }

  const DisplayInfo info = display.info();
  display.fillScreen(kBg);

  const uint16_t statusH = settings.landscape ? 44 : 44;
  display.fillRect(0, 0, info.width, statusH, kPanel);

  const char* orientation = settings.landscape ? "LANDSCAPE" : "PORTRAIT";
  const uint16_t orientW = orientationWidth(settings.landscape);
  const int16_t orientX = 8;
  display.fillRoundRect(orientX, 8, orientW, 26, 6, kButton);
  display.drawRoundRect(orientX, 8, orientW, 26, 6, DisplayDriver::rgb565(100, 116, 139));
  display.drawText(orientX + 10, 17, orientation, kText, 1);

  const uint16_t zoomW = zoomWidth(settings.landscape);
  const int16_t zoomX = orientX + orientW + 8;
  display.fillRoundRect(zoomX, 8, zoomW, 26, 6, zoomed_ ? kPrimary : kButton);
  display.drawRoundRect(zoomX, 8, zoomW, 26, 6, DisplayDriver::rgb565(100, 116, 139));
  display.drawText(zoomX + 10, 17, zoomed_ ? "ZOOM-" : "ZOOM+", kText, 1);

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
    readoutY = info.height - 112;
  }

  display.drawBitmap(viewX, viewY, viewportWidth, viewportHeight, viewportPixels);

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
    drawPaletteScale(display, scaleX, viewY, viewportHeight, stats.maxC, stats.minC, true);
    display.fillRect(readoutX, readoutY, 42, viewportHeight, kPanel);
    display.drawText(readoutX + 6, readoutY + 16, "CTR", kMuted, 1);
    formatTemp(tempText, sizeof(tempText), stats.centerC);
    display.drawText(readoutX + 6, readoutY + 34, tempText, kText, 1);
    display.drawText(readoutX + 6, readoutY + 82, "RNG", kMuted, 1);
    display.drawText(readoutX + 6, readoutY + 100, "AUTO", kText, 1);
    display.drawText(readoutX + 6, readoutY + 148, "PAL", kMuted, 1);
    display.drawText(readoutX + 6, readoutY + 166, paletteName(palette_), kText, 1);
  } else {
    drawPaletteScale(display, info.width - 18, viewY, viewportHeight, stats.maxC, stats.minC, true);
    display.fillRect(readoutX, readoutY, info.width, 48, kPanel);
    display.drawText(16, readoutY + 10, "CTR", kMuted, 1);
    formatTemp(tempText, sizeof(tempText), stats.centerC);
    display.drawText(16, readoutY + 28, tempText, kText, 1);
    display.drawText(112, readoutY + 10, "RNG", kMuted, 1);
    display.drawText(112, readoutY + 28, "AUTO", kText, 1);
    display.drawText(220, readoutY + 10, "PAL", kMuted, 1);
    display.drawText(220, readoutY + 28, paletteName(palette_), kText, 1);
  }

  const uint16_t buttonY = info.height - (settings.landscape ? 54 : 58);
  const uint16_t buttonW = settings.landscape ? 108 : 70;
  const uint16_t gap = settings.landscape ? 12 : 8;
  const uint16_t startX = settings.landscape ? 0 : 4;
  drawButton(display, {static_cast<int16_t>(startX), static_cast<int16_t>(buttonY), buttonW, 24}, "PAL");
  drawButton(display, {static_cast<int16_t>(startX + buttonW + gap), static_cast<int16_t>(buttonY), buttonW, 24}, "FFC");
  drawButton(display, {static_cast<int16_t>(startX + 2 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, 24}, "CAP");
  drawButton(display, {static_cast<int16_t>(startX + 3 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, 24}, "SETUP", false);
  drawFeedback(display, settings.landscape);

  if (setupActive_) {
    renderSetup(display, settings);
  }
}

bool ThermalUi::handleTouch(const TouchPoint& touch,
                            AppSettings& settings,
                            DisplayDriver& display,
                            CaptureStorage& storage,
                            const ThermalFrame& frame,
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
  if (now - lastTouchMs_ < 220) {
    return false;
  }
  lastTouchMs_ = now;

  const Action action = hitTest(touch.x, touch.y, settings.landscape);
  if (setupActive_ && now < ignoreSetupTouchUntilMs_ &&
      (action == Action::SetupCancel || action == Action::SetupSave || action == Action::SetupPath)) {
    return false;
  }

  switch (action) {
    case Action::Palette:
      cyclePalette();
      setFeedback(paletteName(palette_));
      return true;
    case Action::Ffc:
      Serial.println("Manual FFC requested; CCI command not implemented yet");
      setFeedback("FFC requested");
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
                                               savedBasePath,
                                               sizeof(savedBasePath));
        char message[40] = {};
        if (saved) {
          snprintf(message, sizeof(message), "Saved %s.bmp", savedBasePath);
        } else if (savedBasePath[0] != '\0') {
          snprintf(message, sizeof(message), "Save failed %s", savedBasePath);
        } else {
          snprintf(message, sizeof(message), "Capture failed");
        }
        setFeedback(message);
      }
      return true;
    case Action::Setup:
      setupActive_ = true;
      ignoreSetupTouchUntilMs_ = now + 1500;
      setFeedback("Setup opened");
      return true;
    case Action::Zoom:
      zoomed_ = !zoomed_;
      ignoreTouchUntilMs_ = now + 650;
      setFeedback(zoomed_ ? "Zoom in" : "Zoom out");
      return true;
    case Action::Orientation:
      settings.landscape = !settings.landscape;
      display.setLandscape(settings.landscape);
      ignoreTouchUntilMs_ = now + 750;
      setFeedback(settings.landscape ? "Landscape" : "Portrait");
      return true;
    case Action::SetupCancel:
      setupActive_ = false;
      setFeedback("Setup canceled");
      return true;
    case Action::SetupSave:
      if (storage.validateSavePath(settings.savePath)) {
        setupActive_ = false;
        setFeedback("Setup saved");
      } else {
        setFeedback("Invalid path");
      }
      return true;
    case Action::SetupPath:
      cyclePath(settings);
      setFeedback(settings.savePath);
      return true;
    case Action::None:
    default:
      return false;
  }
}

ThermalUi::Action ThermalUi::hitTest(uint16_t x, uint16_t y, bool landscape) const {
  const uint16_t screenW = landscape ? 480 : 320;
  const uint16_t screenH = landscape ? 320 : 480;

  if (setupActive_) {
    if (contains({40, 90, static_cast<uint16_t>(screenW - 80), 220}, x, y)) {
      const uint16_t panelX = 40;
      const uint16_t panelY = 90;
      if (contains({static_cast<int16_t>(panelX + 112), static_cast<int16_t>(panelY + 118), 150, 28}, x, y)) {
        return Action::SetupPath;
      }
      if (contains({static_cast<int16_t>(panelX + 18), static_cast<int16_t>(panelY + 170), 90, 34}, x, y)) {
        return Action::SetupCancel;
      }
      if (contains({static_cast<int16_t>(screenW - 148), static_cast<int16_t>(panelY + 170), 90, 34}, x, y)) {
        return Action::SetupSave;
      }
    }
    return Action::None;
  }

  const uint16_t orientW = orientationWidth(landscape);
  const int16_t orientX = 8;
  if (contains({orientX, 8, orientW, 26}, x, y)) {
    return Action::Orientation;
  }
  const uint16_t zoomW = zoomWidth(landscape);
  const int16_t zoomX = orientX + orientW + 8;
  if (contains({zoomX, 8, zoomW, 26}, x, y)) {
    return Action::Zoom;
  }

  const uint16_t buttonY = screenH - (landscape ? 54 : 58);
  const uint16_t buttonW = landscape ? 108 : 70;
  const uint16_t gap = landscape ? 12 : 8;
  const uint16_t startX = landscape ? 0 : 4;
  if (contains({static_cast<int16_t>(startX), static_cast<int16_t>(buttonY), buttonW, 24}, x, y)) {
    return Action::Palette;
  }
  if (contains({static_cast<int16_t>(startX + buttonW + gap), static_cast<int16_t>(buttonY), buttonW, 24}, x, y)) {
    return Action::Ffc;
  }
  if (contains({static_cast<int16_t>(startX + 2 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, 24}, x, y)) {
    return Action::Capture;
  }
  if (contains({static_cast<int16_t>(startX + 3 * (buttonW + gap)), static_cast<int16_t>(buttonY), buttonW, 24}, x, y)) {
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

void ThermalUi::renderSetup(DisplayDriver& display, const AppSettings& settings) {
  const DisplayInfo info = display.info();
  const uint16_t panelW = info.width - 80;
  const uint16_t panelH = 220;
  const uint16_t panelX = 40;
  const uint16_t panelY = 90;
  display.fillRect(panelX, panelY, panelW, panelH, DisplayDriver::rgb565(245, 247, 250));
  display.drawRect(panelX, panelY, panelW, panelH, kPrimary);
  display.drawText(panelX + 14, panelY + 14, "SETUP", DisplayDriver::rgb565(15, 23, 42), 2);
  display.drawText(panelX + 18, panelY + 54, "Date", DisplayDriver::rgb565(51, 65, 85), 1);
  display.drawText(panelX + 112, panelY + 54, settings.dateFormat, DisplayDriver::rgb565(15, 23, 42), 1);
  display.drawText(panelX + 18, panelY + 86, "Time", DisplayDriver::rgb565(51, 65, 85), 1);
  display.drawText(panelX + 112, panelY + 86, settings.timeFormat, DisplayDriver::rgb565(15, 23, 42), 1);
  display.drawText(panelX + 18, panelY + 118, "Path", DisplayDriver::rgb565(51, 65, 85), 1);
  drawButton(display, {static_cast<int16_t>(panelX + 112), static_cast<int16_t>(panelY + 108), 150, 28}, settings.savePath);
  drawButton(display, {static_cast<int16_t>(panelX + 18), static_cast<int16_t>(panelY + 170), 90, 34}, "Cancel");
  drawButton(display, {static_cast<int16_t>(info.width - 148), static_cast<int16_t>(panelY + 170), 90, 34}, "Save", true);
}

void ThermalUi::renderWaiting(DisplayDriver& display, const AppSettings& settings, bool storageReady) {
  const DisplayInfo info = display.info();
  display.fillScreen(DisplayDriver::rgb565(15, 23, 42));
  display.fillRect(0, 0, info.width, 44, DisplayDriver::rgb565(250, 204, 21));

  const char* orientation = settings.landscape ? "LANDSCAPE" : "PORTRAIT";
  const uint16_t orientW = orientationWidth(settings.landscape);
  const int16_t orientX = 8;
  display.fillRoundRect(orientX, 8, orientW, 26, 6, DisplayDriver::rgb565(37, 99, 235));
  display.drawRoundRect(orientX, 8, orientW, 26, 6, DisplayDriver::rgb565(15, 23, 42));
  display.drawText(orientX + 10, 17, orientation, kText, 1);

  const uint16_t zoomW = zoomWidth(settings.landscape);
  const int16_t zoomX = orientX + orientW + 8;
  display.fillRoundRect(zoomX, 8, zoomW, 26, 6, DisplayDriver::rgb565(37, 99, 235));
  display.drawRoundRect(zoomX, 8, zoomW, 26, 6, DisplayDriver::rgb565(15, 23, 42));
  display.drawText(zoomX + 10, 17, zoomed_ ? "ZOOM-" : "ZOOM+", kText, 1);

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

void ThermalUi::drawFeedback(DisplayDriver& display, bool landscape) {
  const DisplayInfo info = display.info();
  const int16_t y = info.height - (landscape ? 24 : 28);
  const uint16_t h = landscape ? 18 : 20;
  const uint16_t bg = millis() < feedbackUntilMs_ ? DisplayDriver::rgb565(15, 23, 42) : kBg;
  display.fillRoundRect(8, y, info.width - 16, h, 5, bg);
  display.drawRoundRect(8, y, info.width - 16, h, 5, DisplayDriver::rgb565(51, 65, 85));
  display.drawText(16, y + 5, feedback_, kMuted, 1);
}

void ThermalUi::setFeedback(const char* message) {
  if (message == nullptr || message[0] == '\0') {
    return;
  }
  strncpy(feedback_, message, sizeof(feedback_) - 1);
  feedback_[sizeof(feedback_) - 1] = '\0';
  feedbackUntilMs_ = millis() + 2500;
}

bool ThermalUi::contains(const Rect& rect, uint16_t x, uint16_t y) const {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}
