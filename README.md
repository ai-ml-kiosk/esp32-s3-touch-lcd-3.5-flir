# ESP32-S3 Touch LCD 3.5B FLIR

PlatformIO firmware scaffold for the Waveshare ESP32-S3-Touch-LCD-3.5B board
and a FLIR Lepton 2.5 module on breakout board v1.4.

The board-specific source of truth is
[Waveshare 3.5B Board Profile](docs/waveshare-35b-board-profile.md). This
project keeps the first implementation on PlatformIO Arduino and targets the
board's AXS15231B QSPI display and I2C touch path.

The default firmware path reads real FLIR Lepton 2.x VoSPI frames. A synthetic
thermal frame source remains available only for bring-up builds with
`FLIR_USE_SYNTHETIC_FRAMES`.

## Build

```bash
pio run
```

## Upload

```bash
pio run --target upload
```

For a specific serial port:

```bash
pio run --target upload --upload-port /dev/cu.usbmodem1301
```

PlatformIO uses Espressif's `esptool` internally for ESP32-S3 flashing. You can
see this in upload logs as `tool-esptoolpy` and `esptool v...`, so a normal
source-tree upload does not require installing `esptool.py` separately.

## Release Firmware Binaries

Tagged releases publish downloadable firmware assets through GitHub Actions.
Create and push a version tag to build and attach both the app image and the
factory image to a GitHub Release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Release assets:

- `waveshare-esp32-s3-touch-lcd-35b-flir-firmware.bin`: application firmware.
- `waveshare-esp32-s3-touch-lcd-35b-flir-firmware.factory.bin`: combined image
  for full restore from offset `0x0`.
- `README-flash.md`: flashing commands for the release assets.

Preferred upload from this source tree remains PlatformIO:

```bash
pio run --target upload --upload-port /dev/cu.usbmodem1301
```

For a full restore from a downloaded release asset, flash the factory image at
offset `0x0` with Espressif `esptool`. This may be available as `esptool.py` or
`esptool`, depending on how it was installed:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem1301 --baud 921600 write_flash 0x0 waveshare-esp32-s3-touch-lcd-35b-flir-firmware.factory.bin
```

If `esptool.py` is not on your shell path, install/use it through PlatformIO or
Python:

```bash
python -m pip install esptool
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem1301 --baud 921600 write_flash 0x0 waveshare-esp32-s3-touch-lcd-35b-flir-firmware.factory.bin
```

To build the same artifacts locally:

```bash
bash scripts/build_release.sh
```

## Monitor

```bash
pio device monitor
```

## Setup Button Design

The `SETUP` button is an embedded ESP32 firmware feature. It should open an
on-device LCD/touch setup screen for settings such as TF-card save path,
orientation policy, calibration offset, raw/filename capture options, Auto FFC,
clip duration, sound volume, and reserved scrollable fields.

See [ESP32 Setup Button Design](docs/esp32-setup-button-design.md).

For hardware bring-up notes, known symptoms, and debugging actions already
performed on this board, see
[Troubleshooting Actions Log](docs/troubleshooting-actions.md).

## FLIR Wiring

| FLIR breakout signal | ESP32-S3 GPIO |
|---|---:|
| `CLK` / `SCK` / `SCLK` | `GPIO21` |
| `MISO` / `DATA` / `VoSPI` | `GPIO40` |
| `MOSI` | `GPIO41` |
| `CS` | `GPIO42` |
| `SDA` / CCI SDA | `GPIO17` |
| `SCL` / CCI SCL | `GPIO18` |

The Waveshare LCD, touch controller, backlight, and TF card are built in. Do not
reuse their GPIOs for external FLIR wiring. In particular, keep `GPIO7` and
`GPIO8` reserved for the board I2C path.
