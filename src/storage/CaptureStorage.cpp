#include "storage/CaptureStorage.h"

#include <FS.h>
#include <SD_MMC.h>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "board/BoardPins.h"

namespace {

constexpr uint16_t kCaptureFooterHeight = 20;
constexpr uint16_t kCaptureFooterHeightWithFilename = 28;
constexpr int kTfSdMmcFrequency = SDMMC_FREQ_DEFAULT;
constexpr uint8_t kTfMaxOpenFiles = 4;

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

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t rgb565From888(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

bool buildBasePath(const char* dir, const char* name, const char* extension, char* out, size_t outSize) {
  if (dir == nullptr || name == nullptr || extension == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  char filePath[112] = {};
  if (name[0] == '/') {
    snprintf(filePath, sizeof(filePath), "%s", name);
  } else {
    snprintf(filePath, sizeof(filePath), "%s/%s", dir, name);
  }
  const size_t pathLen = strlen(filePath);
  const size_t extLen = strlen(extension);
  if (pathLen <= extLen || strcmp(filePath + pathLen - extLen, extension) != 0) {
    return false;
  }
  filePath[pathLen - extLen] = '\0';
  strncpy(out, filePath, outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

bool sortedBaseAt(const char* dir, const char* extension, uint16_t index, char* out, size_t outSize) {
  if (dir == nullptr || dir[0] == '\0' || extension == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';

  char previous[112] = {};
  char selected[112] = {};
  bool hasPrevious = false;

  for (uint16_t selectedIndex = 0; selectedIndex <= index; ++selectedIndex) {
    File root = SD_MMC.open(dir);
    if (!root || !root.isDirectory()) {
      return false;
    }

    char best[112] = {};
    bool found = false;
    File file = root.openNextFile();
    while (file) {
      const char* name = file.name();
      const size_t len = strlen(name);
      const size_t extLen = strlen(extension);
      if (!file.isDirectory() && len > extLen && strcmp(name + len - extLen, extension) == 0) {
        char basePath[112] = {};
        if (buildBasePath(dir, name, extension, basePath, sizeof(basePath)) &&
            (!hasPrevious || strcmp(basePath, previous) > 0) &&
            (!found || strcmp(basePath, best) < 0)) {
          strncpy(best, basePath, sizeof(best) - 1);
          best[sizeof(best) - 1] = '\0';
          found = true;
        }
      }
      file.close();
      file = root.openNextFile();
      yield();
    }
    root.close();

    if (!found) {
      return false;
    }
    strncpy(selected, best, sizeof(selected) - 1);
    selected[sizeof(selected) - 1] = '\0';
    strncpy(previous, best, sizeof(previous) - 1);
    previous[sizeof(previous) - 1] = '\0';
    hasPrevious = true;
  }

  strncpy(out, selected, outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

const uint8_t* glyphRows(char c) {
  static const uint8_t space[7] = {0, 0, 0, 0, 0, 0, 0};
  static const uint8_t plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
  static const uint8_t minus[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
  static const uint8_t dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
  static const uint8_t slash[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
  static const uint8_t colon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
  static const uint8_t zero[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
  static const uint8_t one[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
  static const uint8_t two[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
  static const uint8_t three[7] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
  static const uint8_t four[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
  static const uint8_t five[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
  static const uint8_t six[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
  static const uint8_t seven[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
  static const uint8_t eight[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
  static const uint8_t nine[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
  static const uint8_t a[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static const uint8_t b[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
  static const uint8_t letterC[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
  static const uint8_t e[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
  static const uint8_t f[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
  static const uint8_t g[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
  static const uint8_t h[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  static const uint8_t i[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
  static const uint8_t l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
  static const uint8_t m[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
  static const uint8_t o[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
  static const uint8_t p[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
  static const uint8_t r[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
  static const uint8_t t[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
  static const uint8_t w[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
  static const uint8_t underscore[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};

  if (c >= 'a' && c <= 'z') {
    c -= 32;
  }

  switch (c) {
    case '+': return plus;
    case '-': return minus;
    case '.': return dot;
    case '/': return slash;
    case ':': return colon;
    case '_': return underscore;
    case '0': return zero;
    case '1': return one;
    case '2': return two;
    case '3': return three;
    case '4': return four;
    case '5': return five;
    case '6': return six;
    case '7': return seven;
    case '8': return eight;
    case '9': return nine;
    case 'A': return a;
    case 'B': return b;
    case 'C': return letterC;
    case 'E': return e;
    case 'F': return f;
    case 'G': return g;
    case 'H': return h;
    case 'I': return i;
    case 'L': return l;
    case 'M': return m;
    case 'O': return o;
    case 'P': return p;
    case 'R': return r;
    case 'T': return t;
    case 'W': return w;
    default: return space;
  }
}

void putPixel(uint8_t* row,
              uint32_t rowSize,
              uint16_t width,
              int16_t x,
              int16_t y,
              uint16_t targetY,
              uint8_t r,
              uint8_t g,
              uint8_t b) {
  if (x < 0 || y < 0 || x >= static_cast<int16_t>(width) || y != static_cast<int16_t>(targetY)) {
    return;
  }
  const uint32_t offset = static_cast<uint32_t>(x) * 3;
  if (offset + 2 >= rowSize) {
    return;
  }
  row[offset] = b;
  row[offset + 1] = g;
  row[offset + 2] = r;
}

void drawTextToRow(uint8_t* row,
                   uint32_t rowSize,
                   uint16_t width,
                   uint16_t targetY,
                   int16_t x,
                   int16_t y,
                   const char* text,
                   uint8_t r,
                   uint8_t g,
                   uint8_t b) {
  if (text == nullptr) {
    return;
  }
  int16_t cursorX = x;
  for (const char* p = text; *p != '\0'; ++p) {
    const uint8_t* rows = glyphRows(*p);
    for (uint8_t gy = 0; gy < 7; ++gy) {
      const uint8_t bits = rows[gy];
      for (uint8_t gx = 0; gx < 5; ++gx) {
        if ((bits & (1 << (4 - gx))) != 0) {
          putPixel(row, rowSize, width, cursorX + gx, y + gy, targetY, r, g, b);
        }
      }
    }
    cursorX += 6;
  }
}

void fillRectToRow(uint8_t* row,
                   uint32_t rowSize,
                   uint16_t width,
                   uint16_t targetY,
                   int16_t x,
                   int16_t y,
                   int16_t w,
                   int16_t h,
                   uint8_t r,
                   uint8_t g,
                   uint8_t b) {
  if (targetY < y || targetY >= y + h) {
    return;
  }
  for (int16_t px = x; px < x + w; ++px) {
    putPixel(row, rowSize, width, px, targetY, targetY, r, g, b);
  }
}

void drawRectToRow(uint8_t* row,
                   uint32_t rowSize,
                   uint16_t width,
                   uint16_t targetY,
                   int16_t x,
                   int16_t y,
                   int16_t w,
                   int16_t h,
                   uint8_t r,
                   uint8_t g,
                   uint8_t b) {
  if (targetY == y || targetY == y + h - 1) {
    fillRectToRow(row, rowSize, width, targetY, x, y, w, h, r, g, b);
    return;
  }
  if (targetY > y && targetY < y + h - 1) {
    putPixel(row, rowSize, width, x, targetY, targetY, r, g, b);
    putPixel(row, rowSize, width, x + w - 1, targetY, targetY, r, g, b);
  }
}

void drawTextToRowScaled(uint8_t* row,
                         uint32_t rowSize,
                         uint16_t width,
                         uint16_t targetY,
                         int16_t x,
                         int16_t y,
                         const char* text,
                         uint8_t scale,
                         uint8_t r,
                         uint8_t g,
                         uint8_t b) {
  if (text == nullptr || scale == 0) {
    return;
  }
  int16_t cursorX = x;
  for (const char* p = text; *p != '\0'; ++p) {
    const uint8_t* rows = glyphRows(*p);
    for (uint8_t gy = 0; gy < 7; ++gy) {
      const uint8_t bits = rows[gy];
      for (uint8_t gx = 0; gx < 5; ++gx) {
        if ((bits & (1 << (4 - gx))) == 0) {
          continue;
        }
        for (uint8_t sy = 0; sy < scale; ++sy) {
          for (uint8_t sx = 0; sx < scale; ++sx) {
            putPixel(row,
                     rowSize,
                     width,
                     cursorX + gx * scale + sx,
                     y + gy * scale + sy,
                     targetY,
                     r,
                     g,
                     b);
          }
        }
      }
    }
    cursorX += 6 * scale;
  }
}

const char* filenameFromPath(const char* path) {
  if (path == nullptr) {
    return "";
  }
  const char* slash = strrchr(path, '/');
  return slash == nullptr ? path : slash + 1;
}

bool formatTimestampStem(char* out, size_t outSize) {
  time_t now = time(nullptr);
  tm localTime = {};
  if (now <= 0 || localtime_r(&now, &localTime) == nullptr) {
    return false;
  }

  const int year = localTime.tm_year + 1900;
  if (year < 2024) {
    return false;
  }

  snprintf(out,
           outSize,
           "thermal_%04d%02d%02d%02d%02d%02d",
           year,
           localTime.tm_mon + 1,
           localTime.tm_mday,
           localTime.tm_hour,
           localTime.tm_min,
           localTime.tm_sec);
  return true;
}

bool captureBaseExists(const char* basePath) {
  char rawPath[112];
  char bmpPath[112];
  snprintf(rawPath, sizeof(rawPath), "%s.raw", basePath);
  snprintf(bmpPath, sizeof(bmpPath), "%s.bmp", basePath);
  return SD_MMC.exists(rawPath) || SD_MMC.exists(bmpPath);
}

}  // namespace

bool CaptureStorage::begin() {
  return mountCard();
}

bool CaptureStorage::recover() {
  Serial.println("TF card recovery: remounting SD_MMC");
  closeThermalClipPlayback();
  if (clipActive_) {
    finishThermalClip();
  }
  SD_MMC.end();
  delay(80);
  return mountCard();
}

bool CaptureStorage::mountCard() {
  if (!SD_MMC.setPins(BoardPins::TF_CLK, BoardPins::TF_CMD, BoardPins::TF_D0)) {
    Serial.println("TF card pin setup failed");
    mounted_ = false;
    return false;
  }

  mounted_ = SD_MMC.begin("/sdcard", true, false, kTfSdMmcFrequency, kTfMaxOpenFiles);
  if (!mounted_) {
    Serial.println("TF card mount failed; capture disabled");
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No TF card attached; capture disabled");
    mounted_ = false;
    return false;
  }

  Serial.printf("TF card ready: %llu MB, SD_MMC freq=%d\n",
                SD_MMC.cardSize() / (1024 * 1024),
                kTfSdMmcFrequency);
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
    Serial.printf("TF card save path mkdir failed: %s\n", path);
    return false;
  }

  char probePath[96];
  snprintf(probePath, sizeof(probePath), "%s/.write_test", path);
  File probe = SD_MMC.open(probePath, FILE_WRITE);
  if (!probe) {
    Serial.printf("TF card save path write probe open failed: %s\n", probePath);
    return false;
  }
  if (probe.print("ok") != 2) {
    Serial.printf("TF card save path write probe failed: %s\n", probePath);
    probe.close();
    return false;
  }
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
                                 const ThermalStats& stats,
                                 char* savedBasePath,
                                 size_t savedBasePathSize) {
  if (!mounted_ || raw14 == nullptr || rgb565 == nullptr) {
    return false;
  }
  if (!ensureSavePath(settings.savePath)) {
    Serial.println("Capture path validation failed; attempting TF card recovery");
    if (!recover() || !ensureSavePath(settings.savePath)) {
      Serial.println("Capture path validation failed after TF card recovery");
      return false;
    }
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

  const bool rawOk = settings.saveRawCapture ? writeRaw(rawPath, raw14, rawPixelCount) : true;
  if (!settings.saveRawCapture && SD_MMC.exists(rawPath)) {
    SD_MMC.remove(rawPath);
  }
  const bool bmpOk = writeBmp24(bmpPath, rgb565, width, height, stats, settings, basePath);
  if (settings.saveRawCapture) {
    Serial.printf("Capture saved: bmp=%s raw=%s status(bmp=%s raw=%s)\n",
                  bmpPath,
                  rawPath,
                  bmpOk ? "ok" : "fail",
                  rawOk ? "ok" : "fail");
  } else {
    Serial.printf("Capture saved: bmp=%s raw=off status(bmp=%s)\n",
                  bmpPath,
                  bmpOk ? "ok" : "fail");
  }
  const bool ok = rawOk && bmpOk;
  if (ok) {
    strncpy(lastBasePath_, basePath, sizeof(lastBasePath_) - 1);
    lastBasePath_[sizeof(lastBasePath_) - 1] = '\0';
  } else {
    Serial.printf("Capture failed: base=%s mounted=%s\n", basePath, mounted_ ? "yes" : "no");
  }
  return ok;
}

bool CaptureStorage::beginThermalClip(const AppSettings& settings, char* savedBasePath, size_t savedBasePathSize) {
  if (!mounted_) {
    return false;
  }
  if (clipActive_) {
    finishThermalClip();
  }
  if (!ensureSavePath(settings.savePath)) {
    Serial.println("Clip path validation failed; attempting TF card recovery");
    if (!recover() || !ensureSavePath(settings.savePath)) {
      Serial.println("Clip path validation failed after TF card recovery");
      return false;
    }
  }

  char basePath[96];
  nextBasePath(settings.savePath, basePath, sizeof(basePath));
  if (savedBasePath != nullptr && savedBasePathSize > 0) {
    strncpy(savedBasePath, basePath, savedBasePathSize - 1);
    savedBasePath[savedBasePathSize - 1] = '\0';
  }

  char clipPath[112];
  snprintf(clipPath, sizeof(clipPath), "%s.tclip", basePath);
  clipFile_ = SD_MMC.open(clipPath, FILE_WRITE);
  if (!clipFile_) {
    Serial.printf("Thermal clip open failed: %s\n", clipPath);
    recover();
    return false;
  }

  clipFile_.write(reinterpret_cast<const uint8_t*>("FLIRTCLP"), 8);
  writeU16(clipFile_, kLeptonWidth);
  writeU16(clipFile_, kLeptonHeight);
  writeU32(clipFile_, 0);
  writeU32(clipFile_, 116);
  clipFrameCount_ = 0;
  clipActive_ = true;
  strncpy(lastBasePath_, basePath, sizeof(lastBasePath_) - 1);
  lastBasePath_[sizeof(lastBasePath_) - 1] = '\0';
  Serial.printf("Thermal clip started: %s\n", clipPath);
  return true;
}

bool CaptureStorage::appendThermalClipFrame(const uint16_t* raw14, size_t rawPixelCount) {
  if (!clipActive_ || !clipFile_ || raw14 == nullptr || rawPixelCount != kLeptonPixelCount) {
    return false;
  }
  const size_t bytes = rawPixelCount * sizeof(uint16_t);
  const size_t written = clipFile_.write(reinterpret_cast<const uint8_t*>(raw14), bytes);
  if (written != bytes) {
    Serial.println("Thermal clip frame write failed");
    finishThermalClip();
    return false;
  }
  ++clipFrameCount_;
  return true;
}

bool CaptureStorage::finishThermalClip() {
  if (!clipActive_) {
    return false;
  }
  if (clipFile_) {
    clipFile_.seek(12);
    writeU32(clipFile_, clipFrameCount_);
    clipFile_.flush();
    clipFile_.close();
  }
  Serial.printf("Thermal clip finished: frames=%lu\n", static_cast<unsigned long>(clipFrameCount_));
  clipActive_ = false;
  clipFrameCount_ = 0;
  return true;
}

uint64_t CaptureStorage::totalBytes() const {
  return mounted_ ? SD_MMC.totalBytes() : 0;
}

uint64_t CaptureStorage::usedBytes() const {
  return mounted_ ? SD_MMC.usedBytes() : 0;
}

uint64_t CaptureStorage::freeBytes() const {
  const uint64_t total = totalBytes();
  const uint64_t used = usedBytes();
  return total > used ? total - used : 0;
}

bool CaptureStorage::deleteLastCapture() {
  if (lastBasePath_[0] == '\0') {
    return false;
  }
  return deleteCapture(lastBasePath_);
}

uint16_t CaptureStorage::captureCount(const char* dir) const {
  if (!mounted_ || dir == nullptr || dir[0] == '\0') {
    return 0;
  }

  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) {
    return 0;
  }

  uint16_t count = 0;
  File file = root.openNextFile();
  while (file) {
    const char* name = file.name();
    const size_t len = strlen(name);
    if (!file.isDirectory() && len > 4 && strcmp(name + len - 4, ".bmp") == 0) {
      ++count;
    }
    file.close();
    file = root.openNextFile();
    yield();
  }
  root.close();
  return count;
}

bool CaptureStorage::captureBaseAt(const char* dir, uint16_t index, char* out, size_t outSize) const {
  if (!mounted_ || dir == nullptr || dir[0] == '\0' || out == nullptr || outSize == 0) {
    return false;
  }
  return sortedBaseAt(dir, ".bmp", index, out, outSize);
}

bool CaptureStorage::latestCaptureBase(const char* dir, char* out, size_t outSize) const {
  if (!mounted_ || dir == nullptr || dir[0] == '\0' || out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';

  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) {
    return false;
  }

  bool found = false;
  char latestPath[112] = {};
  File file = root.openNextFile();
  while (file) {
    const char* name = file.name();
    const size_t len = strlen(name);
    if (!file.isDirectory() && len > 4 && strcmp(name + len - 4, ".bmp") == 0) {
      char filePath[112] = {};
      if (name[0] == '/') {
        snprintf(filePath, sizeof(filePath), "%s", name);
      } else {
        snprintf(filePath, sizeof(filePath), "%s/%s", dir, name);
      }
      const size_t pathLen = strlen(filePath);
      if (pathLen > 4) {
        filePath[pathLen - 4] = '\0';
      }
      if (!found || strcmp(filePath, latestPath) > 0) {
        strncpy(latestPath, filePath, sizeof(latestPath) - 1);
        latestPath[sizeof(latestPath) - 1] = '\0';
        found = true;
      }
    }
    file.close();
    file = root.openNextFile();
    yield();
  }
  root.close();

  if (!found) {
    return false;
  }
  strncpy(out, latestPath, outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

uint16_t CaptureStorage::clipCount(const char* dir) const {
  if (!mounted_ || dir == nullptr || dir[0] == '\0') {
    return 0;
  }
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) {
    return 0;
  }
  uint16_t count = 0;
  File file = root.openNextFile();
  while (file) {
    const char* name = file.name();
    const size_t len = strlen(name);
    if (!file.isDirectory() && len > 6 && strcmp(name + len - 6, ".tclip") == 0) {
      ++count;
    }
    file.close();
    file = root.openNextFile();
    yield();
  }
  root.close();
  return count;
}

bool CaptureStorage::clipBaseAt(const char* dir, uint16_t index, char* out, size_t outSize) const {
  if (!mounted_ || dir == nullptr || dir[0] == '\0' || out == nullptr || outSize == 0) {
    return false;
  }
  return sortedBaseAt(dir, ".tclip", index, out, outSize);
}

bool CaptureStorage::latestClipBase(const char* dir, char* out, size_t outSize) const {
  const uint16_t count = clipCount(dir);
  return count > 0 && clipBaseAt(dir, count - 1, out, outSize);
}

bool CaptureStorage::deleteCapture(const char* basePath) {
  if (!mounted_ || basePath == nullptr || basePath[0] == '\0') {
    return false;
  }

  char rawPath[112];
  char bmpPath[112];
  snprintf(rawPath, sizeof(rawPath), "%s.raw", basePath);
  snprintf(bmpPath, sizeof(bmpPath), "%s.bmp", basePath);

  const bool bmpOk = !SD_MMC.exists(bmpPath) || SD_MMC.remove(bmpPath);
  const bool rawOk = !SD_MMC.exists(rawPath) || SD_MMC.remove(rawPath);
  Serial.printf("Delete capture %s: raw=%s bmp=%s\n",
                basePath,
                rawOk ? "ok" : "fail",
                bmpOk ? "ok" : "fail");
  if (bmpOk && rawOk) {
    if (strcmp(lastBasePath_, basePath) == 0) {
      lastBasePath_[0] = '\0';
    }
    return true;
  }
  return false;
}

bool CaptureStorage::deleteThermalClip(const char* basePath) {
  if (!mounted_ || basePath == nullptr || basePath[0] == '\0') {
    return false;
  }
  char clipPath[112];
  snprintf(clipPath, sizeof(clipPath), "%s.tclip", basePath);
  const bool ok = !SD_MMC.exists(clipPath) || SD_MMC.remove(clipPath);
  Serial.printf("Delete thermal clip %s: %s\n", basePath, ok ? "ok" : "fail");
  return ok;
}

bool CaptureStorage::loadThermalClipFrame(const char* basePath,
                                          uint32_t frameIndex,
                                          ThermalFrame& frame,
                                          uint32_t* frameCount) const {
  if (!mounted_ || basePath == nullptr || basePath[0] == '\0') {
    return false;
  }
  char clipPath[112];
  snprintf(clipPath, sizeof(clipPath), "%s.tclip", basePath);
  File file = SD_MMC.open(clipPath, FILE_READ);
  if (!file) {
    return false;
  }
  uint8_t header[20] = {};
  if (file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "FLIRTCLP", 8) != 0) {
    file.close();
    return false;
  }
  const uint16_t width = readLe16(header + 8);
  const uint16_t height = readLe16(header + 10);
  const uint32_t count = readLe32(header + 12);
  if (frameCount != nullptr) {
    *frameCount = count;
  }
  if (width != kLeptonWidth || height != kLeptonHeight || count == 0) {
    file.close();
    return false;
  }
  if (frameIndex >= count) {
    frameIndex = count - 1;
  }
  const uint32_t offset = sizeof(header) + frameIndex * kLeptonPixelCount * sizeof(uint16_t);
  if (!file.seek(offset)) {
    file.close();
    return false;
  }
  const size_t bytes = kLeptonPixelCount * sizeof(uint16_t);
  const bool ok = file.read(reinterpret_cast<uint8_t*>(frame.raw), bytes) == bytes;
  file.close();
  if (ok) {
    frame.width = kLeptonWidth;
    frame.height = kLeptonHeight;
    frame.frameNumber = frameIndex + 1;
  }
  return ok;
}

bool CaptureStorage::openThermalClipPlayback(const char* basePath, uint32_t* frameCount) {
  if (!mounted_ || basePath == nullptr || basePath[0] == '\0') {
    return false;
  }

  if (playbackClipFile_ && strcmp(playbackClipBasePath_, basePath) == 0) {
    if (frameCount != nullptr) {
      *frameCount = playbackClipFrameCount_;
    }
    return playbackClipFrameCount_ > 0;
  }

  closeThermalClipPlayback();

  char clipPath[112];
  snprintf(clipPath, sizeof(clipPath), "%s.tclip", basePath);
  playbackClipFile_ = SD_MMC.open(clipPath, FILE_READ);
  if (!playbackClipFile_) {
    return false;
  }

  uint8_t header[20] = {};
  if (playbackClipFile_.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "FLIRTCLP", 8) != 0) {
    closeThermalClipPlayback();
    return false;
  }

  const uint16_t width = readLe16(header + 8);
  const uint16_t height = readLe16(header + 10);
  const uint32_t count = readLe32(header + 12);
  if (width != kLeptonWidth || height != kLeptonHeight || count == 0) {
    closeThermalClipPlayback();
    return false;
  }

  strncpy(playbackClipBasePath_, basePath, sizeof(playbackClipBasePath_) - 1);
  playbackClipBasePath_[sizeof(playbackClipBasePath_) - 1] = '\0';
  playbackClipFrameCount_ = count;
  playbackClipNextFrame_ = 0;
  if (frameCount != nullptr) {
    *frameCount = playbackClipFrameCount_;
  }
  return true;
}

bool CaptureStorage::readNextThermalClipPlaybackFrame(ThermalFrame& frame,
                                                      uint32_t* frameIndex,
                                                      uint32_t* frameCount) {
  if (!playbackClipFile_ || playbackClipFrameCount_ == 0) {
    return false;
  }

  if (playbackClipNextFrame_ >= playbackClipFrameCount_) {
    playbackClipNextFrame_ = 0;
  }

  const uint32_t currentFrame = playbackClipNextFrame_;
  const uint32_t offset = 20 + currentFrame * kLeptonPixelCount * sizeof(uint16_t);
  if (!playbackClipFile_.seek(offset)) {
    return false;
  }

  const size_t bytes = kLeptonPixelCount * sizeof(uint16_t);
  if (playbackClipFile_.read(reinterpret_cast<uint8_t*>(frame.raw), bytes) != bytes) {
    return false;
  }

  frame.width = kLeptonWidth;
  frame.height = kLeptonHeight;
  frame.frameNumber = currentFrame + 1;
  playbackClipNextFrame_ = currentFrame + 1;
  if (frameIndex != nullptr) {
    *frameIndex = currentFrame;
  }
  if (frameCount != nullptr) {
    *frameCount = playbackClipFrameCount_;
  }
  return true;
}

void CaptureStorage::closeThermalClipPlayback() {
  if (playbackClipFile_) {
    playbackClipFile_.close();
  }
  playbackClipFrameCount_ = 0;
  playbackClipNextFrame_ = 0;
  playbackClipBasePath_[0] = '\0';
}

bool CaptureStorage::loadCaptureBmp(const char* basePath,
                                    uint16_t* out,
                                    uint16_t outWidth,
                                    uint16_t outHeight) const {
  if (!mounted_ || basePath == nullptr || basePath[0] == '\0' ||
      out == nullptr || outWidth == 0 || outHeight == 0) {
    return false;
  }

  char bmpPath[112];
  snprintf(bmpPath, sizeof(bmpPath), "%s.bmp", basePath);
  File file = SD_MMC.open(bmpPath, FILE_READ);
  if (!file) {
    return false;
  }

  uint8_t header[54] = {};
  if (file.read(header, sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }
  if (header[0] != 'B' || header[1] != 'M') {
    file.close();
    return false;
  }

  const uint32_t pixelOffset = readLe32(&header[10]);
  const uint32_t dibSize = readLe32(&header[14]);
  const uint32_t bmpWidth = readLe32(&header[18]);
  const uint32_t bmpHeight = readLe32(&header[22]);
  const uint16_t planes = readLe16(&header[26]);
  const uint16_t bpp = readLe16(&header[28]);
  const uint32_t compression = readLe32(&header[30]);
  if (dibSize < 40 || planes != 1 || bpp != 24 || compression != 0 ||
      bmpWidth != outWidth || bmpHeight < outHeight) {
    file.close();
    return false;
  }

  const uint32_t rowSize = ((bmpWidth * 3 + 3) / 4) * 4;
  if (rowSize > 1536 || !file.seek(pixelOffset)) {
    file.close();
    return false;
  }

  uint8_t row[1536] = {};
  for (uint32_t rowIndex = 0; rowIndex < bmpHeight; ++rowIndex) {
    if (file.read(row, rowSize) != rowSize) {
      file.close();
      return false;
    }
    const int32_t imageY = static_cast<int32_t>(bmpHeight - 1 - rowIndex);
    if (imageY < 0 || imageY >= outHeight) {
      continue;
    }
    for (uint16_t x = 0; x < outWidth; ++x) {
      const uint32_t offset = static_cast<uint32_t>(x) * 3;
      out[static_cast<uint32_t>(imageY) * outWidth + x] = rgb565From888(row[offset + 2], row[offset + 1], row[offset]);
    }
    if ((rowIndex & 0x0F) == 0) {
      yield();
    }
  }

  file.close();
  return true;
}

bool CaptureStorage::loadCaptureBmpScaled(const char* basePath,
                                          uint16_t* out,
                                          uint16_t outWidth,
                                          uint16_t outHeight) const {
  if (!mounted_ || basePath == nullptr || basePath[0] == '\0' ||
      out == nullptr || outWidth == 0 || outHeight == 0) {
    return false;
  }

  char bmpPath[112];
  snprintf(bmpPath, sizeof(bmpPath), "%s.bmp", basePath);
  File file = SD_MMC.open(bmpPath, FILE_READ);
  if (!file) {
    return false;
  }

  uint8_t header[54] = {};
  if (file.read(header, sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }
  if (header[0] != 'B' || header[1] != 'M') {
    file.close();
    return false;
  }

  const uint32_t pixelOffset = readLe32(&header[10]);
  const uint32_t dibSize = readLe32(&header[14]);
  const uint32_t bmpWidth = readLe32(&header[18]);
  const uint32_t bmpHeight = readLe32(&header[22]);
  const uint16_t planes = readLe16(&header[26]);
  const uint16_t bpp = readLe16(&header[28]);
  const uint32_t compression = readLe32(&header[30]);
  if (dibSize < 40 || planes != 1 || bpp != 24 || compression != 0 ||
      bmpWidth == 0 || bmpHeight == 0) {
    file.close();
    return false;
  }

  const uint32_t rowSize = ((bmpWidth * 3 + 3) / 4) * 4;
  if (rowSize > 1536) {
    file.close();
    return false;
  }

  if (!file.seek(pixelOffset)) {
    file.close();
    return false;
  }

  const uint32_t sourceImageHeight = bmpHeight;
  uint8_t row[1536] = {};
  int32_t targetY = static_cast<int32_t>(outHeight) - 1;
  for (uint32_t fileRow = 0; fileRow < bmpHeight && targetY >= 0; ++fileRow) {
    if (file.read(row, rowSize) != rowSize) {
      file.close();
      return false;
    }
    const uint32_t sourceY = bmpHeight - 1 - fileRow;
    while (targetY >= 0 &&
           static_cast<uint32_t>(targetY) * sourceImageHeight / outHeight == sourceY) {
      for (uint16_t x = 0; x < outWidth; ++x) {
        const uint32_t sourceX = static_cast<uint32_t>(x) * bmpWidth / outWidth;
        const uint32_t offset = sourceX * 3;
        out[static_cast<uint32_t>(targetY) * outWidth + x] =
            rgb565From888(row[offset + 2], row[offset + 1], row[offset]);
      }
      --targetY;
    }
    if ((fileRow & 0x0F) == 0) {
      yield();
    }
  }

  file.close();
  return true;
}

bool CaptureStorage::writeRaw(const char* path, const uint16_t* raw14, size_t pixelCount) {
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Raw capture open failed: %s\n", path);
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(raw14), pixelCount * sizeof(uint16_t));
  yield();
  file.flush();
  file.close();
  const bool ok = written == pixelCount * sizeof(uint16_t);
  if (!ok) {
    Serial.printf("Raw capture write short: %s written=%u expected=%u\n",
                  path,
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(pixelCount * sizeof(uint16_t)));
  }
  return ok;
}

bool CaptureStorage::writeBmp24(const char* path,
                                const uint16_t* rgb565,
                                uint16_t width,
                                uint16_t height,
                                const ThermalStats& stats,
                                const AppSettings& settings,
                                const char* basePath) {
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("BMP capture open failed: %s\n", path);
    return false;
  }

  const bool includeTemperatureFooter = settings.showHotColdDetails || settings.showCenterTemperature;
  const uint16_t footerHeight = settings.includeFilenameInCapture
                                    ? (includeTemperatureFooter ? kCaptureFooterHeightWithFilename : 14)
                                    : (includeTemperatureFooter ? kCaptureFooterHeight : 0);
  const uint16_t outputHeight = height + footerHeight;
  const uint32_t rowSize = ((static_cast<uint32_t>(width) * 3 + 3) / 4) * 4;
  const uint32_t pixelDataSize = rowSize * outputHeight;
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
  writeU32(file, outputHeight);
  writeU16(file, 1);
  writeU16(file, 24);
  writeU32(file, 0);
  writeU32(file, pixelDataSize);
  writeU32(file, 2835);
  writeU32(file, 2835);
  writeU32(file, 0);
  writeU32(file, 0);

  uint8_t row[1536] = {};
  char tempLine[80] = {};
  if (settings.showHotColdDetails && settings.showCenterTemperature) {
    snprintf(tempLine,
             sizeof(tempLine),
             "HIGH %.1fC  LOW %.1fC  CTR %.1fC",
             stats.maxC,
             stats.minC,
             stats.centerC);
  } else if (settings.showHotColdDetails) {
    snprintf(tempLine,
             sizeof(tempLine),
             "HIGH %.1fC  LOW %.1fC",
             stats.maxC,
             stats.minC);
  } else if (settings.showCenterTemperature) {
    snprintf(tempLine,
             sizeof(tempLine),
             "CTR %.1fC",
             stats.centerC);
  }
  char hotLine[24] = {};
  char coldLine[24] = {};
  snprintf(hotLine, sizeof(hotLine), "H %.1fC", stats.maxC);
  snprintf(coldLine, sizeof(coldLine), "L %.1fC", stats.minC);
  char fileLine[80] = {};
  snprintf(fileLine, sizeof(fileLine), "FILE %s.bmp", filenameFromPath(basePath));
  const uint16_t markerW = stats.markerWidth > 0 ? stats.markerWidth : width;
  const uint16_t markerH = stats.markerHeight > 0 ? stats.markerHeight : height;
  const uint16_t hotX = static_cast<uint32_t>(stats.hotX) * width / markerW;
  const uint16_t hotY = static_cast<uint32_t>(stats.hotY) * height / markerH;
  const uint16_t coldX = static_cast<uint32_t>(stats.coldX) * width / markerW;
  const uint16_t coldY = static_cast<uint32_t>(stats.coldY) * height / markerH;

  for (int32_t y = outputHeight - 1; y >= 0; --y) {
    uint32_t offset = 0;
    memset(row, 0, sizeof(row));
    if (y < height) {
      for (uint16_t x = 0; x < width; ++x) {
        const uint16_t color = rgb565[y * width + x];
        row[offset++] = blueFrom565(color);
        row[offset++] = greenFrom565(color);
        row[offset++] = redFrom565(color);
      }
      if (settings.showHotColdDetails) {
        drawRectToRow(row, rowSize, width, y, static_cast<int16_t>(hotX) - 7, static_cast<int16_t>(hotY) - 7, 15, 15, 255, 255, 0);
        drawRectToRow(row, rowSize, width, y, static_cast<int16_t>(coldX) - 7, static_cast<int16_t>(coldY) - 7, 15, 15, 96, 220, 255);
        const int16_t hotLabelW = static_cast<int16_t>(strlen(hotLine) * 12 + 6);
        int16_t hotLabelX = static_cast<int16_t>(hotX) + 10;
        if (hotLabelX + hotLabelW >= static_cast<int16_t>(width)) {
          hotLabelX = static_cast<int16_t>(hotX) - hotLabelW - 10;
        }
        if (hotLabelX < 0) {
          hotLabelX = 0;
        }
        const int16_t hotLabelY = hotY > 24 ? static_cast<int16_t>(hotY) - 24 : static_cast<int16_t>(hotY) + 12;
        fillRectToRow(row, rowSize, width, y, hotLabelX - 2, hotLabelY - 2, hotLabelW, 18, 0, 0, 0);
        drawTextToRowScaled(row, rowSize, width, y, hotLabelX + 1, hotLabelY + 1, hotLine, 2, 255, 255, 0);

        const int16_t coldLabelW = static_cast<int16_t>(strlen(coldLine) * 12 + 6);
        int16_t coldLabelX = static_cast<int16_t>(coldX) + 10;
        if (coldLabelX + coldLabelW >= static_cast<int16_t>(width)) {
          coldLabelX = static_cast<int16_t>(coldX) - coldLabelW - 10;
        }
        if (coldLabelX < 0) {
          coldLabelX = 0;
        }
        const int16_t coldLabelY = coldY > 24 ? static_cast<int16_t>(coldY) - 24 : static_cast<int16_t>(coldY) + 12;
        fillRectToRow(row, rowSize, width, y, coldLabelX - 2, coldLabelY - 2, coldLabelW, 18, 0, 0, 0);
        drawTextToRowScaled(row, rowSize, width, y, coldLabelX + 1, coldLabelY + 1, coldLine, 2, 96, 220, 255);
      }
      if (settings.showCenterTemperature) {
        const int16_t centerX = static_cast<int16_t>(width / 2);
        const int16_t centerY = static_cast<int16_t>(height / 2);
        fillRectToRow(row, rowSize, width, y, centerX - 10, centerY, 21, 2, 255, 255, 255);
        fillRectToRow(row, rowSize, width, y, centerX, centerY - 10, 2, 21, 255, 255, 255);

        char centerLine[24] = {};
        snprintf(centerLine, sizeof(centerLine), "C %.1fC", stats.centerC);
        const int16_t centerLabelW = static_cast<int16_t>(strlen(centerLine) * 12 + 6);
        int16_t centerLabelX = centerX + 12;
        if (centerLabelX + centerLabelW >= static_cast<int16_t>(width)) {
          centerLabelX = centerX - centerLabelW - 12;
        }
        if (centerLabelX < 0) {
          centerLabelX = 0;
        }
        const int16_t centerLabelY = centerY > 24 ? centerY - 24 : centerY + 12;
        fillRectToRow(row, rowSize, width, y, centerLabelX - 2, centerLabelY - 2, centerLabelW, 18, 0, 0, 0);
        drawTextToRowScaled(row, rowSize, width, y, centerLabelX + 1, centerLabelY + 1, centerLine, 2, 255, 255, 255);
      }
    } else {
      const uint16_t footerY = static_cast<uint16_t>(y - height);
      if (includeTemperatureFooter) {
        drawTextToRow(row,
                      rowSize,
                      width,
                      footerY,
                      6,
                      settings.includeFilenameInCapture ? 4 : 7,
                      tempLine,
                      255,
                      255,
                      255);
      }
      if (settings.includeFilenameInCapture) {
        drawTextToRow(row,
                      rowSize,
                      width,
                      footerY,
                      6,
                      includeTemperatureFooter ? 16 : 3,
                      fileLine,
                      180,
                      200,
                      220);
      }
      offset = static_cast<uint32_t>(width) * 3;
    }
    while (offset < rowSize && offset < sizeof(row)) {
      row[offset++] = 0;
    }
    if (file.write(row, rowSize) != rowSize) {
      Serial.printf("BMP capture row write failed: %s y=%ld\n", path, static_cast<long>(y));
      file.close();
      return false;
    }
    if ((y & 0x03) == 0) {
      yield();
    }
  }

  file.flush();
  file.close();
  return true;
}

void CaptureStorage::nextBasePath(const char* dir, char* out, size_t outSize) {
  char stem[40] = {};
  if (!formatTimestampStem(stem, sizeof(stem))) {
    snprintf(stem, sizeof(stem), "thermal_%05lu", static_cast<unsigned long>(sequence_++));
  }

  for (uint8_t suffix = 0; suffix < 100; ++suffix) {
    if (suffix == 0) {
      snprintf(out, outSize, "%s/%s", dir, stem);
    } else {
      snprintf(out, outSize, "%s/%s_%02u", dir, stem, suffix + 1);
    }
    if (!captureBaseExists(out)) {
      return;
    }
  }

  snprintf(out, outSize, "%s/thermal_%05lu", dir, static_cast<unsigned long>(sequence_++));
}
