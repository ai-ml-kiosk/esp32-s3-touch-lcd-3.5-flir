#pragma once

#include <Arduino.h>

struct ImuAccelRaw {
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
};

class ImuDriver {
 public:
  bool begin();
  bool readAccel(ImuAccelRaw& accel);
  bool ready() const { return ready_; }
  uint8_t address() const { return address_; }

 private:
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegisters(uint8_t reg, uint8_t* data, size_t length);
  bool probe(uint8_t address);

  bool ready_ = false;
  uint8_t address_ = 0;
};
