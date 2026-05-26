#include "board/TouchDriver.h"

#include <Wire.h>

#include "board/BoardPins.h"
#include "esp_lcd_touch_axs15231b.h"

bool TouchDriver::begin(bool landscape, uint16_t width, uint16_t height) {
  Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL, BoardPins::I2C_FREQUENCY);
  setOrientation(landscape, width, height);
  ready_ = true;
  Serial.println("AXS15231B touch ready");
  return true;
}

void TouchDriver::setOrientation(bool landscape, uint16_t width, uint16_t height) {
  (void)width;
  (void)height;
  width_ = landscape ? BoardPins::LCD_LANDSCAPE_WIDTH : BoardPins::LCD_PHYSICAL_WIDTH;
  height_ = landscape ? BoardPins::LCD_LANDSCAPE_HEIGHT : BoardPins::LCD_PHYSICAL_HEIGHT;
  const uint16_t rotation = landscape ? 1 : 0;
  bsp_touch_init(&Wire,
                 -1,
                 rotation,
                 width_,
                 height_);
}

bool TouchDriver::read(TouchPoint& point) {
  point.pressed = false;
  if (!ready_) {
    return false;
  }

  const uint32_t now = millis();
  if (now - lastReadMs_ < 25) {
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
  point.pressed = true;
  return true;
}
