# Hardware Design

## Target Hardware

The intended hardware stack is:

- Waveshare ESP32-S3-Touch-LCD-3.5B development board with ESP32-S3R8,
  16 MB flash, 8 MB PSRAM, built-in 3.5 inch 320x480 capacitive LCD/touch,
  262K-color display output, built-in power management, and onboard TF card
  socket. The built-in display/touch controller is AXS15231B, with QSPI display
  communication and I2C touch communication.
- FLIR Lepton 2.5 thermal module on breakout board v1.4. This breakout provides
  the required power rails plus SPI/VoSPI and I2C/CCI pins, but does not expose
  separate FLIR `PWR_EN` or `RST` pins.

External TFT, external touch, and separate TF-card wiring are not part of this
hardware design. LCD, touch, backlight, and TF card support must use the
Waveshare 3.5B onboard circuits and demo/driver configuration.

## Electrical Rules

- Use 3.3V logic for all ESP32-S3 GPIO, SPI, I2C, reset, and interrupt lines.
- Do not feed 5V logic into ESP32-S3 pins.
- FLIR Lepton 2.5 breakout v1.4 is treated as a 3.3V-class interface in this
  project. Confirm your board markings before applying power.
- All modules must share a common ground.
- Use short wiring for FLIR VoSPI, especially SCLK, MISO, MOSI, CS, and ground.
- Do not reuse GPIOs already assigned to the built-in LCD/touch unless the board
  schematic is changed.

## Bus Allocation

Use separate hardware paths for display and FLIR:

| Bus | Purpose | Devices | Reason |
|---|---|---|---|
| Onboard LCD/touch path | User interface | Built-in 320x480 AXS15231B LCD/touch and backlight, used as 480x320 landscape by default | Already wired by Waveshare; use the board support pins and drivers. |
| FLIR SPI | Thermal frame capture | FLIR Lepton VoSPI | Keeps Lepton packet timing isolated from LCD/touch and TF-card transactions. |

The confirmed FLIR SPI bus uses exposed expansion pins that are independent of
the built-in LCD, touch, TF-card, and board I2C paths: `GPIO21` SCLK,
`GPIO40` MISO, `GPIO41` MOSI, and `GPIO42` CS. In this project FLIR MOSI is a
required connection, not an optional placeholder.

Use a dedicated exposed I2C pair for FLIR CCI:

| Bus | Purpose | Devices |
|---|---|---|
| FLIR I2C/CCI | Lepton control/status | FLIR Lepton CCI address, typically `0x2A`, on `GPIO17` SDA and `GPIO18` SCL. |

Keep `GPIO7` and `GPIO8` reserved for the Waveshare board I2C path. Earlier
bring-up attempts that reused `GPIO7/GPIO8` for FLIR CCI were replaced by the
independent `GPIO17/GPIO18` map.

## Power Budget Notes

- ESP32-S3 Wi-Fi, the built-in LCD backlight, TF writes, and the Lepton module
  can create burst current. Use a stable USB-C or battery supply and confirm the
  3.3V rail can support the external FLIR breakout.
- If thermal frames fail when the display backlight or TF card is active, test
  with the backlight dimmed and TF card removed to isolate power dips from SPI
  timing problems.

## Mechanical And Optical Notes

- The Lepton image is 80x60 and low frame-rate compared with the display.
- The UI should default to landscape orientation on the built-in 480x320 display.
  On the Type B board variant, orientation changes are detected from the onboard
  QMI8658 accelerometer on the internal I2C bus. The app should auto-rotate
  after a sustained 500 ms gravity-vector change and must update display
  rendering orientation and touch coordinate mapping together. Scale the thermal
  image while preserving aspect ratio or deliberately using a fill mode.
- The final display target should use the AXS15231B QSPI display path. Preserve
  palette calculations internally at high precision, then pack to RGB565 unless
  the selected driver explicitly supports RGB666/262K frame pushes. The
  implementation pattern is defined in [Display Driver Design](display-driver-design.md).
- If a visible camera is added later, treat thermal/visible alignment as a
  per-device calibration problem. For this first ESP32-S3 project, the scope is
  thermal-only display.
