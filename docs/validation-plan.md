# Validation Plan

## Phase 1: Electrical Bring-Up

1. Power ESP32-S3 alone and confirm serial logging.
2. Bring up the built-in 3.5 inch AXS15231B QSPI LCD with the Waveshare 3.5B
   display driver.
3. Render a solid-color test pattern to the built-in LCD.
4. Read the built-in AXS15231B I2C touch controller and print raw/calibrated
   touch coordinates.
5. Mount the onboard TF card and run read/write tests.
6. Add FLIR power and I2C only; scan for CCI address.
7. Add FLIR VoSPI and attempt packet sync.
8. Initialize the FLIR Lepton 2.5, handling the synchronization of VoSPI packets (discarding 0x0F packets) and handling periodic FFC (Flat Field Correction) shutter clicks.
9. Press the ESP32 reset/reboot button and confirm the Lepton stream resumes
   without a full power cycle.

## Phase 2: FLIR Frame Capture

Expected checks:

- SPI mode matches Lepton breakout requirements.
- Packet size is 164 bytes for Lepton 2.x VoSPI.
- Frame is 80x60.
- Frame rate is around Lepton's normal rate, roughly 8.6Hz for common export
  modules.
- Packet discard/resync behavior recovers after temporary sync loss.
- Serial status includes `vospi_status`, `sync_loss`, and `recovery` counters.
- CCI scan detects the Lepton at `0x2A` on `GPIO17` SDA / `GPIO18` SCL.
- VoSPI uses the confirmed wiring: SCLK `GPIO21`, MISO `GPIO40`, MOSI
  `GPIO41`, CS `GPIO42`.

## Phase 3: Display Rendering

Expected checks:

- Thermal image appears with correct orientation.
- Landscape is the default orientation after a fresh settings reset.
- The status-bar orientation label switches between landscape and portrait.
- The active landscape output is 480x320 and logs the actual display pixel
  format selected by the AXS15231B QSPI driver.
- Hot/cold markers follow a warm object.
- Color scale remains stable enough for practical viewing.
- UI remains responsive while capture continues.

## Phase 4: Touch And Storage

Expected checks:

- Touch coordinates map correctly after calibration.
- Touch does not interfere with LCD drawing.
- TF-card writes do not stall Lepton capture.
- First-release captures include a monotonic sequence number. Timestamped names
  are validated later after RTC or network time is enabled.

## Phase 5: Stability Test

Run for at least 30 minutes:

- No watchdog resets.
- No permanent VoSPI sync loss.
- Reset-button reboot recovers: the firmware requests a Lepton OEM reboot over
  CCI before starting VoSPI, then frame count increases again.
- No display white-screen or SPI bus lockups.
- Thermal frame rate remains stable.
- ESP32-S3 regulator, LCD backlight, and FLIR breakout do not overheat.

## Issues Captured During Bring-Up

- Initial wiring attempts on `GPIO38/GPIO39/GPIO40/GPIO41` for VoSPI and
  `GPIO7/GPIO8` for CCI were replaced. The current working map keeps FLIR off
  board I2C and uses `GPIO21/40/41/42` plus `GPIO17/18`.
- A full power cycle and an ESP32 reset-button reboot are different cases.
  Breakout v1.4 leaves the Lepton powered during ESP32 reset because no
  separate `RST` or `PWR_EN` is exposed.
- Repeated VoSPI headers of `00 00 00 00` mean CCI can still be alive while the
  video SPI stream is idle or wedged. The firmware now detects that condition,
  restarts the SPI peripheral, and performs startup CCI reboot of the Lepton.
- Display flicker was reduced by batching UI drawing and flushing the display
  once per rendered frame.
