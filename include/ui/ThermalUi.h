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
              bool storageReady,
              const char* lastCaptureBasePath);
  bool handleTouch(const TouchPoint& touch,
                   AppSettings& settings,
                   DisplayDriver& display,
                   CaptureStorage& storage,
                   const ThermalFrame& frame,
                   const ThermalStats& stats,
                   const uint16_t* viewportPixels,
                   uint16_t viewportWidth,
                   uint16_t viewportHeight);

  PaletteMode palette() const { return palette_; }
  bool zoomed() const { return zoomed_; }
  bool setupActive() const { return setupActive_; }
  bool playbackActive() const { return playbackActive_; }
  void showStatus(const char* message);

 private:
  enum class Action {
    None,
    Palette,
    Ffc,
    Capture,
    Playback,
    Setup,
    Zoom,
    HotColdDetails,
    CenterTemperature,
    PlaybackClose,
    PlaybackDelete,
    PlaybackPrev,
    PlaybackNext,
    SetupScrollUp,
    SetupScrollDown,
    SetupCancel,
    SetupSave,
    SetupPath,
    SetupIdle,
    SetupTempOffset,
    SetupAutoRotate,
    SetupOrientation,
    SetupFilename,
    SetupRaw,
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
  void cycleIdleSleep(AppSettings& settings);
  void cycleTemperatureOffset(AppSettings& settings);
  void toggleAutoRotate(AppSettings& settings);
  void toggleManualOrientation(AppSettings& settings);
  void toggleFilenameFooter(AppSettings& settings);
  void toggleRawCapture(AppSettings& settings);
  void storeCapturePreview(const uint16_t* viewportPixels, uint16_t viewportWidth, uint16_t viewportHeight);
  bool ensureCapturePreviewBuffer(uint16_t viewportWidth, uint16_t viewportHeight);
  void releaseCaptureThumbnail();
  bool loadBrowserPreview(CaptureStorage& storage, uint16_t viewportWidth, uint16_t viewportHeight);
  void renderSetup(DisplayDriver& display, const AppSettings& settings);
  void refreshCaptureBrowser(CaptureStorage& storage, const AppSettings& settings);
  void renderPlayback(DisplayDriver& display);
  void renderWaiting(DisplayDriver& display, const AppSettings& settings, bool storageReady);
  void drawButton(DisplayDriver& display, const Rect& rect, const char* label, bool primary = false);
  void drawToggle(DisplayDriver& display, const Rect& rect, bool enabled);
  void drawIconButton(DisplayDriver& display, const Rect& rect, Action action, bool primary = false);
  void drawActionIcon(DisplayDriver& display, int16_t cx, int16_t cy, Action action, uint16_t color);
  void drawZoomIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool zoomed, uint16_t color);
  void drawHotColdIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled);
  void drawCenterTempIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled);
  void drawOrientationIcon(DisplayDriver& display, int16_t x, int16_t y, bool landscape);
  void drawTempLabel(DisplayDriver& display, int16_t x, int16_t y, const char* text, uint16_t color);
  void drawPaletteScale(DisplayDriver& display,
                        int16_t x,
                        int16_t y,
                        uint16_t h,
                        float maxC,
                        float minC,
                        bool labelsLeft);
  void drawPaletteScaleEndLabels(DisplayDriver& display,
                                 int16_t x,
                                 int16_t y,
                                 uint16_t h,
                                 float maxC,
                                 float minC);
  void drawFeedback(DisplayDriver& display, bool landscape);
  bool contains(const Rect& rect, uint16_t x, uint16_t y) const;

  PaletteMode palette_ = PaletteMode::Ironbow;
  bool zoomed_ = false;
  bool setupActive_ = false;
  bool playbackActive_ = false;
  bool hasCapturePreview_ = false;
  bool hasCaptureThumbnail_ = false;
  AppSettings setupDraftSettings_{};
  int16_t setupScrollY_ = 0;
  uint16_t* capturePreviewPixels_ = nullptr;
  uint16_t* captureThumbnailPixels_ = nullptr;
  uint16_t capturePreviewWidth_ = 0;
  uint16_t capturePreviewHeight_ = 0;
  uint16_t captureThumbnailWidth_ = 0;
  uint16_t captureThumbnailHeight_ = 0;
  uint16_t captureBrowserIndex_ = 0;
  uint16_t captureBrowserCount_ = 0;
  char captureBrowserBasePath_[96] = {};
  uint32_t lastTouchMs_ = 0;
  uint32_t ignoreTouchUntilMs_ = 0;
  uint32_t ignoreSetupTouchUntilMs_ = 0;
  char feedback_[40] = "Ready";
  uint32_t feedbackUntilMs_ = 0;
};
