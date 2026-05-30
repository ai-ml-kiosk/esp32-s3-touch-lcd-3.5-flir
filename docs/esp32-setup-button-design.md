# ESP32 Setup Button Design

This document defines the embedded C++ design for the `SETUP` button on the
Waveshare ESP32-S3-Touch-LCD-3.5B FLIR app. This is an on-device LCD/touch flow,
not a host-computer dialog.

![ESP32 setup screen flow](assets/esp32-setup-screen-flow.svg)

## Runtime Behavior

- `SETUP` opens an on-device setup screen or overlay rendered by the firmware UI.
- Only one setup view can be active at a time.
- While setup is active, normal thermal controls are disabled except for setup
  navigation, `Save`, and `Cancel`.
- `Save` validates settings, updates the runtime settings object, persists them,
  and returns to the thermal viewer.
- `Cancel` discards pending edits from the setup draft and returns to the
  thermal viewer.

## Initial Data

The setup screen shows live firmware state from `AppSettings`:

| Field | Source |
|---|---|
| Save path | Active TF-card capture directory, default `/flir`. |
| Auto Rotate | Whether QMI8658 gravity-vector auto-rotation is enabled. |
| Orientation | Manual `Landscape` / `Portrait` choice used when Auto Rotate is off. |
| Idle sleep | Touch inactivity timeout in seconds. `Off` disables automatic Lepton sleep. |
| Calibration offset | Temperature display offset in degrees C. |
| Show Filename | Whether saved BMP images include the filename in the bottom footer. |
| Save raw frame | Whether `CAP` writes the binary `.raw` file next to the BMP. |

If no settings have been saved yet, firmware should use these defaults:

| Setting | Default |
|---|---|
| Save path | `/flir` |
| Auto Rotate | `On` |
| Orientation | `Landscape` when Auto Rotate is off |
| Idle sleep | `Off` / `0` seconds |
| Calibration offset | `+0.0C` |
| Show Filename | `On` |
| Save raw frame | `On` |

## Touch Inactivity Sleep

The ESP32 app can detect touch-based inactivity by recording the last time the
touch controller reported a pressed point. This is deliberately based on user
touches, not thermal-frame activity, because the Lepton stream is expected to
keep running even when no user is present.

Runtime policy:

- Every valid touch press updates `lastUserActivityMs`.
- If `idleSleepSeconds` is `0`, automatic sleep is disabled.
- If `idleSleepSeconds` is greater than `0` and no touch is seen before the
  timeout expires, firmware requests the existing backend Lepton CCI sleep path.
- Automatic idle sleep turns the LCD backlight off after rendering the bottom
  status message, reducing screen power draw and avoiding a glowing idle panel.
- The first touch while the Lepton is asleep wakes the Lepton and is consumed by
  the wake action. It must not also trigger the UI button underneath the finger.
- Touch wake and Serial `wake` turn the backlight on before recovering Lepton
  VoSPI. Manual Serial `sleep` leaves the screen on for diagnostics.
- Setup mode suppresses automatic sleep so users are not interrupted while
  changing settings.
- Serial `sleep` and `wake` remain available for diagnostics.

Setup should expose `Idle sleep` as seconds presets:

```text
Off -> 30 sec -> 60 sec -> 120 sec -> 300 sec -> 600 sec -> Off
```

This keeps the UI small enough for the 3.5 inch screen and avoids a full numeric
keyboard until the setup module grows richer text/number editing controls.

## Temperature Calibration Offset

The first calibration setting is a simple display offset. It corrects displayed
temperature readouts without changing raw thermal frames or saved `.raw` files.

```text
correctedC = measuredC + offsetC
```

Setup should expose `Cal` as a touch-cycled value:

```text
-5.0C -> -4.5C -> ... -> +0.0C -> ... -> +5.0C -> -5.0C
```

Firmware stores this as tenths of a degree in NVS. The offset applies to center,
hot, cold, and palette-scale labels only.

## Save Path Rules

The ESP32 app saves captures on the onboard TF card. The setup path is a TF-card
directory path, not a host computer path.

Validation rules:

- Path must be absolute and start with `/`.
- Path must not be empty.
- Path must not contain traversal segments such as `..`.
- Directory must exist or be creatable on the TF card.
- Firmware must verify it can write a small temporary file before accepting the
  path.

If validation fails, show an error message on the setup screen and keep the user
in setup mode.

## Capture Path Contract

`CAP` must read the active save path from the runtime settings object at capture
time. It must not keep a separate cached path.

```text
SETUP Save
  -> Settings.savePath updated
  -> Settings persisted to NVS or TF-card config

CAP pressed
  -> CaptureStorage reads Settings.savePath
  -> CaptureStorage validates/mount-checks TF card
  -> raw frame and annotated image file are written under that directory
```

Captured file base names use the following format when the ESP32 system clock is
valid:

```text
thermal_YYYYmmddHHMMSS
```

For example:

```text
thermal_20260525143005.bmp
thermal_20260525143005.raw
```

If the ESP32 clock has not been set by an RTC or network time source, firmware
falls back to monotonic names such as `thermal_00001`. If two captures happen
within the same second, the second and later captures receive a suffix such as
`_02`.

Saved BMP images may include a black footer below the thermal image. Temperature
footer content is controlled by runtime status-bar annotation toggles at capture
time, not by setup fields:

```text
HIGH <temp>C  LOW <temp>C  CTR <temp>C
```

If high/low annotation is off, `HIGH` and `LOW` are omitted. If center annotation
is off, `CTR` is omitted. If both annotation toggles are off and `Show Filename`
is also off, no footer is added. The filename footer line is controlled by the
setup `Show Filename` toggle:

```text
FILE frame_00001.bmp
```

Raw `.raw` files remain unannotated binary Lepton data. The setup `Raw` switch
controls whether raw files are written: green/right means raw saving is enabled,
gray/left means only the annotated BMP is saved. The annotated BMP is always
written.

The current firmware seeds system time from the build timestamp until a real
RTC/NTP source is added, so filenames use `thermal_YYYYmmddHHMMSS` during normal
operation and fall back to monotonic `thermal_00001` style names only if no valid
time source is available.

The runtime capture-review icon opens the latest saved capture path. Its Delete
action removes the matching `.bmp` and optional `.raw` pair from the TF card.

## Proposed C++ Modules

| Module | Responsibility |
|---|---|
| `settings` | Runtime settings model, defaults, persistence, validation helpers. |
| `setup_ui` | On-device setup screen, field editing, Save/Cancel actions. |
| `capture_storage` | Capture file naming and writes under `Settings.savePath`. |
| `touch_driver` | Touch events mapped to setup controls when setup is active. |
| `thermal_ui` | Opens setup screen, capture review/delete, and resumes thermal viewer after Save/Cancel. |

## Suggested Types

```cpp
struct AppSettings {
  char savePath[64] = "/flir";
  bool landscape = true;
  bool autoRotate = true;
  uint16_t inactivitySleepSeconds = 0;
  int8_t temperatureOffsetTenths = 0;
  bool includeFilenameInCapture = true;
  bool saveRawCapture = true;
};

enum class SetupResult {
  None,
  Saved,
  Cancelled,
};
```

## UI Layout

The implemented setup overlay is compact and touch-friendly. In landscape it
uses two columns; in portrait it uses a single scrollable column with fixed
Cancel/Save actions at the bottom.

```text
+------------------------------------------------+
| SETUP                                          |
| Path:       /flir        Auto:       [ ON ]    |
| Idle:       Off          Orient:     Auto      |
| Cal:        +0.0C        Show Name:  [ ON ]    |
|                         Raw:        [ ON ]     |
|                                                |
| [Cancel]                              [Save]   |
+------------------------------------------------+
```

Boolean fields are rendered as toggle switches:

- Green track, knob right, `ON`: enabled.
- Gray track, knob left, `OFF`: disabled.

The setup panel must remain scrollable as future settings are added. Save and
Cancel stay fixed at the bottom of the setup panel while field rows can scroll
within the content area.

For text entry on the board, start with a small set of safe path presets:

- `/flir`
- `/flir/captures`
- `/thermal`

A full on-screen keyboard can be added later behind the same `setup_ui` module.

## Persistence

Prefer NVS for compact settings. If users need to edit settings outside firmware,
also support a TF-card config file such as `/flir/config.json` later.

Minimum requirement:

- Load settings during application startup.
- Use defaults if settings are missing or invalid.
- Persist settings after setup `Save`.
- Keep runtime settings updated immediately after setup `Save`.
