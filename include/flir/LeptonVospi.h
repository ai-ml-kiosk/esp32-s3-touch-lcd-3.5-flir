#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "thermal/ThermalFrame.h"

enum class LeptonStatus {
  Ok,
  Timeout,
  SyncLost,
  SpiError,
};

class LeptonVospi {
 public:
  bool begin();
  bool readFrame(ThermalFrame& frame);
  LeptonStatus lastStatus() const { return lastStatus_; }
  uint32_t frameNumber() const { return frameNumber_; }
  uint32_t syncLossCount() const { return syncLossCount_; }
  uint32_t recoveryCount() const { return recoveryCount_; }

 private:
  static constexpr size_t kPacketSize = 164;
  static constexpr size_t kPayloadOffset = 4;
  static constexpr uint8_t kPacketCount = 60;
  static constexpr uint32_t kSpiFrequency = 8000000;
  static constexpr uint32_t kFrameTimeoutMs = 250;
  static constexpr uint32_t kResyncDelayMs = 185;
  static constexpr uint16_t kSignalRecoveryThreshold = 8;

  bool readPacket(uint8_t* packet);
  bool isDiscardPacket(const uint8_t* packet) const;
  bool isZeroPacket(const uint8_t* packet) const;
  bool isAllOnesPacket(const uint8_t* packet) const;
  uint8_t packetNumber(const uint8_t* packet) const;
  void decodePacketPayload(const uint8_t* packet, uint16_t packetIndex, ThermalFrame& frame);
  void printPacketDiagnostic(const uint8_t* packet, uint8_t expected, const char* reason);
  void resync();
  void recoverSpiBus(const char* reason);

  SPIClass spi_{HSPI};
  SPISettings spiSettings_{kSpiFrequency, MSBFIRST, SPI_MODE3};
  LeptonStatus lastStatus_ = LeptonStatus::Timeout;
  uint32_t frameNumber_ = 0;
  uint32_t syncLossCount_ = 0;
  uint32_t recoveryCount_ = 0;
  uint32_t diagnosticCount_ = 0;
  uint16_t invalidSignalCount_ = 0;
};
