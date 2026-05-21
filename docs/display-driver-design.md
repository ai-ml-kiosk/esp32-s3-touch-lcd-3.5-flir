# Display Driver Design

This document defines the display implementation policy for this project. For
board-specific facts, start with [Waveshare 3.5B Board Profile](waveshare-35b-board-profile.md).

## Primary Path For This Board

The Waveshare ESP32-S3-Touch-LCD-3.5B uses an AXS15231B display/touch controller.
The display path is QSPI and the touch path is I2C. Therefore, the first firmware
implementation must not use `esp_lcd_rgb_panel_config_t` for this board.

The first implementation should stay with PlatformIO Arduino and follow the
Waveshare Arduino demo stack:

- `GFX_Library_for_Arduino` for LCD drawing.
- `TCA9554` for the Waveshare board control path when required by the demo.
- `esp_lcd_touch_axs15231b` for touch.
- Waveshare's TF-card demo configuration for storage.

## Driver Boundary

The app should depend on a small display abstraction, not on the concrete display
library:

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

`thermal_ui` should ask `DisplayDriver::info()` for the active orientation,
physical size, and pixel format. Thermal math should never include display bus
details.

## Color Depth Policy

The LCD panel advertises 262K colors, but the software path must match the
selected AXS15231B QSPI driver.

First implementation rule:

- Preserve palette calculations internally at high precision.
- Use RGB565 for the first board bring-up unless the selected AXS15231B driver
  clearly supports RGB666/262K frame pushes.
- Keep any RGB565 fallback inside `display_driver`.
- Log the active display pixel format during startup.

This lets the app keep a future RGB666 path without lying to the code generator
about what the current board/library path can guarantee.

## Rendering Pipeline

```text
thermal_math palette output
        |
        v
driver-independent color buffer
        |
        v
display_driver packs to active format
  - RGB565 for first AXS15231B QSPI implementation
  - RGB666 only if verified by selected driver
        |
        v
AXS15231B QSPI display push
```

## Touch Binding

Touch is independent from the display rendering API. Use a `touch_driver` module
that wraps the AXS15231B I2C touch path and publishes debounced touch points to
the UI layer.

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

The display driver must not read touch state directly.

## Alternate Parallel RGB Path

The earlier `esp_lcd_rgb_panel_config_t` design is retained only as an alternate
board strategy. Use it for a different Waveshare board that physically exposes a
parallel RGB panel, not for the ESP32-S3-Touch-LCD-3.5B.

For ESP32-S3 RGB panels, Espressif documents RGB data width as 8 or 16 lines.
Any RGB666/18-bit path must be enabled only after the target board and selected
driver explicitly support that wider color transfer. Otherwise, force RGB565.

