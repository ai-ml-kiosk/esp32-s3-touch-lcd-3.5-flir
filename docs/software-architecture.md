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
- Auto-rotation state from the onboard QMI8658 and zoom state. Landscape is the
  default.
- Persisted viewer settings.

## Proposed ESP32-S3 Components

| Component | Responsibility |
|---|---|
| `lepton_vospi` | Dedicated SPI read loop, packet validation, frame assembly, resync. Implemented by `LeptonVospi`. |
| `lepton_cci` | I2C/CCI control, status, optional FFC control, optional reset handling. |
| `thermal_math` | TLinear to Celsius conversion, min/max detection, auto range, palette mapping. |
| `display_driver` | Waveshare AXS15231B QSPI LCD initialization, drawing, backlight control. |
| `touch_driver` | Built-in AXS15231B I2C touch sampling, calibration, debouncing. |
| `capture_storage` | Optional BMP/raw frame capture and logs on the onboard TF card. |
| `setup_ui` | On-device settings screen for save path, locale/region, date/time format, orientation, touch inactivity sleep, and temperature offset calibration. |
| `thermal_ui` | Main screen layout, buttons, orientation/zoom, status-bar annotation toggles, high/low markers, center marker. |
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
- Serial logs for SPI sync, VoSPI status, sync-loss count, and frame rate.
- Startup Lepton CCI reboot and VoSPI recovery counters.
- Backend-only Serial Monitor commands for Lepton CCI standby/wake testing.

Then add controls in this order:

1. Touch read and calibration.
2. Palette/range controls.
3. QMI8658 auto-rotation with 500 ms debounce.
4. Capture to TF card.
5. Settings persistence.
6. Optional manual FFC button.
7. Optional Lepton sleep/wake UI only after backend serial testing is reliable.

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
- Treat reset-button reboot as a warm camera restart. The ESP32 resets, but the
  breakout v1.4 Lepton remains powered; firmware should reassert CS high,
  reboot the Lepton over CCI when available, wait for camera boot/CCI stability,
  then start VoSPI.
- Treat CCI sleep/wake as a backend-controlled power state. The firmware pauses
  VoSPI before OEM power-down, recovers the CCI bus before software power-on,
  waits for boot status, and restarts VoSPI before accepting the feature as UI
  ready.
- Automatic touch-idle sleep should also turn off the LCD backlight through the
  display driver. Wake paths must restore the backlight before camera recovery
  status is shown.
- Startup must also tolerate a Lepton left in software power-down by a previous
  firmware session. Send the CCI power-on register sequence before the normal
  startup OEM reboot and VoSPI begin.
- Repeated all-zero or all-`0xFF` VoSPI packet headers should be handled as an
  invalid signal stream, not as packet zero. Restart the SPI peripheral and
  resync before continuing.
- If Wi-Fi is added later, test whether radio activity affects SPI timing or
  power stability.
