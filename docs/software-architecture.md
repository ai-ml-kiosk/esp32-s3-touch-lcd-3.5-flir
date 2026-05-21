# Software Architecture

This project should copy the intent of GoVision's `core/thermal.py` and
`ui/thermal_ui.py`, not the Python implementation directly. The ESP32-S3
firmware should be written as embedded C/C++ components or Arduino/PlatformIO
modules.

See [Modular Design Principles](modular-design-principles.md) for the source
layout, dependency rules, and review checklist that should be followed as new
features and devices are added.

## GoVision Logic To Preserve

From `core/thermal.py`:

- FLIR Lepton configuration.
- SPI bus open/init.
- VoSPI packet synchronization.
- Raw 80x60 frame assembly.
- Error handling and resync behavior.
- Optional reset/enable support.
- TLinear raw value conversion to Celsius.

From `ui/thermal_ui.py`:

- Live thermal viewer.
- Color palette mapping.
- Auto low/high percentile range.
- Sensitivity adjustment.
- Hot/cold spot detection.
- Capture control.
- Orientation/rotation and zoom state. Landscape is the default.
- Persisted viewer settings.

## Proposed ESP32-S3 Components

| Component | Responsibility |
|---|---|
| `lepton_vospi` | Dedicated SPI read loop, packet validation, frame assembly, resync. |
| `lepton_cci` | I2C/CCI control, status, optional FFC control, optional reset handling. |
| `thermal_math` | TLinear to Celsius conversion, min/max detection, auto range, palette mapping. |
| `display_driver` | Waveshare ESP32-S3-Touch-LCD-3.5B LCD initialization, DMA drawing, backlight control. |
| `touch_driver` | Built-in capacitive touch sampling, calibration, debouncing. |
| `sd_storage` | Optional BMP/raw frame capture and logs. |
| `thermal_ui` | Main screen layout, buttons, orientation/zoom, high/low markers. |
| `settings` | Non-volatile settings in NVS or JSON on SD card. |

`src/main.cpp` should remain a thin entry point. The top-level lifecycle belongs
in `Application`, and each hardware device or feature should live behind its own
module boundary.

## Suggested FreeRTOS Task Model

| Task | Priority | Notes |
|---|---:|---|
| Lepton capture task | High | Owns FLIR SPI bus. Assembles newest full raw frame. |
| UI/render task | Medium | Converts latest raw frame to the active 262K-color display format and updates the built-in LCD. |
| Touch/input task | Medium/low | Polls or waits for the built-in touch controller event. |
| Storage task | Low | Writes captures to SD without blocking capture. |

Use double buffering:

- Buffer A: being filled by Lepton capture.
- Buffer B: latest complete raw frame for rendering.

Avoid letting TF-card writes block Lepton VoSPI capture.

## Display Pipeline

1. Capture complete 80x60 Lepton raw frame.
2. Convert raw values to Celsius if TLinear is enabled.
3. Calculate low/high display range.
4. Normalize to palette index.
5. Map palette to RGB666/262K color output, or RGB565 only as a driver fallback.
6. Scale to the active display viewport. Default orientation is 480x320 landscape.
7. Draw high/low markers and UI controls.

## First Firmware Milestone

Start with a minimal thermal-only screen:

- No touch controls.
- No SD card writes.
- No Wi-Fi.
- Fixed palette.
- Fixed scale.
- Serial logs for SPI sync and frame rate.

Then add controls in this order:

1. Touch read and calibration.
2. Palette/range controls.
3. Orientation toggle.
4. Capture to SD.
5. Settings persistence.
6. Optional manual FFC button.

## ESP32-S3 Constraints

- Lepton is low resolution, but VoSPI packet timing is strict.
- Full-screen updates on the built-in LCD can be expensive. The default full
  screen target is 480x320 landscape. Update only the thermal viewport when
  possible.
- Prefer RGB666/262K-color output for the final palette-rendered image so the
  display matches the hardware capability. If memory or driver support forces a
  fallback, document the RGB565 conversion explicitly in the display driver.
- Keep FLIR VoSPI on its own pins to avoid long LCD/touch/TF-card transactions
  disturbing Lepton capture.
- If Wi-Fi is added later, test whether radio activity affects SPI timing or
  power stability.
