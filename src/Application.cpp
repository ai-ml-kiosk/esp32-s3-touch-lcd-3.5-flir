#include "Application.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "pin_config.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint32_t kSpiFrequency = 20000000;
constexpr uint8_t kLeptonCciAddress = 0x2A;

void printPinMap() {
  Serial.println();
  Serial.println("Waveshare ESP32-S3-Touch-LCD-3.5B FLIR bring-up");
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
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
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

void initializeFlirPins() {
  pinMode(Pins::FLIR_SPI_CS, OUTPUT);
  digitalWrite(Pins::FLIR_SPI_CS, HIGH);

  if (Pins::FLIR_RESET >= 0) {
    pinMode(Pins::FLIR_RESET, OUTPUT);
    digitalWrite(Pins::FLIR_RESET, HIGH);
  }

  if (Pins::FLIR_POWER_ENABLE >= 0) {
    pinMode(Pins::FLIR_POWER_ENABLE, OUTPUT);
    digitalWrite(Pins::FLIR_POWER_ENABLE, HIGH);
  }

  Wire.begin(Pins::FLIR_CCI_SDA, Pins::FLIR_CCI_SCL, kI2cFrequency);
  SPI.begin(Pins::FLIR_SPI_SCLK, Pins::FLIR_SPI_MISO, Pins::FLIR_SPI_MOSI, Pins::FLIR_SPI_CS);
  SPI.beginTransaction(SPISettings(kSpiFrequency, MSBFIRST, SPI_MODE3));
  SPI.endTransaction();
}

}  // namespace

namespace Application {

void setup() {
  Serial.begin(kSerialBaud);
  delay(1500);

  printPinMap();
  initializeFlirPins();
  scanI2cBus();

  Serial.println("PlatformIO bring-up firmware ready.");
}

void loop() {
  static uint32_t lastStatusMs = 0;
  const uint32_t now = millis();
  if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    Serial.println("Waiting for FLIR firmware implementation...");
  }
}

}  // namespace Application
