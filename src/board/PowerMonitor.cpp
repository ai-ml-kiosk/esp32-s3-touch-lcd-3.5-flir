#include "board/PowerMonitor.h"

namespace {

constexpr uint8_t kAxp2101Address = 0x34;
constexpr uint8_t kAxp2101RegStatus1 = 0x00;
constexpr uint8_t kAxp2101RegStatus2 = 0x01;
constexpr uint8_t kAxp2101RegChipId = 0x03;
constexpr uint8_t kAxp2101RegBatteryPercent = 0xA4;
constexpr uint8_t kAxp2101ChipId = 0x4A;

}  // namespace

bool PowerMonitor::begin(TwoWire& wire) {
  wire_ = &wire;
  return update();
}

bool PowerMonitor::update() {
  if (wire_ == nullptr) {
    status_ = BatteryStatus{};
    return false;
  }

  BatteryStatus next{};
  uint8_t chipId = 0;
  if (!readRegister(kAxp2101RegChipId, &chipId) || chipId != kAxp2101ChipId) {
    status_ = next;
    status_.updatedMs = millis();
    return false;
  }

  uint8_t status1 = 0;
  uint8_t status2 = 0;
  uint8_t percent = 0;
  next.pmicPresent = true;
  if (readRegister(kAxp2101RegStatus1, &status1)) {
    next.externalPowerGood = (status1 & 0x20) != 0;
    next.batfetOpen = (status1 & 0x10) != 0;
    next.batteryPresent = (status1 & 0x08) != 0;
    next.thermalRegulation = (status1 & 0x02) != 0;
    next.currentLimit = (status1 & 0x01) != 0;
  }
  if (readRegister(kAxp2101RegStatus2, &status2)) {
    switch (status2 & 0x07) {
      case 0:
        next.chargeState = BatteryChargeState::Trickle;
        break;
      case 1:
        next.chargeState = BatteryChargeState::Precharge;
        break;
      case 2:
        next.chargeState = BatteryChargeState::ConstantCurrent;
        break;
      case 3:
        next.chargeState = BatteryChargeState::ConstantVoltage;
        break;
      case 4:
        next.chargeState = BatteryChargeState::Done;
        break;
      case 5:
        next.chargeState = BatteryChargeState::NotCharging;
        break;
      default:
        next.chargeState = BatteryChargeState::Unknown;
        break;
    }
  }
  if (next.batteryPresent && readRegister(kAxp2101RegBatteryPercent, &percent)) {
    next.percentage = percent <= 100 ? static_cast<int8_t>(percent) : -1;
  }
  if (next.externalPowerGood) {
    next.source = PowerSource::External;
  } else if (next.batteryPresent) {
    next.source = PowerSource::Battery;
  } else {
    next.source = PowerSource::Unknown;
  }
  next.updatedMs = millis();
  status_ = next;
  return true;
}

bool PowerMonitor::readRegister(uint8_t reg, uint8_t* value) {
  if (wire_ == nullptr || value == nullptr) {
    return false;
  }
  wire_->beginTransmission(kAxp2101Address);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {
    return false;
  }
  if (wire_->requestFrom(kAxp2101Address, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  *value = wire_->read();
  return true;
}
