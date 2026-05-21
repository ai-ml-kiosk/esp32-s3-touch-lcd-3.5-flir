#include "Application.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "board/BoardPins.h"
#include "board/DisplayDriver.h"
#include "board/TouchDriver.h"
#include "flir/LeptonVospi.h"
#include "flir/SyntheticLepton.h"
#include "pin_config.h"
#include "settings/SettingsStore.h"
#include "storage/CaptureStorage.h"
#include "thermal/ThermalProcessor.h"
#include "ui/ThermalUi.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint8_t kLeptonCciAddress = 0x2A;
constexpr uint16_t kLeptonCciRegStatus = 0x0002;
constexpr uint16_t kLeptonCciRegCommand = 0x0004;
constexpr uint16_t kLeptonCciRegDataLength = 0x0006;
constexpr uint16_t kLeptonCciBusyMask = 0x0001;
constexpr uint16_t kLeptonCciBootedMask = 0x0004;
constexpr uint16_t kLeptonOemRebootRunCommand = 0x4842;
constexpr uint32_t kFrameIntervalMs = 116;

DisplayDriver display;
TouchDriver touch;
TwoWire flirCciWire(1);
SettingsStore settingsStore;
CaptureStorage storage;
#if defined(FLIR_USE_SYNTHETIC_FRAMES)
SyntheticLepton lepton;
#else
LeptonVospi lepton;
#endif
ThermalProcessor thermal;
ThermalUi ui;
AppSettings settings;
ThermalFrame frame;
ThermalStats stats;
uint16_t* viewportPixels = nullptr;
uint16_t viewportWidth = 0;
uint16_t viewportHeight = 0;
uint32_t lastFrameMs = 0;
uint32_t lastStatusMs = 0;
bool needsRender = true;

void printPinMap() {
  Serial.println();
  Serial.println("Waveshare ESP32-S3-Touch-LCD-3.5B FLIR firmware");
  Serial.printf("LCD QSPI CS GPIO%d CLK GPIO%d D0..D3 GPIO%d,%d,%d,%d\n",
                BoardPins::LCD_QSPI_CS,
                BoardPins::LCD_QSPI_CLK,
                BoardPins::LCD_QSPI_D0,
                BoardPins::LCD_QSPI_D1,
                BoardPins::LCD_QSPI_D2,
                BoardPins::LCD_QSPI_D3);
  Serial.printf("Touch/board I2C SDA GPIO%d SCL GPIO%d\n", BoardPins::I2C_SDA, BoardPins::I2C_SCL);
  Serial.printf("TF card SD_MMC CLK GPIO%d CMD GPIO%d D0 GPIO%d\n", BoardPins::TF_CLK, BoardPins::TF_CMD, BoardPins::TF_D0);
  Serial.printf("FLIR VoSPI SCLK GPIO%d\n", Pins::FLIR_SPI_SCLK);
  Serial.printf("FLIR VoSPI MISO GPIO%d\n", Pins::FLIR_SPI_MISO);
  Serial.printf("FLIR VoSPI MOSI GPIO%d\n", Pins::FLIR_SPI_MOSI);
  Serial.printf("FLIR VoSPI CS   GPIO%d\n", Pins::FLIR_SPI_CS);
  Serial.printf("FLIR CCI SDA    GPIO%d\n", Pins::FLIR_CCI_SDA);
  Serial.printf("FLIR CCI SCL    GPIO%d\n", Pins::FLIR_CCI_SCL);
}

void scanI2cBus() {
  Serial.println("Scanning CCI/I2C bus...");
  bool foundAny = false;

  for (uint8_t address = 1; address < 127; ++address) {
    flirCciWire.beginTransmission(address);
    const uint8_t error = flirCciWire.endTransmission();
    if (error == 0) {
      foundAny = true;
      Serial.printf("  found 0x%02X", address);
      if (address == kLeptonCciAddress) {
        Serial.print(" (FLIR Lepton CCI expected)");
      }
      Serial.println();
    }
  }

  if (!foundAny) {
    Serial.println("  no I2C devices found");
  }
}

bool leptonCciPresent() {
  flirCciWire.beginTransmission(kLeptonCciAddress);
  return flirCciWire.endTransmission() == 0;
}

bool leptonCciWriteRegister(uint16_t reg, uint16_t value) {
  flirCciWire.beginTransmission(kLeptonCciAddress);
  flirCciWire.write(static_cast<uint8_t>(reg >> 8));
  flirCciWire.write(static_cast<uint8_t>(reg & 0xFF));
  flirCciWire.write(static_cast<uint8_t>(value >> 8));
  flirCciWire.write(static_cast<uint8_t>(value & 0xFF));
  return flirCciWire.endTransmission() == 0;
}

bool leptonCciReadRegister(uint16_t reg, uint16_t* value) {
  if (value == nullptr) {
    return false;
  }

  flirCciWire.beginTransmission(kLeptonCciAddress);
  flirCciWire.write(static_cast<uint8_t>(reg >> 8));
  flirCciWire.write(static_cast<uint8_t>(reg & 0xFF));
  if (flirCciWire.endTransmission(false) != 0) {
    return false;
  }

  if (flirCciWire.requestFrom(kLeptonCciAddress, static_cast<uint8_t>(2)) != 2) {
    return false;
  }

  *value = (static_cast<uint16_t>(flirCciWire.read()) << 8) | flirCciWire.read();
  return true;
}

bool waitForLeptonCciReady(uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    uint16_t status = 0;
    if (leptonCciReadRegister(kLeptonCciRegStatus, &status) && ((status & kLeptonCciBusyMask) == 0)) {
      return true;
    }
    delay(10);
  }
  return false;
}

bool waitForLeptonBootStatus(uint32_t timeoutMs) {
  const uint32_t started = millis();
  uint8_t stableBootReads = 0;
  while (millis() - started < timeoutMs) {
    uint16_t status = 0;
    if (leptonCciReadRegister(kLeptonCciRegStatus, &status) &&
        ((status & (kLeptonCciBootedMask | kLeptonCciBusyMask)) == kLeptonCciBootedMask)) {
      ++stableBootReads;
      if (stableBootReads >= 3) {
        Serial.printf("Lepton CCI boot status stable: 0x%04X\n", status);
        return true;
      }
    } else {
      stableBootReads = 0;
    }
    delay(100);
  }
  return false;
}

bool rebootLeptonViaCci() {
  if (!leptonCciPresent()) {
    Serial.println("Lepton CCI reboot skipped: no ACK at 0x2A");
    return false;
  }

  uint16_t status = 0;
  if (leptonCciReadRegister(kLeptonCciRegStatus, &status)) {
    Serial.printf("Lepton CCI status before reboot: 0x%04X\n", status);
  }

  if (!waitForLeptonCciReady(500)) {
    Serial.println("Lepton CCI reboot skipped: command interface busy");
    return false;
  }

  if (!leptonCciWriteRegister(kLeptonCciRegDataLength, 0) ||
      !leptonCciWriteRegister(kLeptonCciRegCommand, kLeptonOemRebootRunCommand)) {
    Serial.println("Lepton CCI reboot command failed to write");
    return false;
  }

  Serial.println("Lepton CCI OEM reboot requested");
  delay(250);
  const bool booted = waitForLeptonBootStatus(7000);
  if (booted) {
    delay(1500);
  } else {
    Serial.println("Lepton CCI reboot warning: boot status did not stabilize");
  }
  return booted;
}

void holdLeptonDeselected() {
  pinMode(Pins::FLIR_SPI_CS, OUTPUT);
  digitalWrite(Pins::FLIR_SPI_CS, HIGH);
  pinMode(Pins::FLIR_SPI_SCLK, OUTPUT);
  digitalWrite(Pins::FLIR_SPI_SCLK, LOW);
  pinMode(Pins::FLIR_SPI_MOSI, OUTPUT);
  digitalWrite(Pins::FLIR_SPI_MOSI, LOW);
}

void waitForLeptonColdBoot() {
  Serial.println("Waiting for Lepton CCI before VoSPI start...");
  const uint32_t started = millis();
  uint8_t stableAcks = 0;
  while (millis() - started < 5000) {
    if (leptonCciPresent()) {
      ++stableAcks;
      if (stableAcks >= 3) {
        Serial.println("Lepton CCI stable; allowing VoSPI boot settle");
        delay(1500);
        return;
      }
    } else {
      stableAcks = 0;
    }
    delay(100);
  }
  Serial.println("Lepton CCI did not become stable before VoSPI start");
}

void initializeFlirCciI2c() {
  flirCciWire.begin(Pins::FLIR_CCI_SDA, Pins::FLIR_CCI_SCL, kI2cFrequency);
  flirCciWire.setTimeOut(50);
}

bool allocateViewportBuffer() {
  const DisplayInfo info = display.info();
  viewportWidth = settings.landscape ? 420 : 304;
  viewportHeight = settings.landscape ? 208 : 334;
  if (viewportWidth > info.width) {
    viewportWidth = info.width;
  }
  if (viewportHeight > info.height - 80) {
    viewportHeight = info.height - 80;
  }

  const size_t bytes = static_cast<size_t>(viewportWidth) * viewportHeight * sizeof(uint16_t);
  if (viewportPixels != nullptr) {
    heap_caps_free(viewportPixels);
  }
  viewportPixels = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (viewportPixels == nullptr) {
    viewportPixels = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  }

  if (viewportPixels == nullptr) {
    Serial.printf("Failed to allocate viewport buffer: %u bytes\n", static_cast<unsigned>(bytes));
    return false;
  }
  Serial.printf("Viewport buffer: %ux%u (%u bytes)\n", viewportWidth, viewportHeight, static_cast<unsigned>(bytes));
  return true;
}

void updateOrientationIfNeeded(bool previousLandscape) {
  if (previousLandscape == settings.landscape) {
    return;
  }

  settingsStore.save(settings);
  display.setLandscape(settings.landscape);
  const DisplayInfo info = display.info();
  touch.setOrientation(settings.landscape, info.width, info.height);
  allocateViewportBuffer();
  needsRender = true;
}

void renderFrame() {
  if (frame.frameNumber != 0 && viewportPixels == nullptr) {
    return;
  }
  if (frame.frameNumber != 0) {
    thermal.renderRgb565(frame, ui.palette(), viewportPixels, viewportWidth, viewportHeight, &stats);
  }
  ui.render(display, frame, stats, viewportPixels, viewportWidth, viewportHeight, settings, storage.isMounted());
  display.flush();
}

}  // namespace

namespace Application {

void setup() {
  Serial.begin(kSerialBaud);
  delay(1500);

  printPinMap();
  holdLeptonDeselected();
  settingsStore.begin();
  settings = settingsStore.load();

  initializeFlirCciI2c();
  rebootLeptonViaCci();
  waitForLeptonColdBoot();

  if (!display.begin(settings.landscape)) {
    Serial.println("Display initialization failed; firmware will continue with serial diagnostics only");
  }
  const DisplayInfo displayInfo = display.info();
  touch.begin(settings.landscape, displayInfo.width, displayInfo.height);
  storage.begin();
  lepton.begin();
  ui.begin(display);
  ui.render(display, frame, stats, nullptr, 0, 0, settings, storage.isMounted());
  display.flush();
  allocateViewportBuffer();

  scanI2cBus();

#if defined(FLIR_USE_SYNTHETIC_FRAMES)
  Serial.println("FLIR viewer firmware ready. Synthetic frames enabled by build flag.");
#else
  Serial.println("FLIR viewer firmware ready. Reading real Lepton VoSPI frames.");
#endif
}

void loop() {
  const uint32_t now = millis();
  const bool previousLandscape = settings.landscape;

  if (now - lastFrameMs >= kFrameIntervalMs) {
    lastFrameMs = now;
    if (lepton.readFrame(frame)) {
      renderFrame();
      needsRender = false;
    }
  }

  TouchPoint touchPoint;
  if (touch.read(touchPoint)) {
    if (ui.handleTouch(touchPoint, settings, display, storage, frame, viewportPixels, viewportWidth, viewportHeight)) {
      updateOrientationIfNeeded(previousLandscape);
      settingsStore.save(settings);
      needsRender = true;
    }
  }

  if (needsRender) {
    renderFrame();
    needsRender = false;
  }

  if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    Serial.printf("frame=%lu orientation=%s palette=%d storage=%s heap=%u psram=%u",
                  static_cast<unsigned long>(frame.frameNumber),
                  settings.landscape ? "landscape" : "portrait",
                  static_cast<int>(ui.palette()),
                  storage.isMounted() ? "ready" : "not-mounted",
                  ESP.getFreeHeap(),
                  ESP.getFreePsram());
    Serial.printf(" cci_0x2A=%s cci_sda=%d cci_scl=%d",
                  leptonCciPresent() ? "yes" : "no",
                  Pins::FLIR_CCI_SDA,
                  Pins::FLIR_CCI_SCL);
#if !defined(FLIR_USE_SYNTHETIC_FRAMES)
    Serial.printf(" vospi_status=%d sync_loss=%lu recovery=%lu",
                  static_cast<int>(lepton.lastStatus()),
                  static_cast<unsigned long>(lepton.syncLossCount()),
                  static_cast<unsigned long>(lepton.recoveryCount()));
#endif
    Serial.println();
  }
}

}  // namespace Application
