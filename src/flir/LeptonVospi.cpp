#include "flir/LeptonVospi.h"

#include <cstring>

#include "pin_config.h"

bool LeptonVospi::begin() {
  spi_.end();
  delay(20);

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

  invalidSignalCount_ = 0;
  spi_.begin(Pins::FLIR_SPI_SCLK, Pins::FLIR_SPI_MISO, Pins::FLIR_SPI_MOSI, Pins::FLIR_SPI_CS);
  digitalWrite(Pins::FLIR_SPI_CS, HIGH);
  delay(250);
  resync();
  Serial.printf("Lepton VoSPI driver ready: SCLK=%d MISO=%d MOSI=%d CS=%d freq=%lu mode=3\n",
                Pins::FLIR_SPI_SCLK,
                Pins::FLIR_SPI_MISO,
                Pins::FLIR_SPI_MOSI,
                Pins::FLIR_SPI_CS,
                static_cast<unsigned long>(kSpiFrequency));
  return true;
}

bool LeptonVospi::readFrame(ThermalFrame& frame) {
  uint8_t packet[kPacketSize];
  uint8_t nextPacket = 0;
  const uint32_t startMs = millis();

  while (millis() - startMs < kFrameTimeoutMs) {
    if (!readPacket(packet)) {
      lastStatus_ = LeptonStatus::SpiError;
      return false;
    }

    if (isDiscardPacket(packet)) {
      continue;
    }

    if (isZeroPacket(packet) || isAllOnesPacket(packet)) {
      ++invalidSignalCount_;
      printPacketDiagnostic(packet, nextPacket, isZeroPacket(packet) ? "all-zero" : "all-ff");
      if (invalidSignalCount_ >= kSignalRecoveryThreshold) {
        lastStatus_ = LeptonStatus::SyncLost;
        recoverSpiBus(isZeroPacket(packet) ? "all-zero packet stream" : "all-ff packet stream");
        return false;
      }
      continue;
    }

    invalidSignalCount_ = 0;
    const uint8_t id = packetNumber(packet);
    if (nextPacket == 0) {
      if (id != 0) {
        continue;
      }
    } else if (id != nextPacket) {
      lastStatus_ = LeptonStatus::SyncLost;
      ++syncLossCount_;
      printPacketDiagnostic(packet, nextPacket, "sequence");
      resync();
      return false;
    }

    decodePacketPayload(packet, id, frame);
    ++nextPacket;

    if (nextPacket == kPacketCount) {
      ++frameNumber_;
      frame.frameNumber = frameNumber_;
      lastStatus_ = LeptonStatus::Ok;
      invalidSignalCount_ = 0;
      return true;
    }
  }

  lastStatus_ = LeptonStatus::Timeout;
  ++syncLossCount_;
  printPacketDiagnostic(packet, nextPacket, "timeout");
  resync();
  return false;
}

bool LeptonVospi::readPacket(uint8_t* packet) {
  if (packet == nullptr) {
    return false;
  }

  static uint8_t txZeros[kPacketSize] = {};
  memset(packet, 0, kPacketSize);
  spi_.beginTransaction(spiSettings_);
  digitalWrite(Pins::FLIR_SPI_CS, LOW);
  delayMicroseconds(2);
  spi_.transferBytes(txZeros, packet, kPacketSize);
  delayMicroseconds(2);
  digitalWrite(Pins::FLIR_SPI_CS, HIGH);
  spi_.endTransaction();
  return true;
}

bool LeptonVospi::isDiscardPacket(const uint8_t* packet) const {
  return (packet[0] & 0x0F) == 0x0F;
}

bool LeptonVospi::isZeroPacket(const uint8_t* packet) const {
  return packet[0] == 0x00 && packet[1] == 0x00 && packet[2] == 0x00 && packet[3] == 0x00;
}

bool LeptonVospi::isAllOnesPacket(const uint8_t* packet) const {
  return packet[0] == 0xFF && packet[1] == 0xFF && packet[2] == 0xFF && packet[3] == 0xFF;
}

uint8_t LeptonVospi::packetNumber(const uint8_t* packet) const {
  return packet[1];
}

void LeptonVospi::decodePacketPayload(const uint8_t* packet, uint16_t packetIndex, ThermalFrame& frame) {
  if (packetIndex >= kPacketCount) {
    return;
  }

  const uint16_t row = packetIndex;
  for (uint16_t x = 0; x < kLeptonWidth; ++x) {
    const size_t offset = kPayloadOffset + x * 2;
    frame.raw[row * kLeptonWidth + x] = (static_cast<uint16_t>(packet[offset]) << 8) | packet[offset + 1];
  }
}

void LeptonVospi::printPacketDiagnostic(const uint8_t* packet, uint8_t expected, const char* reason) {
  if ((diagnosticCount_++ % 32) != 0 || packet == nullptr) {
    return;
  }

  const uint8_t id = packetNumber(packet);
  const bool allZero = isZeroPacket(packet);
  const bool allOnes = isAllOnesPacket(packet);
  Serial.printf("VoSPI diag reason=%s expected=%u got=%u hdr=%02X %02X %02X %02X discard=%s zero=%s ff=%s sync_loss=%lu\n",
                reason,
                expected,
                id,
                packet[0],
                packet[1],
                packet[2],
                packet[3],
                isDiscardPacket(packet) ? "yes" : "no",
                allZero ? "yes" : "no",
                allOnes ? "yes" : "no",
                static_cast<unsigned long>(syncLossCount_));
}

void LeptonVospi::resync() {
  digitalWrite(Pins::FLIR_SPI_CS, HIGH);
  delay(kResyncDelayMs);
}

void LeptonVospi::recoverSpiBus(const char* reason) {
  ++syncLossCount_;
  ++recoveryCount_;
  Serial.printf("VoSPI recovery reason=%s recovery=%lu invalid_signal=%u\n",
                reason,
                static_cast<unsigned long>(recoveryCount_),
                invalidSignalCount_);

  digitalWrite(Pins::FLIR_SPI_CS, HIGH);
  spi_.end();

  pinMode(Pins::FLIR_SPI_CS, OUTPUT);
  digitalWrite(Pins::FLIR_SPI_CS, HIGH);
  pinMode(Pins::FLIR_SPI_SCLK, OUTPUT);
  digitalWrite(Pins::FLIR_SPI_SCLK, LOW);
  pinMode(Pins::FLIR_SPI_MOSI, OUTPUT);
  digitalWrite(Pins::FLIR_SPI_MOSI, LOW);

  delay(100);
  spi_.begin(Pins::FLIR_SPI_SCLK, Pins::FLIR_SPI_MISO, Pins::FLIR_SPI_MOSI, Pins::FLIR_SPI_CS);
  digitalWrite(Pins::FLIR_SPI_CS, HIGH);
  delay(500);
  resync();
  invalidSignalCount_ = 0;
}
