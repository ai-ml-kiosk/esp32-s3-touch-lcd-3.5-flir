#pragma once

#include <Arduino.h>
#include <Wire.h>

enum class PowerSource : uint8_t {
  Unknown,
  Battery,
  External,
};

enum class BatteryChargeState : uint8_t {
  Unknown,
  Trickle,
  Precharge,
  ConstantCurrent,
  ConstantVoltage,
  Done,
  NotCharging,
};

struct BatteryStatus {
  bool pmicPresent = false;
  bool batteryPresent = false;
  bool externalPowerGood = false;
  bool batfetOpen = false;
  bool thermalRegulation = false;
  bool currentLimit = false;
  int8_t percentage = -1;
  PowerSource source = PowerSource::Unknown;
  BatteryChargeState chargeState = BatteryChargeState::Unknown;
  uint32_t updatedMs = 0;
};

class PowerMonitor {
 public:
  bool begin(TwoWire& wire);
  bool update();
  const BatteryStatus& status() const { return status_; }

 private:
  bool readRegister(uint8_t reg, uint8_t* value);

  TwoWire* wire_ = nullptr;
  BatteryStatus status_{};
};
