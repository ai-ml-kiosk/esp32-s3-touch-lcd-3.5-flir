# Software Architecture

This project should copy the intent of the earlier GoVision thermal behavior,
not the host application implementation. The first firmware implementation
remains a PlatformIO Arduino project with embedded C++ modules.

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
- Orientation state and zoom state. Landscape is the default.
- Persisted viewer settings.

## Proposed ESP32-S3 Components

| Component | Responsibility |
|---|---|
| `lepton_vospi` | Dedicated SPI read loop, packet validation, frame assembly, resync. |
| `lepton_cci` | I2C/CCI control, status, optional FFC control, optional reset handling. |
| `thermal_math` | TLinear to Celsius conversion, min/max detection, auto range, palette mapping. |
| `display_driver` | Waveshare AXS15231B QSPI LCD initialization, drawing, backlight control. |
| `touch_driver` | Built-in AXS15231B I2C touch sampling, calibration, debouncing. |
| `capture_storage` | Optional BMP/raw frame capture and logs on the onboard TF card. |
| `setup_ui` | On-device settings screen for save path, locale/region, date/time format, and orientation. |
| `thermal_ui` | Main screen layout, buttons, orientation/zoom, high/low markers. |
| `settings` | Non-volatile settings in NVS, with optional TF-card config later. |

`src/main.cpp` should remain a thin entry point. The top-level lifecycle belongs
in `Application`, and each hardware device or feature should live behind its own
module boundary.

## Suggested FreeRTOS Task Model

| Task | Priority | Notes |
|---|---:|---|
| Lepton capture task | High | Owns FLIR SPI bus. Assembles newest full raw frame. |
| UI/render task | Medium | Converts latest raw frame to the active display-driver pixel format and updates the built-in LCD. |
| Touch/input task | Medium/low | Polls or waits for the built-in touch controller event. |
| Storage task | Low | Writes captures to TF card without blocking capture. |

Use double buffering:

- Buffer A: being filled by Lepton capture.
- Buffer B: latest complete raw frame for rendering.

Avoid letting TF-card writes block Lepton VoSPI capture.

## Display Pipeline

1. Capture complete 80x60 Lepton raw frame.
2. Convert raw values to Celsius if TLinear is enabled.
3. Calculate low/high display range.
4. Normalize to palette index.
5. Map palette internally at high precision, then let `display_driver` pack to
   the active AXS15231B QSPI pixel format. RGB565 is the first target unless
   RGB666 is verified in the selected driver. See [Display Driver Design](display-driver-design.md).
6. Scale to the active display viewport. Default orientation is 480x320 landscape.
7. Draw high/low markers and UI controls.

## First Firmware Milestone

Start with a minimal thermal-only screen:

- No touch controls.
- No TF-card writes.
- No Wi-Fi.
- Fixed palette.
- Fixed scale.
- Serial logs for SPI sync and frame rate.

Then add controls in this order:

1. Touch read and calibration.
2. Palette/range controls.
3. Orientation toggle.
4. Capture to TF card.
5. Settings persistence.
6. Optional manual FFC button.

## ESP32-S3 Constraints

- Lepton is low resolution, but VoSPI packet timing is strict.
- Full-screen updates on the built-in LCD can be expensive. The default full
  screen target is 480x320 landscape. Update only the thermal viewport when
  possible.
- Prefer high-precision palette math, but keep final pixel packing inside the
  display driver. Document whether the active AXS15231B QSPI path is RGB565 or
  RGB666.
- Keep FLIR VoSPI on its own pins to avoid long LCD/touch/TF-card transactions
  disturbing Lepton capture.
- If Wi-Fi is added later, test whether radio activity affects SPI timing or
  power stability.
