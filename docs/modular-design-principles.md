# Modular Design Principles

This firmware is expected to grow with more sensors, UI features, storage
targets, and connectivity. Keep the source tree modular from the start so new
devices can be added without rewriting the FLIR app.

## Core Rules

- `src/main.cpp` must stay thin. It should call application setup and loop
  functions only.
- Hardware-specific code belongs under a board support layer, not inside app
  logic.
- Each device driver owns one physical device or bus-facing protocol.
- Feature modules depend on interfaces or small data types, not directly on
  unrelated drivers.
- Long-running work must be isolated in tasks or state machines. Avoid blocking
  the UI/render loop or Lepton capture loop.
- Shared state must move through explicit buffers, queues, or immutable snapshots.
  Avoid global cross-module variables except for carefully scoped singletons at
  the application composition layer.
- Pin assignments live in `include/pin_config.h`; no module should hard-code GPIO
  numbers.
- Driver fallbacks, such as RGB565 instead of RGB666, must be documented inside
  the driver that makes the compromise.

## Recommended Source Layout

| Path | Responsibility |
|---|---|
| `include/Application.h`, `src/Application.cpp` | Top-level orchestration and module lifecycle. |
| `include/board/`, `src/board/` | Waveshare board support: display, touch, TF card, backlight, power. |
| `include/flir/`, `src/flir/` | Lepton CCI, VoSPI, frame assembly, FFC, camera status. |
| `include/thermal/`, `src/thermal/` | Frame math, palettes, scaling, spot detection, temperature conversion. |
| `include/ui/`, `src/ui/` | Screen layout, touch actions, orientation handling, visual state. |
| `include/storage/`, `src/storage/` | Capture persistence and metadata writing. |
| `include/settings/`, `src/settings/` | Persistent app settings such as palette, range mode, and orientation. |
| `include/common/`, `src/common/` | Small shared types with no hardware dependency. |

## Dependency Direction

Keep dependencies flowing inward:

```text
main
  -> Application
      -> board drivers
      -> FLIR drivers
      -> thermal processing
      -> UI
      -> storage
      -> settings
```

Device drivers must not call UI modules. UI modules must not configure GPIO
directly. Thermal processing must not know about SPI, I2C, touch, SD, or display
driver details.

## Module Contract Pattern

Each module should expose a small lifecycle API:

```cpp
bool begin();
void update();
```

Modules that produce or consume data should use explicit structs:

```cpp
struct ThermalFrame {
  uint16_t width;
  uint16_t height;
  const uint16_t* raw;
};
```

Prefer passing references to interfaces or data snapshots over reaching into
another module's internal state.

## Adding A New Device

When adding a device:

1. Add pins or bus settings to `include/pin_config.h`.
2. Create a driver module under the matching domain folder.
3. Give the driver a `begin()` method and a non-blocking `update()` or explicit
   read/write methods.
4. Register the driver in `Application`.
5. Add UI/settings/storage integration in separate modules if needed.
6. Update the board profile, docs, and validation checks for the new device.

## Review Checklist

Before merging a feature:

- `src/main.cpp` still only delegates to `Application`.
- New code has one clear owner folder.
- No new GPIO numbers are hard-coded outside `pin_config.h`.
- UI, storage, and device driver code are not tangled together.
- Blocking operations are isolated from Lepton capture and screen refresh.
- Settings that affect behavior survive reboot if users would expect them to.
