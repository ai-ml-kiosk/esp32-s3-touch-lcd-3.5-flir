#include "storage/CaptureStorage.h"

#include <FS.h>
#include <SD_MMC.h>
#include <cstring>

#include "board/BoardPins.h"

namespace {

bool hasTraversal(const char* path) {
  return strstr(path, "..") != nullptr;
}

uint8_t redFrom565(uint16_t color) {
  const uint8_t r = (color >> 11) & 0x1F;
  return (r << 3) | (r >> 2);
}

uint8_t greenFrom565(uint16_t color) {
  const uint8_t g = (color >> 5) & 0x3F;
  return (g << 2) | (g >> 4);
}

uint8_t blueFrom565(uint16_t color) {
  const uint8_t b = color & 0x1F;
  return (b << 3) | (b >> 2);
}

void writeU16(File& file, uint16_t value) {
  file.write(value & 0xFF);
  file.write((value >> 8) & 0xFF);
}

void writeU32(File& file, uint32_t value) {
  file.write(value & 0xFF);
  file.write((value >> 8) & 0xFF);
  file.write((value >> 16) & 0xFF);
  file.write((value >> 24) & 0xFF);
}

}  // namespace

bool CaptureStorage::begin() {
  if (!SD_MMC.setPins(BoardPins::TF_CLK, BoardPins::TF_CMD, BoardPins::TF_D0)) {
    Serial.println("TF card pin setup failed");
    mounted_ = false;
    return false;
  }

  mounted_ = SD_MMC.begin("/sdcard", true);
  if (!mounted_) {
    Serial.println("TF card mount failed; capture disabled");
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No TF card attached; capture disabled");
    mounted_ = false;
    return false;
  }

  Serial.printf("TF card ready: %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
  ensureSavePath("/flir");
  return true;
}

bool CaptureStorage::validateSavePath(const char* path) const {
  if (path == nullptr || path[0] != '/' || path[1] == '\0') {
    return false;
  }
  return !hasTraversal(path);
}

bool CaptureStorage::ensureSavePath(const char* path) {
  if (!mounted_ || !validateSavePath(path)) {
    return false;
  }

  if (!SD_MMC.exists(path) && !SD_MMC.mkdir(path)) {
    return false;
  }

  char probePath[96];
  snprintf(probePath, sizeof(probePath), "%s/.write_test", path);
  File probe = SD_MMC.open(probePath, FILE_WRITE);
  if (!probe) {
    return false;
  }
  probe.print("ok");
  probe.close();
  SD_MMC.remove(probePath);
  return true;
}

bool CaptureStorage::saveCapture(const AppSettings& settings,
                                 const uint16_t* raw14,
                                 size_t rawPixelCount,
                                 const uint16_t* rgb565,
                                 uint16_t width,
                                 uint16_t height,
                                 char* savedBasePath,
                                 size_t savedBasePathSize) {
  if (!mounted_ || raw14 == nullptr || rgb565 == nullptr) {
    return false;
  }
  if (!ensureSavePath(settings.savePath)) {
    Serial.println("Capture path validation failed");
    return false;
  }

  char basePath[96];
  nextBasePath(settings.savePath, basePath, sizeof(basePath));
  if (savedBasePath != nullptr && savedBasePathSize > 0) {
    strncpy(savedBasePath, basePath, savedBasePathSize - 1);
    savedBasePath[savedBasePathSize - 1] = '\0';
  }

  char rawPath[112];
  char bmpPath[112];
  snprintf(rawPath, sizeof(rawPath), "%s.raw", basePath);
  snprintf(bmpPath, sizeof(bmpPath), "%s.bmp", basePath);

  const bool rawOk = writeRaw(rawPath, raw14, rawPixelCount);
  const bool bmpOk = writeBmp24(bmpPath, rgb565, width, height);
  Serial.printf("Capture %s: raw=%s bmp=%s\n", basePath, rawOk ? "ok" : "fail", bmpOk ? "ok" : "fail");
  return rawOk && bmpOk;
}

bool CaptureStorage::writeRaw(const char* path, const uint16_t* raw14, size_t pixelCount) {
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(raw14), pixelCount * sizeof(uint16_t));
  file.close();
  return written == pixelCount * sizeof(uint16_t);
}

bool CaptureStorage::writeBmp24(const char* path, const uint16_t* rgb565, uint16_t width, uint16_t height) {
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }

  const uint32_t rowSize = ((static_cast<uint32_t>(width) * 3 + 3) / 4) * 4;
  const uint32_t pixelDataSize = rowSize * height;
  const uint32_t fileSize = 54 + pixelDataSize;
  if (rowSize > 1536) {
    file.close();
    return false;
  }

  file.write('B');
  file.write('M');
  writeU32(file, fileSize);
  writeU16(file, 0);
  writeU16(file, 0);
  writeU32(file, 54);
  writeU32(file, 40);
  writeU32(file, width);
  writeU32(file, height);
  writeU16(file, 1);
  writeU16(file, 24);
  writeU32(file, 0);
  writeU32(file, pixelDataSize);
  writeU32(file, 2835);
  writeU32(file, 2835);
  writeU32(file, 0);
  writeU32(file, 0);

  uint8_t row[1536] = {};
  for (int32_t y = height - 1; y >= 0; --y) {
    uint32_t offset = 0;
    for (uint16_t x = 0; x < width; ++x) {
      const uint16_t color = rgb565[y * width + x];
      row[offset++] = blueFrom565(color);
      row[offset++] = greenFrom565(color);
      row[offset++] = redFrom565(color);
    }
    while (offset < rowSize && offset < sizeof(row)) {
      row[offset++] = 0;
    }
    if (file.write(row, rowSize) != rowSize) {
      file.close();
      return false;
    }
  }

  file.close();
  return true;
}

void CaptureStorage::nextBasePath(const char* dir, char* out, size_t outSize) {
  snprintf(out, outSize, "%s/frame_%05lu", dir, static_cast<unsigned long>(sequence_++));
}
