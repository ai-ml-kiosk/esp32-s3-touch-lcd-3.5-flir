#include "Application.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>
#include <strings.h>

#include "board/BoardPins.h"
#include "board/DisplayDriver.h"
#include "board/ImuDriver.h"
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
constexpr uint16_t kLeptonCciRegPowerOn = 0x0000;
constexpr uint16_t kLeptonCciBusyMask = 0x0001;
constexpr uint16_t kLeptonCciBootedMask = 0x0004;
constexpr uint16_t kLeptonCciResponseMask = 0xFF00;
constexpr uint16_t kLeptonCciResponseOk = 0x0000;
constexpr uint16_t kLeptonOemPowerDownRunCommand = 0x4802;
constexpr uint16_t kLeptonOemRebootRunCommand = 0x4842;
constexpr uint32_t kFrameIntervalMs = 116;
constexpr uint32_t kImuPollIntervalMs = 100;
constexpr uint32_t kAutoRotationDebounceMs = 500;
constexpr int32_t kAutoRotationMinAccel = 2500;
constexpr int32_t kAutoRotationAxisMargin = 800;
constexpr size_t kSerialCommandCapacity = 48;

DisplayDriver display;
TouchDriver touch;
ImuDriver imu;
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
uint32_t lastUserActivityMs = 0;
uint32_t lastGoodFrameMs = 0;
uint32_t lastLeptonEscalationMs = 0;
uint32_t lastObservedRecoveryCount = 0;
uint32_t lastImuPollMs = 0;
uint32_t lastTouchActionMs = 0;
uint32_t orientationCandidateSinceMs = 0;
ImuAccelRaw lastImuAccel;
bool needsRender = true;
bool leptonLowPower = false;
bool displayOffForIdleSleep = false;
bool touchWasActive = false;
bool orientationCandidateLandscape = true;
bool hasOrientationCandidate = false;
bool lastImuReadOk = false;
bool lastImuClassified = false;
bool lastImuLandscape = true;
uint32_t leptonLowPowerSinceMs = 0;
char serialCommandBuffer[kSerialCommandCapacity] = {};
size_t serialCommandLength = 0;

void renderFrame();

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
      return "task-watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

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
  Serial.println("Board IMU QMI8658 on internal I2C bus for auto-rotation");
  Serial.printf("TF card SD_MMC CLK GPIO%d CMD GPIO%d D0 GPIO%d\n", BoardPins::TF_CLK, BoardPins::TF_CMD, BoardPins::TF_D0);
  Serial.printf("FLIR VoSPI SCLK GPIO%d\n", Pins::FLIR_SPI_SCLK);
  Serial.printf("FLIR VoSPI MISO GPIO%d\n", Pins::FLIR_SPI_MISO);
  Serial.printf("FLIR VoSPI MOSI GPIO%d\n", Pins::FLIR_SPI_MOSI);
  Serial.printf("FLIR VoSPI CS   GPIO%d\n", Pins::FLIR_SPI_CS);
  Serial.printf("FLIR CCI SDA    GPIO%d\n", Pins::FLIR_CCI_SDA);
  Serial.printf("FLIR CCI SCL    GPIO%d\n", Pins::FLIR_CCI_SCL);
}

int monthFromBuildDate(const char* month) {
  static constexpr const char* kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* found = strstr(kMonths, month);
  if (found == nullptr) {
    return 1;
  }
  return static_cast<int>((found - kMonths) / 3) + 1;
}

void seedSystemTimeFromBuild() {
  tm buildTime = {};
  char month[4] = {};
  int day = 1;
  int year = 2026;
  int hour = 0;
  int minute = 0;
  int second = 0;

  if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) != 3 ||
      sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
    Serial.println("Build time parse failed; capture names may use sequence fallback");
    return;
  }

  buildTime.tm_year = year - 1900;
  buildTime.tm_mon = monthFromBuildDate(month) - 1;
  buildTime.tm_mday = day;
  buildTime.tm_hour = hour;
  buildTime.tm_min = minute;
  buildTime.tm_sec = second;
  buildTime.tm_isdst = -1;

  const time_t epoch = mktime(&buildTime);
  if (epoch <= 0) {
    Serial.println("Build time epoch invalid; capture names may use sequence fallback");
    return;
  }

  timeval now = {};
  now.tv_sec = epoch;
  settimeofday(&now, nullptr);
  Serial.printf("System time seeded from firmware build: %04d-%02d-%02d %02d:%02d:%02d\n",
                year,
                buildTime.tm_mon + 1,
                day,
                hour,
                minute,
                second);
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

const char* cciResponseName(uint16_t status) {
  const uint16_t response = status & kLeptonCciResponseMask;
  if (response == kLeptonCciResponseOk) {
    return "ok";
  }
  return "error";
}

bool runLeptonCciCommand(uint16_t command, const char* label, uint32_t readyTimeoutMs = 700) {
  if (!leptonCciPresent()) {
    Serial.printf("Lepton CCI %s skipped: no ACK at 0x%02X\n", label, kLeptonCciAddress);
    return false;
  }

  uint16_t status = 0;
  if (leptonCciReadRegister(kLeptonCciRegStatus, &status)) {
    Serial.printf("Lepton CCI status before %s: 0x%04X\n", label, status);
  }

  if (!waitForLeptonCciReady(readyTimeoutMs)) {
    Serial.printf("Lepton CCI %s skipped: command interface busy\n", label);
    return false;
  }

  if (!leptonCciWriteRegister(kLeptonCciRegDataLength, 0) ||
      !leptonCciWriteRegister(kLeptonCciRegCommand, command)) {
    Serial.printf("Lepton CCI %s command failed to write\n", label);
    return false;
  }

  if (!waitForLeptonCciReady(readyTimeoutMs)) {
    Serial.printf("Lepton CCI %s warning: command did not complete before timeout\n", label);
    return false;
  }

  if (!leptonCciReadRegister(kLeptonCciRegStatus, &status)) {
    Serial.printf("Lepton CCI %s warning: unable to read command response\n", label);
    return false;
  }

  const bool ok = (status & kLeptonCciResponseMask) == kLeptonCciResponseOk;
  Serial.printf("Lepton CCI %s response=%s status=0x%04X\n", label, cciResponseName(status), status);
  return ok;
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

bool rebootLeptonViaCci(uint32_t bootTimeoutMs = 7000) {
  if (!runLeptonCciCommand(kLeptonOemRebootRunCommand, "OEM reboot", 700)) {
    return false;
  }

  Serial.println("Lepton CCI OEM reboot requested; waiting for boot");
  delay(250);
  const bool booted = waitForLeptonBootStatus(bootTimeoutMs);
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

void recoverLeptonCciBusForWake() {
  flirCciWire.end();
  pinMode(Pins::FLIR_CCI_SDA, INPUT_PULLUP);
  pinMode(Pins::FLIR_CCI_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(Pins::FLIR_CCI_SCL, HIGH);
  delayMicroseconds(20);
  digitalWrite(Pins::FLIR_CCI_SCL, LOW);
  delayMicroseconds(20);
  digitalWrite(Pins::FLIR_CCI_SCL, HIGH);
  delayMicroseconds(20);
  initializeFlirCciI2c();
  Serial.println("Lepton CCI wake bus recovery pulse sent");
}

bool writeLeptonPowerOnWithRecovery(const char* label) {
  recoverLeptonCciBusForWake();

  for (uint8_t attempt = 1; attempt <= 3; ++attempt) {
    if (leptonCciWriteRegister(kLeptonCciRegPowerOn, 0x0000)) {
      Serial.printf("Lepton CCI power-on write accepted for %s on attempt %u\n", label, attempt);
      return true;
    }
    Serial.printf("Lepton CCI power-on write failed for %s on attempt %u; retrying bus recovery\n", label, attempt);
    recoverLeptonCciBusForWake();
    delay(50);
  }

  Serial.printf("Lepton CCI power-on write failed for %s\n", label);
  return false;
}

void ensureLeptonPoweredForStartup() {
  Serial.println("Ensuring Lepton CCI power-on state before startup reboot");
  if (writeLeptonPowerOnWithRecovery("startup")) {
    delay(950);
  }
}

bool requestLeptonLowPower() {
#if defined(FLIR_USE_SYNTHETIC_FRAMES)
  Serial.println("sleep ignored: synthetic FLIR mode has no Lepton CCI device");
  return false;
#else
  if (leptonLowPower) {
    Serial.println("Lepton already in requested low-power state");
    return true;
  }

  Serial.println("Lepton sleep requested over CCI");
  holdLeptonDeselected();
  const bool ok = runLeptonCciCommand(kLeptonOemPowerDownRunCommand, "OEM power-down", 1000);
  if (!ok) {
    Serial.println("Lepton sleep failed; keeping VoSPI active");
    return false;
  }

  leptonLowPower = true;
  leptonLowPowerSinceMs = millis();
  lastFrameMs = millis();
  Serial.println("Lepton low-power state entered; VoSPI reads paused");
  return true;
#endif
}

bool wakeLeptonFromLowPower() {
#if defined(FLIR_USE_SYNTHETIC_FRAMES)
  Serial.println("wake ignored: synthetic FLIR mode has no Lepton CCI device");
  return false;
#else
  if (!leptonLowPower) {
    Serial.println("Lepton is already awake; wake skipped");
    return true;
  }

  Serial.println("Lepton wake requested over CCI");
  holdLeptonDeselected();

  if (!writeLeptonPowerOnWithRecovery("wake")) {
    Serial.println("Lepton wake failed: power-on write was not ACKed");
    return false;
  }

  delay(950);
  if (!rebootLeptonViaCci(1500)) {
    Serial.println("Lepton wake warning: OEM reboot after power-on did not stabilize");
  }

  waitForLeptonColdBoot();
  lepton.begin();
  leptonLowPower = false;
  leptonLowPowerSinceMs = 0;
  lastFrameMs = 0;
  needsRender = true;
  Serial.printf("Lepton wake complete; VoSPI recovery=%lu sync_loss=%lu\n",
                static_cast<unsigned long>(lepton.recoveryCount()),
                static_cast<unsigned long>(lepton.syncLossCount()));
  return true;
#endif
}

void escalateLeptonVospiRecovery(const char* reason) {
#if defined(FLIR_USE_SYNTHETIC_FRAMES)
  (void)reason;
#else
  const uint32_t now = millis();
  if (now - lastLeptonEscalationMs < 10000) {
    return;
  }
  lastLeptonEscalationMs = now;

  Serial.printf("VoSPI escalation: %s; rebooting Lepton over CCI and restarting SPI\n", reason);
  ui.showStatus("Recovering Lepton...");
  frame.frameNumber = 0;
  needsRender = true;
  renderFrame();

  holdLeptonDeselected();
  if (!rebootLeptonViaCci(2500)) {
    Serial.println("VoSPI escalation warning: CCI reboot did not stabilize");
  }
  waitForLeptonColdBoot();
  lepton.begin();
  lastFrameMs = 0;
  lastGoodFrameMs = millis();
  lastObservedRecoveryCount = lepton.recoveryCount();
  ui.showStatus("Lepton recovery done");
  needsRender = true;
#endif
}

void printRuntimeStatus() {
  Serial.printf("frame=%lu orientation=%s palette=%d temp_offset=%.1fC storage=%s heap=%u psram=%u low_power=%s",
                static_cast<unsigned long>(frame.frameNumber),
                settings.landscape ? "landscape" : "portrait",
                static_cast<int>(ui.palette()),
                static_cast<float>(settings.temperatureOffsetTenths) / 10.0f,
                storage.isMounted() ? "ready" : "not-mounted",
                ESP.getFreeHeap(),
                ESP.getFreePsram(),
                leptonLowPower ? "sleep" : "awake");
  Serial.printf(" idle_sleep=%s",
                settings.inactivitySleepSeconds == 0 ? "off" : "on");
  if (settings.inactivitySleepSeconds != 0) {
    Serial.printf(" idle_timeout_s=%u idle_ms=%lu",
                  settings.inactivitySleepSeconds,
                  static_cast<unsigned long>(millis() - lastUserActivityMs));
  }
  if (leptonLowPower) {
    Serial.printf(" sleep_ms=%lu", static_cast<unsigned long>(millis() - leptonLowPowerSinceMs));
  }
  Serial.printf(" cci_0x2A=%s cci_sda=%d cci_scl=%d",
                leptonCciPresent() ? "yes" : "no",
                Pins::FLIR_CCI_SDA,
                Pins::FLIR_CCI_SCL);
  Serial.printf(" imu=%s accel=(%d,%d,%d)",
                imu.ready() ? (lastImuReadOk ? "ok" : "no-read") : "missing",
                lastImuAccel.x,
                lastImuAccel.y,
                lastImuAccel.z);
  if (lastImuReadOk) {
    Serial.printf(" imu_orientation=%s",
                  lastImuClassified ? (lastImuLandscape ? "landscape" : "portrait") : "flat/uncertain");
    if (hasOrientationCandidate) {
      Serial.printf(" candidate=%s candidate_ms=%lu",
                    orientationCandidateLandscape ? "landscape" : "portrait",
                    static_cast<unsigned long>(millis() - orientationCandidateSinceMs));
    }
  }
#if !defined(FLIR_USE_SYNTHETIC_FRAMES)
  Serial.printf(" vospi_status=%d sync_loss=%lu recovery=%lu",
                static_cast<int>(lepton.lastStatus()),
                static_cast<unsigned long>(lepton.syncLossCount()),
                static_cast<unsigned long>(lepton.recoveryCount()));
#endif
  Serial.println();
}

void printSerialHelp() {
  Serial.println("Serial commands: help, status, sleep, wake");
  Serial.println("  sleep  - request Lepton OEM power-down over CCI and pause VoSPI reads");
  Serial.println("  wake   - recover CCI bus, write power-on register, wait for boot, restart VoSPI");
  Serial.println("  status - print CCI ACK, low-power state, and VoSPI recovery counters");
}

void handleSerialCommand(const char* command) {
  if (command == nullptr || command[0] == '\0') {
    return;
  }

  if (strcasecmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    printSerialHelp();
  } else if (strcasecmp(command, "status") == 0) {
    printRuntimeStatus();
  } else if (strcasecmp(command, "sleep") == 0) {
    requestLeptonLowPower();
  } else if (strcasecmp(command, "wake") == 0) {
    if (displayOffForIdleSleep) {
      display.setBacklight(true);
      displayOffForIdleSleep = false;
    }
    wakeLeptonFromLowPower();
  } else {
    Serial.printf("Unknown command '%s'\n", command);
    printSerialHelp();
  }
}

void processSerialInput() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      serialCommandBuffer[serialCommandLength] = '\0';
      handleSerialCommand(serialCommandBuffer);
      serialCommandLength = 0;
      serialCommandBuffer[0] = '\0';
    } else if (serialCommandLength + 1 < kSerialCommandCapacity) {
      serialCommandBuffer[serialCommandLength++] = c;
    } else {
      serialCommandLength = 0;
      serialCommandBuffer[0] = '\0';
      Serial.println("Serial command too long; buffer cleared");
    }
  }
}

uint32_t inactivitySleepMs() {
  // Automatic Lepton sleep is disabled until CCI wake/recovery is reliable on
  // this hardware. Manual serial sleep/wake commands remain available.
  return 0;
}

bool allocateViewportBuffer() {
  const DisplayInfo info = display.info();
  viewportWidth = settings.landscape ? 420 : info.width - 44;
  viewportHeight = settings.landscape ? 208 : 318;
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
  touchWasActive = false;
  lastTouchActionMs = millis();
  allocateViewportBuffer();
  needsRender = true;
}

bool classifyLandscapeFromGravity(const ImuAccelRaw& accel, bool* landscape) {
  if (landscape == nullptr) {
    return false;
  }

  const int32_t absX = abs(static_cast<int32_t>(accel.x));
  const int32_t absY = abs(static_cast<int32_t>(accel.y));
  const int32_t dominant = absX > absY ? absX : absY;
  const int32_t difference = abs(absX - absY);

  if (dominant < kAutoRotationMinAccel || difference < kAutoRotationAxisMargin) {
    return false;
  }

  *landscape = absY > absX;
  return true;
}

bool settingsEqual(const AppSettings& left, const AppSettings& right) {
  return strcmp(left.savePath, right.savePath) == 0 &&
         strcmp(left.locale, right.locale) == 0 &&
         strcmp(left.dateFormat, right.dateFormat) == 0 &&
         strcmp(left.timeFormat, right.timeFormat) == 0 &&
         left.landscape == right.landscape &&
         left.autoRotate == right.autoRotate &&
         left.inactivitySleepSeconds == right.inactivitySleepSeconds &&
         left.temperatureOffsetTenths == right.temperatureOffsetTenths &&
         left.includeFilenameInCapture == right.includeFilenameInCapture &&
         left.saveRawCapture == right.saveRawCapture;
}

void updateAutoOrientation() {
  const uint32_t now = millis();
  if (!settings.autoRotate || !imu.ready() || ui.setupActive() || now - lastImuPollMs < kImuPollIntervalMs) {
    return;
  }
  lastImuPollMs = now;

  ImuAccelRaw accel;
  bool candidateLandscape = settings.landscape;
  lastImuReadOk = imu.readAccel(accel);
  if (!lastImuReadOk) {
    lastImuClassified = false;
    hasOrientationCandidate = false;
    orientationCandidateSinceMs = 0;
    return;
  }
  lastImuAccel = accel;
  lastImuClassified = classifyLandscapeFromGravity(accel, &candidateLandscape);
  lastImuLandscape = candidateLandscape;
  if (!lastImuClassified) {
    hasOrientationCandidate = false;
    orientationCandidateSinceMs = 0;
    return;
  }

  if (candidateLandscape == settings.landscape) {
    hasOrientationCandidate = false;
    orientationCandidateSinceMs = 0;
    return;
  }

  if (!hasOrientationCandidate || orientationCandidateLandscape != candidateLandscape) {
    hasOrientationCandidate = true;
    orientationCandidateLandscape = candidateLandscape;
    orientationCandidateSinceMs = now;
    return;
  }

  if (now - orientationCandidateSinceMs < kAutoRotationDebounceMs) {
    return;
  }

  const bool previousLandscape = settings.landscape;
  settings.landscape = candidateLandscape;
  Serial.printf("Auto-rotation: accel=(%d,%d,%d) -> %s\n",
                accel.x,
                accel.y,
                accel.z,
                settings.landscape ? "landscape" : "portrait");
  ui.showStatus(settings.landscape ? "Auto landscape" : "Auto portrait");
  updateOrientationIfNeeded(previousLandscape);
  settingsStore.save(settings);
  hasOrientationCandidate = false;
  orientationCandidateSinceMs = 0;
}

void renderFrame() {
  if (frame.frameNumber != 0 && viewportPixels == nullptr) {
    return;
  }
  if (frame.frameNumber != 0) {
    thermal.renderRgb565(frame, ui.palette(), viewportPixels, viewportWidth, viewportHeight, &stats, ui.zoomed(), settings.landscape);
    const float offsetC = static_cast<float>(settings.temperatureOffsetTenths) / 10.0f;
    stats.minC += offsetC;
    stats.maxC += offsetC;
    stats.centerC += offsetC;
  }
  ui.render(display,
            frame,
            stats,
            viewportPixels,
            viewportWidth,
            viewportHeight,
            settings,
            storage.isMounted(),
            storage.lastCaptureBasePath());
  display.flush();
}

}  // namespace

namespace Application {

void setup() {
  Serial.begin(kSerialBaud);
  delay(1500);

  const esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("ESP32 reset reason: %s (%d)\n", resetReasonName(resetReason), static_cast<int>(resetReason));
  printPinMap();
  seedSystemTimeFromBuild();
  holdLeptonDeselected();
  settingsStore.begin();
  settings = settingsStore.load();

  initializeFlirCciI2c();
  ensureLeptonPoweredForStartup();
  rebootLeptonViaCci();
  waitForLeptonColdBoot();

  if (!display.begin(settings.landscape)) {
    Serial.println("Display initialization failed; firmware will continue with serial diagnostics only");
  }
  const DisplayInfo displayInfo = display.info();
  touch.begin(settings.landscape, displayInfo.width, displayInfo.height);
  imu.begin();
  storage.begin();
  lepton.begin();
  ui.begin(display);
  ui.render(display, frame, stats, nullptr, 0, 0, settings, storage.isMounted(), storage.lastCaptureBasePath());
  display.flush();
  allocateViewportBuffer();

  scanI2cBus();
  lastUserActivityMs = millis();
  lastGoodFrameMs = millis();

#if defined(FLIR_USE_SYNTHETIC_FRAMES)
  Serial.println("FLIR viewer firmware ready. Synthetic frames enabled by build flag.");
#else
  Serial.println("FLIR viewer firmware ready. Reading real Lepton VoSPI frames.");
#endif
}

void loop() {
  const uint32_t now = millis();
  const bool previousLandscape = settings.landscape;

  processSerialInput();

  TouchPoint touchPoint;
  const bool touchSampled = touch.read(touchPoint);
  const bool touchActive = touchSampled && touchPoint.pressed;
  const bool touchPressedEdge = touchActive && !touchWasActive;
  if (touchSampled) {
    touchWasActive = touchActive;
  }

  bool touchConsumedForWake = false;
  if (touchActive) {
    lastUserActivityMs = millis();
  }

  if (touchPressedEdge) {
    if (leptonLowPower) {
      Serial.println("Touch activity detected while Lepton is asleep; waking");
      if (displayOffForIdleSleep) {
        display.setBacklight(true);
        displayOffForIdleSleep = false;
      }
      ui.showStatus("Waking Lepton...");
      needsRender = true;
      wakeLeptonFromLowPower();
      ui.showStatus("Lepton awake");
      lastUserActivityMs = millis();
      touchConsumedForWake = true;
    }
  }

  const uint32_t idleTimeoutMs = inactivitySleepMs();
  if (!leptonLowPower && idleTimeoutMs != 0 && !ui.setupActive() &&
      millis() - lastUserActivityMs >= idleTimeoutMs) {
    Serial.printf("Touch inactivity timeout reached after %u seconds; sleeping Lepton\n",
                  settings.inactivitySleepSeconds);
    ui.showStatus("Idle timeout: sleeping");
    needsRender = true;
    renderFrame();
    requestLeptonLowPower();
    if (leptonLowPower) {
      display.setBacklight(false);
      displayOffForIdleSleep = true;
    }
  }

  const bool touchActionReady = touchActive && touchPressedEdge;
  if (touchActionReady && !touchConsumedForWake && !leptonLowPower) {
    const AppSettings previousSettings = settings;
    if (ui.handleTouch(touchPoint, settings, display, storage, frame, stats, viewportPixels, viewportWidth, viewportHeight)) {
      lastTouchActionMs = now;
      updateOrientationIfNeeded(previousLandscape);
      if (!settingsEqual(previousSettings, settings)) {
        settingsStore.save(settings);
      }
      lastUserActivityMs = millis();
      needsRender = true;
    }
  }

  if (!touchActive) {
    updateAutoOrientation();
  }

  if (!leptonLowPower && now - lastFrameMs >= kFrameIntervalMs) {
    lastFrameMs = now;
    if (lepton.readFrame(frame)) {
      lastGoodFrameMs = now;
      lastObservedRecoveryCount = lepton.recoveryCount();
      renderFrame();
      needsRender = false;
#if !defined(FLIR_USE_SYNTHETIC_FRAMES)
    } else if (lepton.recoveryCount() != lastObservedRecoveryCount) {
      lastObservedRecoveryCount = lepton.recoveryCount();
      if (now - lastGoodFrameMs >= 3000) {
        escalateLeptonVospiRecovery("no frames after repeated SPI recovery");
      }
#endif
    }
  }

  if (needsRender) {
    renderFrame();
    needsRender = false;
  }

  if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    printRuntimeStatus();
  }
}

}  // namespace Application
