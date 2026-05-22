#pragma once

#include "board/DisplayDriver.h"
#include "board/TouchDriver.h"
#include "settings/AppSettings.h"
#include "storage/CaptureStorage.h"
#include "thermal/ThermalFrame.h"
#include "thermal/ThermalProcessor.h"

class ThermalUi {
 public:
  bool begin(DisplayDriver& display);
  void render(DisplayDriver& display,
              const ThermalFrame& frame,
              const ThermalStats& stats,
              const uint16_t* viewportPixels,
              uint16_t viewportWidth,
              uint16_t viewportHeight,
              const AppSettings& settings,
              bool storageReady);
  bool handleTouch(const TouchPoint& touch,
                   AppSettings& settings,
                   DisplayDriver& display,
                   CaptureStorage& storage,
                   const ThermalFrame& frame,
                   const uint16_t* viewportPixels,
                   uint16_t viewportWidth,
                   uint16_t viewportHeight);

  PaletteMode palette() const { return palette_; }
  bool zoomed() const { return zoomed_; }
  bool setupActive() const { return setupActive_; }

 private:
  enum class Action {
    None,
    Palette,
    Ffc,
    Capture,
    Setup,
    Zoom,
    Orientation,
    SetupCancel,
    SetupSave,
    SetupPath,
  };

  struct Rect {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
  };

  Action hitTest(uint16_t x, uint16_t y, bool landscape) const;
  void cyclePalette();
  void cyclePath(AppSettings& settings);
  void renderSetup(DisplayDriver& display, const AppSettings& settings);
  void renderWaiting(DisplayDriver& display, const AppSettings& settings, bool storageReady);
  void drawButton(DisplayDriver& display, const Rect& rect, const char* label, bool primary = false);
  void drawTempLabel(DisplayDriver& display, int16_t x, int16_t y, const char* text, uint16_t color);
  void drawPaletteScale(DisplayDriver& display,
                        int16_t x,
                        int16_t y,
                        uint16_t h,
                        float maxC,
                        float minC,
                        bool labelsLeft);
  void drawFeedback(DisplayDriver& display, bool landscape);
  void setFeedback(const char* message);
  bool contains(const Rect& rect, uint16_t x, uint16_t y) const;

  PaletteMode palette_ = PaletteMode::Ironbow;
  bool zoomed_ = false;
  bool setupActive_ = false;
  uint32_t lastTouchMs_ = 0;
  uint32_t ignoreTouchUntilMs_ = 0;
  uint32_t ignoreSetupTouchUntilMs_ = 0;
  char feedback_[40] = "Ready";
  uint32_t feedbackUntilMs_ = 0;
};
