#include "esp_lcd_touch_axs15231b.h"

namespace {

TwoWire* touchI2c = nullptr;
uint16_t touchWidth = 0;
uint16_t touchHeight = 0;
uint16_t touchRotation = 0;
touch_data_t lastTouch = {};

bool touchI2cWriteRead(uint8_t driverAddr,
                       const uint8_t* writeBuf,
                       uint32_t writeLen,
                       uint8_t* readBuf,
                       uint32_t readLen) {
  if (touchI2c == nullptr) {
    return false;
  }

  touchI2c->beginTransmission(driverAddr);
  touchI2c->write(writeBuf, writeLen);
  if (touchI2c->endTransmission() != 0) {
    return false;
  }

  touchI2c->requestFrom(driverAddr, readLen);
  if (touchI2c->available() != static_cast<int>(readLen)) {
    return false;
  }
  touchI2c->readBytes(readBuf, readLen);
  return true;
}

}  // namespace

void bsp_touch_init(TwoWire* touch_i2c, int tp_rst, uint16_t rotation, uint16_t width, uint16_t height) {
  touchI2c = touch_i2c;
  touchWidth = width;
  touchHeight = height;
  touchRotation = rotation;
  lastTouch.touch_num = 0;

  if (tp_rst != -1) {
    pinMode(tp_rst, OUTPUT);
    digitalWrite(tp_rst, LOW);
    delay(200);
    digitalWrite(tp_rst, HIGH);
    delay(300);
  }
}

void bsp_touch_read(void) {
  uint8_t data[14] = {0};
  const uint8_t cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};

  if (!touchI2cWriteRead(AXS5106L_ADDR, cmd, sizeof(cmd), data, sizeof(data))) {
    lastTouch.touch_num = 0;
    return;
  }

  if (data[1] == 0 || data[2] == 0 || data[3] < 2 || data[5] < 2) {
    lastTouch.touch_num = 0;
    return;
  }

  if (data[0] == 0xff || data[1] > MAX_TOUCH_MAX_POINTS) {
    lastTouch.touch_num = 0;
    return;
  }

  lastTouch.touch_num = data[1];
  for (uint8_t i = 0; i < lastTouch.touch_num; i++) {
    lastTouch.coords[i].x = ((data[6 * i + 2] & 0x0F) << 8) | data[6 * i + 3];
    lastTouch.coords[i].y = ((data[6 * i + 4] & 0x0F) << 8) | data[6 * i + 5];
  }
}

bool bsp_touch_get_coordinates(touch_data_t* touch_data) {
  if (touch_data == nullptr || lastTouch.touch_num == 0) {
    return false;
  }

  touch_data->touch_num = lastTouch.touch_num;
  for (uint8_t i = 0; i < lastTouch.touch_num; i++) {
    switch (touchRotation) {
      case 1:
        touch_data->coords[i].y = touchHeight - 1 - lastTouch.coords[i].x;
        touch_data->coords[i].x = lastTouch.coords[i].y;
        break;
      case 2:
        touch_data->coords[i].x = touchWidth - 1 - lastTouch.coords[i].x;
        touch_data->coords[i].y = touchHeight - 1 - lastTouch.coords[i].y;
        break;
      case 3:
        touch_data->coords[i].y = lastTouch.coords[i].x;
        touch_data->coords[i].x = touchWidth - 1 - lastTouch.coords[i].y;
        break;
      default:
        touch_data->coords[i].x = lastTouch.coords[i].x;
        touch_data->coords[i].y = lastTouch.coords[i].y;
        break;
    }
  }
  return true;
}
