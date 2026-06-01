# Waveshare 3.5B Board Profile

This file is the board-level source of truth for code generation. Use it before
the generic app design docs whenever a driver, bus, or color-format choice
depends on the physical Waveshare ESP32-S3-Touch-LCD-3.5B hardware.

## Verified Board Facts

| Area | Value |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-3.5B |
| MCU module | ESP32-S3R8 |
| Flash | 16 MB |
| PSRAM | 8 MB |
| LCD resolution | 320x480 physical pixels |
| Default app orientation | 480x320 landscape |
| Display colors | 262K panel capability |
| Display driver IC | AXS15231B |
| Display interface | QSPI, not parallel RGB |
| Touch interface | I2C through the AXS15231B touch path |
| Motion sensor | QMI8658 6-axis IMU on internal I2C |
| Onboard storage | TF card socket |
| Useful vendor demos | `07_sd_test`, `08_gfx_helloworld`, `09_lvgl_arduino_v8`, `10_lvgl_arduino_v9` |

## Framework Decision

Keep this repository as a PlatformIO Arduino project for the first generated
firmware stage.

Rationale:

- The project is already scaffolded as PlatformIO + Arduino.
- Waveshare provides Arduino examples for this board.
- Waveshare's 3.5B demos use `GFX_Library_for_Arduino`, `TCA9554`, and
  `esp_lcd_touch_axs15231b`.
- The actual board display path is QSPI AXS15231B, so an
  `esp_lcd_rgb_panel_config_t` parallel RGB implementation is not the primary
  path for this hardware.

ESP-IDF-native firmware can be added later, but it should target an AXS15231B
QSPI panel driver, not the RGB panel driver, unless the hardware changes.

## Display Driver Direction

First implementation target:

1. Initialize the board power/IO expander path required by the Waveshare demo.
2. Initialize the AXS15231B display through the QSPI display path.
3. Use a driver-native pixel format for screen pushes.
4. Keep palette calculations internally high precision.
5. Convert to the selected driver pixel format only in the display layer.

The display layer must expose a small app-facing API:

```cpp
enum class DisplayPixelFormat {
  Rgb565,
  Rgb666,
};

struct DisplayInfo {
  uint16_t width;
  uint16_t height;
  DisplayPixelFormat pixelFormat;
};

class DisplayDriver {
 public:
  bool begin();
  DisplayInfo info() const;
  bool drawBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const void* pixels);
};
```

For this board, expect `DisplayPixelFormat::Rgb565` unless the selected
AXS15231B QSPI library explicitly supports a 262K/RGB666 transfer mode.

## Color Policy

The panel advertises 262K colors, but that does not by itself guarantee that the
selected software path can push RGB666 frames. The firmware should:

- Preserve thermal palette calculations at 8-bit-per-channel precision.
- Prefer RGB666 only if the AXS15231B QSPI driver supports it cleanly.
- Use RGB565 as the first implementation fallback and document the fallback in
  the display driver logs.

This means the thermal math code should not depend on the display bus color
depth. Only the display driver owns the final pixel packing.

## Touch Driver Direction

Touch is not a generic external interrupt-only design. Use the Waveshare
AXS15231B touch driver path over I2C through a `touch_driver` module.

The touch module should publish debounced events:

```cpp
struct TouchPoint {
  uint16_t x;
  uint16_t y;
  bool pressed;
};

class TouchDriver {
 public:
  bool begin();
  bool read(TouchPoint& point);
};
```

The UI layer owns mapping touch coordinates to controls such as `PAL`, image
quality, `CAP`, capture review/delete, `SETUP`, hot/cold annotation, center
annotation, custom marker placement, and pinch zoom. The orientation glyph is an
indicator only; orientation changes come from QMI8658 auto-rotation.

## IMU Auto-Rotation Direction

The Type B board includes a QMI8658 6-axis IMU on the internal I2C bus. Firmware
should initialize the accelerometer and classify the raw gravity vector into
landscape or portrait. On the tested board mounting, landscape is Y-axis
dominant and portrait is X-axis dominant. When a new orientation remains
dominant for at least 500 ms, the app should:

1. Update the display orientation matrix.
2. Reinitialize touch coordinate mapping for the new display size.
3. Reallocate the thermal viewport buffer.
4. Re-render the active screen and persist the selected orientation.

Do not use a software-only orientation toggle button or an external orientation
pin trigger for this board profile.

## TF Card Direction

Use the onboard TF card path from Waveshare's board support/demo configuration.
External SPI wiring for storage is out of scope. The storage module should be
named `capture_storage` in code and docs so it does not imply a generic SD-card
bus owner.

## FLIR Wiring Impact

The FLIR module remains external and uses the project pin map:

| FLIR signal | ESP32-S3 GPIO |
|---|---:|
| `CLK` / `SCK` / `SCLK` | `GPIO21` |
| `MISO` / `DATA` / `VoSPI` | `GPIO40` |
| `MOSI` | `GPIO41` |
| `CS` | `GPIO42` |
| `SDA` / CCI SDA | `GPIO17` |
| `SCL` / CCI SCL | `GPIO18` |

Do not assign FLIR signals to onboard LCD, touch, QSPI display, or TF-card pins.
Keep `GPIO7` and `GPIO8` available for the board I2C path.
