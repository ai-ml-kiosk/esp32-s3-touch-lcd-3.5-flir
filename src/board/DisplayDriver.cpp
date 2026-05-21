#include "board/DisplayDriver.h"

#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include <Wire.h>

#include "board/BoardPins.h"

namespace {

TCA9554 tca(BoardPins::TCA9554_ADDRESS, &Wire);
Arduino_DataBus* bus = nullptr;
Arduino_GFX* panel = nullptr;
Arduino_GFX* gfx = nullptr;

uint16_t textWidth(const char* text, uint8_t size) {
  return strlen(text) * 6 * size;
}

void flushDisplay() {
  if (gfx != nullptr) {
    gfx->flush(true);
  }
}

}  // namespace

bool DisplayDriver::begin(bool landscape) {
  landscape_ = landscape;
  pinMode(BoardPins::LCD_BACKLIGHT, OUTPUT);
  digitalWrite(BoardPins::LCD_BACKLIGHT, HIGH);
  Serial.printf("LCD backlight GPIO%d set HIGH\n", BoardPins::LCD_BACKLIGHT);

  ready_ = initializePanel();
  if (!ready_) {
    return false;
  }

  applyOrientation();
  Serial.printf("LCD logical size %dx%d\n", gfx->width(), gfx->height());
  fillScreen(rgb565(0, 0, 0));
  flush();
  return true;
}

void DisplayDriver::setLandscape(bool landscape) {
  if (landscape_ == landscape && ready_) {
    return;
  }
  landscape_ = landscape;
  applyOrientation();
  fillScreen(rgb565(0, 0, 0));
}

DisplayInfo DisplayDriver::info() const {
  DisplayInfo displayInfo;
  displayInfo.width = ready_ ? gfx->width() : (landscape_ ? BoardPins::LCD_LANDSCAPE_WIDTH : BoardPins::LCD_PHYSICAL_WIDTH);
  displayInfo.height = ready_ ? gfx->height() : (landscape_ ? BoardPins::LCD_LANDSCAPE_HEIGHT : BoardPins::LCD_PHYSICAL_HEIGHT);
  displayInfo.pixelFormat = DisplayPixelFormat::Rgb565;
  return displayInfo;
}

void DisplayDriver::fillScreen(uint16_t color) {
  if (ready_) {
    gfx->fillScreen(color);
  }
}

void DisplayDriver::fillRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t color) {
  if (ready_) {
    gfx->fillRect(x, y, width, height, color);
  }
}

void DisplayDriver::drawRect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t color) {
  if (ready_) {
    gfx->drawRect(x, y, width, height, color);
  }
}

void DisplayDriver::fillRoundRect(int16_t x,
                                  int16_t y,
                                  uint16_t width,
                                  uint16_t height,
                                  uint16_t radius,
                                  uint16_t color) {
  if (ready_) {
    gfx->fillRoundRect(x, y, width, height, radius, color);
  }
}

void DisplayDriver::drawRoundRect(int16_t x,
                                  int16_t y,
                                  uint16_t width,
                                  uint16_t height,
                                  uint16_t radius,
                                  uint16_t color) {
  if (ready_) {
    gfx->drawRoundRect(x, y, width, height, radius, color);
  }
}

void DisplayDriver::drawText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size) {
  if (!ready_) {
    return;
  }
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(text);
}

void DisplayDriver::drawTextRight(int16_t right, int16_t y, const char* text, uint16_t color, uint8_t size) {
  const int16_t x = right - static_cast<int16_t>(textWidth(text, size));
  drawText(x, y, text, color, size);
}

bool DisplayDriver::drawBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* pixels) {
  if (!ready_ || pixels == nullptr) {
    return false;
  }
  gfx->draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(pixels), width, height);
  return true;
}

void DisplayDriver::flush() {
  flushDisplay();
}

uint16_t DisplayDriver::rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

bool DisplayDriver::initializePanel() {
  Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL, BoardPins::I2C_FREQUENCY);

  if (!tca.begin()) {
    Serial.println("TCA9554 not found; continuing with direct LCD init");
  } else {
    Serial.println("TCA9554 found; toggling LCD reset on EXIO1");
    tca.pinMode1(BoardPins::TCA9554_LCD_RESET_PIN, OUTPUT);
    tca.write1(BoardPins::TCA9554_LCD_RESET_PIN, HIGH);
    delay(10);
    tca.write1(BoardPins::TCA9554_LCD_RESET_PIN, LOW);
    delay(10);
    tca.write1(BoardPins::TCA9554_LCD_RESET_PIN, HIGH);
    delay(200);
  }

  if (bus == nullptr) {
    bus = new Arduino_ESP32QSPI(BoardPins::LCD_QSPI_CS,
                                BoardPins::LCD_QSPI_CLK,
                                BoardPins::LCD_QSPI_D0,
                                BoardPins::LCD_QSPI_D1,
                                BoardPins::LCD_QSPI_D2,
                                BoardPins::LCD_QSPI_D3);
  }

  if (panel == nullptr) {
    panel = new Arduino_AXS15231B(bus,
                                  -1 /* RST */,
                                  0 /* rotation */,
                                  false /* IPS */,
                                  BoardPins::LCD_PHYSICAL_WIDTH,
                                  BoardPins::LCD_PHYSICAL_HEIGHT);
  }

  if (gfx == nullptr) {
    gfx = new Arduino_Canvas(BoardPins::LCD_PHYSICAL_WIDTH,
                             BoardPins::LCD_PHYSICAL_HEIGHT,
                             panel,
                             0,
                             0,
                             landscape_ ? 1 : 0);
  }

  if (!gfx->begin()) {
    Serial.println("AXS15231B display init failed");
    return false;
  }

  Serial.println("AXS15231B QSPI display ready, RGB565 output");
  return true;
}

void DisplayDriver::applyOrientation() {
  if (ready_) {
    gfx->setRotation(landscape_ ? 1 : 0);
  }
}
