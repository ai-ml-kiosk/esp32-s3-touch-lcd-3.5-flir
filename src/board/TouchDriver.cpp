#include "board/TouchDriver.h"

#include <Wire.h>

#include "board/BoardPins.h"
#include "esp_lcd_touch_axs15231b.h"

namespace {

uint8_t panelRotationFromAppRotation(uint8_t rotation) {
  return (rotation + 2) & 0x03;
}

}  // namespace

bool TouchDriver::begin(bool landscape, uint16_t width, uint16_t height) {
  Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL, BoardPins::I2C_FREQUENCY);
  setOrientation(landscape, width, height);
  ready_ = true;
  Serial.println("AXS15231B touch ready");
  return true;
}

void TouchDriver::setOrientation(bool landscape, uint16_t width, uint16_t height) {
  setRotation(landscape ? 1 : 0, width, height);
}

void TouchDriver::setRotation(uint8_t rotation, uint16_t width, uint16_t height) {
  (void)width;
  (void)height;
  rotation &= 0x03;
  const bool landscape = rotation == 1 || rotation == 3;
  width_ = landscape ? BoardPins::LCD_LANDSCAPE_WIDTH : BoardPins::LCD_PHYSICAL_WIDTH;
  height_ = landscape ? BoardPins::LCD_LANDSCAPE_HEIGHT : BoardPins::LCD_PHYSICAL_HEIGHT;
  bsp_touch_init(&Wire,
                 -1,
                 panelRotationFromAppRotation(rotation),
                 width_,
                 height_);
}

bool TouchDriver::read(TouchPoint& point) {
  point.pressed = false;
  point.touchCount = 0;
  if (!ready_) {
    return false;
  }

  const uint32_t now = millis();
  if (now - lastReadMs_ < 12) {
    return false;
  }
  lastReadMs_ = now;

  touch_data_t data;
  bsp_touch_read();
  if (!bsp_touch_get_coordinates(&data) || data.touch_num == 0) {
    return true;
  }

  const uint16_t x = data.coords[0].x;
  const uint16_t y = data.coords[0].y;
  if (x >= width_ || y >= height_) {
    return true;
  }

  point.x = x;
  point.y = y;
  point.touchCount = data.touch_num;
  if (data.touch_num > 1) {
    const uint16_t x2 = data.coords[1].x;
    const uint16_t y2 = data.coords[1].y;
    if (x2 < width_ && y2 < height_) {
      point.x2 = x2;
      point.y2 = y2;
    } else {
      point.touchCount = 1;
    }
  }
  point.pressed = true;
  return true;
}
