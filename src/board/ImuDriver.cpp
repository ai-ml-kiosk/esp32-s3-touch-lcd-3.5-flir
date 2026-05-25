#include "board/ImuDriver.h"

#include <Wire.h>

#include "board/BoardPins.h"

namespace {

constexpr uint8_t kQmi8658AddressLow = 0x6A;
constexpr uint8_t kQmi8658AddressHigh = 0x6B;
constexpr uint8_t kQmi8658WhoAmI = 0x00;
constexpr uint8_t kQmi8658Ctrl1 = 0x02;
constexpr uint8_t kQmi8658Ctrl2 = 0x03;
constexpr uint8_t kQmi8658Ctrl3 = 0x04;
constexpr uint8_t kQmi8658Ctrl7 = 0x08;
constexpr uint8_t kQmi8658AccelXLow = 0x35;
constexpr uint8_t kQmi8658ExpectedWhoAmI = 0x05;

int16_t readI16(const uint8_t* data) {
  return static_cast<int16_t>((static_cast<uint16_t>(data[1]) << 8) | data[0]);
}

}  // namespace

bool ImuDriver::begin() {
  Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL, BoardPins::I2C_FREQUENCY);

  if (probe(kQmi8658AddressLow)) {
    address_ = kQmi8658AddressLow;
  } else if (probe(kQmi8658AddressHigh)) {
    address_ = kQmi8658AddressHigh;
  } else {
    ready_ = false;
    Serial.println("QMI8658 IMU not found on board I2C bus");
    return false;
  }

  uint8_t whoAmI = 0;
  if (!readRegisters(kQmi8658WhoAmI, &whoAmI, 1)) {
    ready_ = false;
    Serial.println("QMI8658 IMU found but WHO_AM_I read failed");
    return false;
  }

  if (whoAmI != kQmi8658ExpectedWhoAmI) {
    Serial.printf("QMI8658 IMU WHO_AM_I unexpected: 0x%02X\n", whoAmI);
  }

  // CTRL1 enables address auto-increment. CTRL2 selects accelerometer 4g/125Hz.
  // CTRL3 keeps gyro configured but disabled. CTRL7 enables accelerometer only.
  if (!writeRegister(kQmi8658Ctrl1, 0x40) ||
      !writeRegister(kQmi8658Ctrl2, 0x23) ||
      !writeRegister(kQmi8658Ctrl3, 0x03) ||
      !writeRegister(kQmi8658Ctrl7, 0x01)) {
    ready_ = false;
    Serial.println("QMI8658 IMU configuration failed");
    return false;
  }

  ready_ = true;
  Serial.printf("QMI8658 IMU ready at 0x%02X WHO_AM_I=0x%02X\n", address_, whoAmI);
  return true;
}

bool ImuDriver::readAccel(ImuAccelRaw& accel) {
  if (!ready_) {
    return false;
  }

  uint8_t data[6] = {};
  if (!readRegisters(kQmi8658AccelXLow, data, sizeof(data))) {
    return false;
  }

  accel.x = readI16(&data[0]);
  accel.y = readI16(&data[2]);
  accel.z = readI16(&data[4]);
  return true;
}

bool ImuDriver::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address_);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool ImuDriver::readRegisters(uint8_t reg, uint8_t* data, size_t length) {
  if (data == nullptr || length == 0) {
    return false;
  }

  Wire.beginTransmission(address_);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t requested = static_cast<uint8_t>(length);
  if (Wire.requestFrom(address_, requested) != requested) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool ImuDriver::probe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}
