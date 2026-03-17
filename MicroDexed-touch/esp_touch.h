#if defined ESP_DISPLAY

#ifndef _esp_touchscreen_h_
#define _esp_touchscreen_h_

#include "Arduino.h"

class TS_Point
{
public:
  TS_Point(void)
    : x(0), y(0), z(0) {}
  TS_Point(int16_t x, int16_t y, int16_t z)
    : x(x), y(y), z(z) {}
  int16_t x, y, z;
};

class esp_touchscreen
{
public:
  constexpr esp_touchscreen(uint8_t cspin)
    : csPin(cspin) {}
  bool begin();
  TS_Point getPoint();
  TS_Point getPixel();
  bool touched();

private:
  void update();
  uint8_t csPin, rotation = 1;
  int16_t xraw = 0, yraw = 0, zraw = 0;
};

#endif
#endif