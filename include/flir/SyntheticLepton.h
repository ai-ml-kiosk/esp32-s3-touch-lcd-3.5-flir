#pragma once

#include "thermal/ThermalFrame.h"

class SyntheticLepton {
 public:
  bool begin();
  bool readFrame(ThermalFrame& frame);

 private:
  uint32_t frameNumber_ = 0;
};
