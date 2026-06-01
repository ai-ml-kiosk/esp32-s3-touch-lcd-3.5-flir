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
  void applySettings(const AppSettings& settings);
  void render(DisplayDriver& display,
              const ThermalFrame& frame,
              const ThermalStats& stats,
              const uint16_t* viewportPixels,
              uint16_t viewportWidth,
              uint16_t viewportHeight,
              const AppSettings& settings,
              CaptureStorage& storage,
              bool storageReady,
              const char* lastCaptureBasePath);
  bool handleTouch(const TouchPoint& touch,
                   AppSettings& settings,
                   DisplayDriver& display,
                   CaptureStorage& storage,
                   ThermalProcessor& thermal,
                   const ThermalFrame& frame,
                   const ThermalStats& stats,
                   const uint16_t* viewportPixels,
                   uint16_t viewportWidth,
                   uint16_t viewportHeight);

  PaletteMode palette() const { return palette_; }
  bool zoomed() const { return zoomed_; }
  bool setupActive() const { return setupActive_; }
  bool playbackActive() const { return playbackActive_; }
  bool gestureActive() const { return pinchActive_; }
  bool consumeVideoClipRequest();
  bool consumeSoftPowerRequest();
  enum class SoundEvent {
    None,
    Click,
    Alert,
    Scroll,
  };
  SoundEvent consumeSoundEvent();
  void setVideoClipActive(bool active);
  bool updatePlayback(CaptureStorage& storage,
                      ThermalProcessor& thermal,
                      const AppSettings& settings,
                      uint16_t viewportWidth,
                      uint16_t viewportHeight);
  void showStatus(const char* message);

 private:
  enum class Action {
    None,
    Palette,
    Noise,
    Capture,
    VideoClip,
    Playback,
    Setup,
    HotColdDetails,
    CenterTemperature,
    ClearCustomMarkers,
    Sound,
    SoftPower,
    PlaybackClose,
    PlaybackDelete,
    PlaybackPrev,
    PlaybackNext,
    PlaybackImageTab,
    PlaybackClipTab,
    PlaybackPlayPause,
    SetupScrollUp,
    SetupScrollDown,
    SetupCancel,
    SetupSave,
    SetupPath,
    SetupIdle,
    SetupIdleDown,
    SetupTempOffset,
    SetupTempOffsetDown,
    SetupAutoRotate,
    SetupOrientation,
    SetupFilename,
    SetupRaw,
    SetupAutoFfc,
    SetupClipDuration,
    SetupClipDurationDown,
    SetupSoundVolume,
    SetupSoundVolumeDown,
  };

  struct Rect {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
  };

  struct CustomMarker {
    bool active = false;
    uint16_t xPermil = 0;
    uint16_t yPermil = 0;
  };

  Action hitTest(uint16_t x, uint16_t y, bool landscape) const;
  void cyclePalette(AppSettings& settings);
  void cycleImageQuality(AppSettings& settings);
  void cyclePath(AppSettings& settings);
  void cycleIdleSleep(AppSettings& settings);
  void decreaseIdleSleep(AppSettings& settings);
  void cycleTemperatureOffset(AppSettings& settings);
  void decreaseTemperatureOffset(AppSettings& settings);
  void toggleAutoRotate(AppSettings& settings);
  void toggleManualOrientation(AppSettings& settings);
  void toggleFilenameFooter(AppSettings& settings);
  void toggleRawCapture(AppSettings& settings);
  void toggleAutoFfc(AppSettings& settings);
  void cycleClipDuration(AppSettings& settings);
  void decreaseClipDuration(AppSettings& settings);
  void cycleSoundVolume(AppSettings& settings);
  void decreaseSoundVolume(AppSettings& settings);
  void storeCapturePreview(const uint16_t* viewportPixels, uint16_t viewportWidth, uint16_t viewportHeight);
  bool ensureCapturePreviewBuffer(uint16_t viewportWidth, uint16_t viewportHeight);
  bool ensurePlaybackClipFrame();
  bool ensureCaptureThumbnailBuffer(uint16_t thumbW, uint16_t thumbH);
  void releasePlaybackClipFrame();
  void releaseCaptureThumbnail();
  bool loadBrowserPreview(CaptureStorage& storage, uint16_t viewportWidth, uint16_t viewportHeight);
  bool loadClipPreview(CaptureStorage& storage,
                       ThermalProcessor& thermal,
                       const AppSettings& settings,
                       uint16_t viewportWidth,
                       uint16_t viewportHeight);
  void renderSetup(DisplayDriver& display, const AppSettings& settings);
  void refreshCaptureBrowser(CaptureStorage& storage, const AppSettings& settings);
  void renderPlayback(DisplayDriver& display, const AppSettings& settings, CaptureStorage& storage);
  void renderWaiting(DisplayDriver& display, const AppSettings& settings, bool storageReady);
  void drawButton(DisplayDriver& display, const Rect& rect, const char* label, bool primary = false);
  void drawToggle(DisplayDriver& display, const Rect& rect, bool enabled);
  void drawIconButton(DisplayDriver& display, const Rect& rect, Action action, bool primary = false);
  void drawActionIcon(DisplayDriver& display, int16_t cx, int16_t cy, Action action, uint16_t color);
  void drawHotColdIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled);
  void drawCenterTempIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled);
  void drawClearMarkersIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled);
  void drawSoundIcon(DisplayDriver& display, int16_t cx, int16_t cy, bool enabled);
  void drawOrientationIcon(DisplayDriver& display, int16_t x, int16_t y, bool landscape);
  void drawTempLabel(DisplayDriver& display, int16_t x, int16_t y, const char* text, uint16_t color);
  void addCustomMarker(uint16_t localX, uint16_t localY, uint16_t viewportWidth, uint16_t viewportHeight);
  void clearCustomMarkers();
  float customMarkerTemperature(const ThermalFrame& frame,
                                const AppSettings& settings,
                                const CustomMarker& marker) const;
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
  void drawPlaybackClipMarkers(DisplayDriver& display,
                               int16_t x,
                               int16_t y,
                               uint16_t w,
                               uint16_t h,
                               const AppSettings& settings);
  void drawFeedback(DisplayDriver& display, bool landscape);
  bool contains(const Rect& rect, uint16_t x, uint16_t y) const;
  bool handleSetupTouch(const TouchPoint& touch,
                        AppSettings& settings,
                        CaptureStorage& storage);
  bool executeSetupAction(Action action,
                          AppSettings& settings,
                          CaptureStorage& storage);
  bool handleSetupDrag(const TouchPoint& touch, bool landscape);
  bool handlePinchZoom(AppSettings& settings, const TouchPoint& touch);
  int16_t maxSetupScroll(bool landscape) const;

  PaletteMode palette_ = PaletteMode::Ironbow;
  ThermalFrame* playbackClipFrame_ = nullptr;
  bool zoomed_ = false;
  bool setupActive_ = false;
  bool playbackActive_ = false;
  bool hasCapturePreview_ = false;
  bool hasCaptureThumbnail_ = false;
  bool videoClipRequest_ = false;
  bool softPowerRequest_ = false;
  bool videoClipActive_ = false;
  SoundEvent pendingSoundEvent_ = SoundEvent::None;
  AppSettings setupDraftSettings_{};
  int16_t setupScrollY_ = 0;
  bool pinchActive_ = false;
  bool pinchChanged_ = false;
  bool pinchStartZoomed_ = false;
  uint16_t pinchStartDistance_ = 0;
  bool setupDragActive_ = false;
  bool setupDragMoved_ = false;
  bool setupControlHeld_ = false;
  Action setupPendingAction_ = Action::None;
  uint16_t setupDragStartY_ = 0;
  uint16_t setupDragLastY_ = 0;
  uint32_t setupDragLastMs_ = 0;
  uint16_t* capturePreviewPixels_ = nullptr;
  uint16_t* captureThumbnailPixels_ = nullptr;
  uint16_t capturePreviewWidth_ = 0;
  uint16_t capturePreviewHeight_ = 0;
  uint16_t captureThumbnailWidth_ = 0;
  uint16_t captureThumbnailHeight_ = 0;
  uint16_t captureBrowserIndex_ = 0;
  uint16_t captureBrowserCount_ = 0;
  bool playbackClipMode_ = false;
  bool clipPlaying_ = false;
  bool clipAtEnd_ = false;
  bool hasPlaybackClipStats_ = false;
  ThermalStats playbackClipStats_{};
  uint32_t clipFrameIndex_ = 0;
  uint32_t clipFrameCount_ = 0;
  uint32_t lastClipPlaybackMs_ = 0;
  CustomMarker customMarkers_[3] = {};
  uint8_t customMarkerCount_ = 0;
  char captureBrowserBasePath_[96] = {};
  uint32_t lastTouchMs_ = 0;
  uint32_t ignoreTouchUntilMs_ = 0;
  uint32_t ignoreSetupTouchUntilMs_ = 0;
  char feedback_[40] = "Ready";
  uint32_t feedbackUntilMs_ = 0;
};
