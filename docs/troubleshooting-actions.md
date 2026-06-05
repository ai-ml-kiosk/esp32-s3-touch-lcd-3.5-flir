# Troubleshooting Actions Log

This document records the troubleshooting and stabilization work performed for
the Waveshare ESP32-S3-Touch-LCD-3.5B Type B plus FLIR Lepton 2.5 breakout v1.4
project. It is intended for both users bringing up hardware and developers
continuing the firmware.

## Current Known-Good Baseline

- Board: Waveshare ESP32-S3-Touch-LCD-3.5B Type B.
- Firmware framework: PlatformIO Arduino.
- Display path: built-in AXS15231B QSPI LCD.
- Touch path: built-in AXS15231B I2C touch.
- Orientation: QMI8658 accelerometer auto-rotation with a 500 ms debounce.
- Storage: onboard TF card through board support.
- Camera: FLIR Lepton 2.5 on breakout board v1.4.
- FLIR wiring:

| FLIR signal | ESP32-S3 GPIO |
|---|---:|
| `CLK` / `SCLK` | `GPIO21` |
| `MISO` / `VoSPI` | `GPIO40` |
| `MOSI` | `GPIO41` |
| `CS` | `GPIO42` |
| `SDA` / CCI SDA | `GPIO17` |
| `SCL` / CCI SCL | `GPIO18` |
| `VIN` | `3V3` |
| `GND` | `GND` |

## User Troubleshooting Checklist

### Screen Is Black

Checks performed:

- Verified display initialization with boot color flashes: red, green, blue,
  white.
- Added a bright bring-up screen showing `DISPLAY OK / WAITING FOR LEPTON
  VOSPI`.
- Added bottom color bars so the display path can be validated independently of
  FLIR.

What to do:

- If the color flashes do not appear, troubleshoot LCD/backlight/display driver
  first.
- If the display test appears but the thermal image does not, troubleshoot FLIR
  VoSPI wiring and Lepton boot state.

### Display Works But It Waits For Lepton

Observed symptoms:

- Serial repeatedly prints `frame=0`.
- Serial shows `vospi_status=2` and increasing `sync_loss`.
- Screen stays on `WAITING FOR LEPTON VOSPI`.

Checks performed:

- Confirmed Lepton voltage and ground.
- Confirmed CCI wiring separately from VoSPI wiring.
- Verified the same Lepton module works on another ESP32 board and Jetson Nano.
- Moved FLIR pins away from built-in display/TF/I2C pin conflicts.
- Settled on the known-good FLIR map listed above.

What to do:

- Recheck `CLK`, `MISO`, `MOSI`, `CS`, `SDA`, `SCL`, `VIN`, and `GND`.
- Do not swap CCI `SDA`/`SCL` with VoSPI `MISO`/`CLK`.
- Keep FLIR off `GPIO7`/`GPIO8`, which belong to board I2C.
- Keep FLIR off `GPIO9`/`GPIO10`/`GPIO11` when TF-card support is enabled.

### Image Appears Then Disappears After ESP32 Reboot

Observed symptom:

- Pressing the ESP32 reset button sometimes left the Lepton video stream wedged
  because the Lepton breakout stayed powered while only the ESP32 restarted.

Actions performed:

- Added CCI startup detection at Lepton address `0x2A`.
- Added Lepton OEM reboot over CCI during ESP32 startup.
- Added VoSPI recovery escalation when frame reads stop advancing.
- Added logs for reset reason, VoSPI status, sync loss, recovery count, heap,
  and PSRAM.

What to do:

- Use a full power cycle if reset-button recovery does not restore frames.
- Watch Serial for recovery messages and whether `frame` begins incrementing.

### Screen Turns Red Or Freezes

Observed causes addressed:

- Automatic Lepton idle sleep could leave VoSPI unrecovered on wake.
- Blocking capture/review file operations could starve the UI loop.
- Repeated touch actions could retrigger review or palette actions too quickly.

Actions performed:

- Disabled automatic Lepton idle sleep until CCI wake plus VoSPI recovery is
  reliable on this hardware.
- Kept manual Serial `sleep` and `wake` commands for backend-only testing.
- Added a PMIC-first software power switch path using the bottom-right power
  icon and Serial `poweroff`: Lepton low-power is requested, then the firmware
  verifies AXP2101 at I2C `0x34` and writes the PMIC shutdown bit. If that fails,
  it falls back to blanking the LCD, turning off the backlight, and suppressing
  frame rendering until touch or Serial `wake` restores the app.
- Treat the software power icon like a shutdown control, not a sleep button.
  The first press opens a centered confirmation prompt. `NO` cancels; `YES`
  starts shutdown. After confirmation, firmware shows a shutdown overlay,
  finishes any active clip write, closes playback files, flushes raw/BMP capture
  files, then requests Lepton low-power and PMIC shutdown. Avoid intentionally
  pressing it during heavy TF-card activity when possible, but the normal
  power-button path is designed to complete firmware-owned writes before power
  is cut. After a confirmed PMIC shutdown, wake requires physical `PWR`, charger
  insertion, or power reconnect; touch
  wake applies only to the fallback soft-off state.
- Added cooperative `yield()` calls in BMP save, BMP load, raw save, and capture
  directory loops.
- Released review thumbnail buffers when closing review.
- Added touch debounce and edge-trigger handling for main actions.

What to do:

- Confirm `low_power=sleep` is not appearing from automatic idle behavior.
- If the screen freezes, check whether Serial still prints runtime status.
- If `frame` stops increasing, treat it as VoSPI recovery rather than display
  failure.

### Buttons Require Many Presses Or Feel Slow

Actions performed:

- Removed an overly strict multi-sample touch filter.
- Kept action handling edge-triggered so holding a finger does not repeatedly
  trigger actions.
- Reduced review prev/next/delete work where possible.
- Avoided blocking redraw work after closing review.

What to do:

- Tap once and release; do not hold a finger on the action.
- If only one screen has poor touch behavior, verify orientation mapping after
  auto-rotation.

### Captures Save Unexpected Files

Actions performed:

- Capture filename format changed to `thermal_YYYYmmddHHMMSS`.
- Added `_02`, `_03`, and later suffixes when multiple captures happen in the
  same second.
- Fixed raw-save toggle behavior so `.raw` is written only when setup `Raw` is
  enabled.
- Capture review now opens the latest saved BMP first.

Current behavior:

- BMP is always the primary capture artifact.
- Raw `.raw` is optional and controlled by setup `Raw`.
- `Show Filename` controls whether the filename is written into the BMP footer.
- Runtime hot/cold and center status-bar icons control BMP annotation overlays
  and temperature footer content at the moment `CAP` is pressed.
- Review-page image and clip navigation must use sorted base filenames rather
  than TF-card directory order; otherwise Prev/Next can appear mixed after
  deletions or many captures.
- Thermal clip files store raw Lepton frames for low-resource recording. Review
  playback redraws high/low, center, and custom marker overlays after decoding
  each frame rather than storing rendered overlays in the clip file.
- Clip recording latches the saved setup `Clip` duration when recording starts.
  Review image mode hides play/pause controls; review clip mode keeps the
  progress bar separate from the saved filename line.
- The recording timeout must compare against a fresh frame-time value, not a
  stale loop timestamp captured before recording started; otherwise unsigned
  time subtraction can save the clip immediately.
- Clip review rewinds and reloads the first frame when play is pressed after
  the clip reached the end.
- Review header shows the configured working directory and TF-card
  used/total/free capacity from `SD_MMC.usedBytes()` and `SD_MMC.totalBytes()`.

## Developer Notes

### Firmware Backup And Rollback

Before flashing experimental firmware, copy the current combined factory image
to a local backup directory. Keep these binaries local; `firmware-backups/` is
ignored by Git.

Use segmented flashing for normal firmware updates when you want to preserve
setup values. Writing the combined `firmware.factory.bin` at `0x0` can erase the
NVS settings partition and make setup values appear to return to defaults after
the flash.

```bash
/private/tmp/platformio-test-esp32-s3/penv/bin/python /private/tmp/platformio-test-esp32-s3/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 \
  --port /dev/cu.usbmodem1301 \
  --baud 921600 \
  write_flash \
  0x0 .pio/build/waveshare-esp32-s3-touch-lcd-35b-flir/bootloader.bin \
  0x8000 .pio/build/waveshare-esp32-s3-touch-lcd-35b-flir/partitions.bin \
  0xe000 /private/tmp/platformio-test-esp32-s3/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/waveshare-esp32-s3-touch-lcd-35b-flir/firmware.bin
```

Current rollback backup created during stabilization:

```text
firmware-backups/firmware-20260605-usbcdc-setup-touch.factory.bin
sha256 4760ddab82ce5331c8cf6bdaf100cac2991948f972e5105918a372ce709e56df
```

Create a new backup after a successful build:

```bash
mkdir -p firmware-backups
cp .pio/build/waveshare-esp32-s3-touch-lcd-35b-flir/firmware.factory.bin firmware-backups/firmware-YYYYMMDD-description.factory.bin
shasum -a 256 firmware-backups/firmware-YYYYMMDD-description.factory.bin
```

Restore a backup through the ESP32-S3 USB flashing path:

```bash
python /private/tmp/platformio-test-esp32-s3/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 \
  --port /dev/cu.usbmodem1301 \
  --baud 921600 \
  write_flash 0x0 firmware-backups/firmware-YYYYMMDD-description.factory.bin
```

Use the actual current serial port if macOS assigns a different
`/dev/cu.usbmodem*` name.

### Display And UI

- `DisplayDriver` wraps the Waveshare AXS15231B display path.
- Runtime diagrams and firmware use icon buttons for bottom actions.
- Status bar includes:
  - orientation indicator only,
  - `FULL` / `2X` zoom toggle,
  - hot/cold annotation toggle,
  - center annotation toggle,
  - custom marker clear icon,
  - live/storage status text,
  - battery icon with PMIC percentage/source summary.
- Two-finger touches are ignored by design. Zoom is a status-bar tap action so
  pinch attempts cannot accidentally place custom spot-temperature markers.
- The bottom action row includes Palette, image quality, Capture, Review, and
  Setup. Runtime manual FFC was removed after the CCI run-FFC path repeatedly
  froze the live UI; use setup Auto FFC instead.
- Image quality modes are `DETAIL`, `BAL`, and `SMOOTH`; use `SMOOTH` if the
  display is noisy, and `DETAIL` if motion feels too soft.
- Palette scale temperatures and readouts must remain independent from
  annotation toggles.

### Battery And External Power Monitoring

- The firmware reads the onboard AXP2101 PMIC at I2C `0x34` every 5 seconds.
- Runtime status logs include `pmic`, `power_source`, `battery`, `battery_pct`,
  and `charge`.
- The status-bar battery icon shows the PMIC fuel-gauge percentage when a
  battery is detected. A charging mark indicates external power is present.
- Pressing the battery icon opens an on-screen summary with PMIC detection,
  source, battery presence, charging phase, and basic health flags.
- Firmware reports the AXP2101 power path; it does not manually force external
  power or battery routing. If external-power freezes return, correlate the
  freeze with `power_source=external`, `charge`, and PMIC health flags.
- If icons stop responding after USB/external power is disconnected, suspect a
  board-I2C touch-controller disturbance during the AXP2101 power-path
  transition. Firmware now detects PMIC source changes, reinitializes the touch
  controller mapping, clears stale touch-edge state, and logs `Power source
  changed ... touch controller reinitialized`.
- Status-bar icons use enlarged full-height touch targets and the main UI now
  receives touch-release samples so control latches can clear even when running
  from battery.

### FLIR Acquisition

- `LeptonVospi` owns FLIR SPI packet reads, discard-packet filtering, packet
  numbering checks, resync, and recovery counters.
- CCI helper code handles boot/status checks and OEM reboot.
- Breakout v1.4 has no firmware-controlled power-enable/reset pin in this
  wiring, so software recovery relies on CCI and VoSPI resync.

### Capture And Review

- `CaptureStorage::saveCapture()` reads `AppSettings` at capture time.
- BMP footer size changes based on annotation and filename options.
- BMP marker overlays match runtime options:
  - hot/cold on: square hot/cold markers and labels,
  - hot/cold off: no hot/cold markers or labels,
  - center on: plus marker and center label,
  - center off: no center marker or label.
- Review uses scaled BMP preview and latest capture lookup.

### Build, Upload, And Monitor Commands

Use no-proxy commands in this environment:

```bash
env -u HTTP_PROXY -u HTTPS_PROXY -u http_proxy -u https_proxy -u ALL_PROXY -u all_proxy pio run
env -u HTTP_PROXY -u HTTPS_PROXY -u http_proxy -u https_proxy -u ALL_PROXY -u all_proxy pio run --target upload --upload-port /dev/cu.usbmodem1301
env -u HTTP_PROXY -u HTTPS_PROXY -u http_proxy -u https_proxy -u ALL_PROXY -u all_proxy pio device monitor --port /dev/cu.usbmodem1301 --baud 115200
```

### Useful Serial Signals

Watch these fields during diagnosis:

```text
frame=<n>
orientation=landscape|portrait
palette=<n>
storage=ready|missing
heap=<bytes>
psram=<bytes>
low_power=awake|sleep
vospi_status=<n>
sync_loss=<n>
recovery=<n>
```

Interpretation:

- `frame` increasing means thermal acquisition is alive.
- `frame=0` after display bring-up means no complete Lepton frame yet.
- Rising `sync_loss` means VoSPI packet alignment or electrical signaling is
  failing.
- `storage=missing` means capture should be disabled until TF-card mount works.
- `low_power=sleep` should only appear after explicit manual Serial sleep while
  automatic idle sleep remains disabled.

## Open Follow-Ups

- Revisit automatic idle sleep only after CCI wake and VoSPI recovery are proven
  reliable over repeated sleep/wake cycles.
- Add a real RTC or network time source if build-time seeded timestamps are not
  sufficient.
- Consider reducing BMP write latency further if capture still causes visible
  pauses.
- Add a small diagnostic screen or Serial command to dump current touch
  coordinates when debugging future button alignment issues.
