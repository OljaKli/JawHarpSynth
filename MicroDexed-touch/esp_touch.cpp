#include "config.h"
#if defined ESP_DISPLAY
#include "esp_touch.h"
#define Z_THRESHOLD 400

FLASHMEM bool esp_touchscreen::begin()
{
  return true;
}

TS_Point esp_touchscreen::getPoint()
{
  return TS_Point(xraw, yraw, zraw);
}

TS_Point esp_touchscreen::getPixel()
{
  uint16_t xPixel = 0;
  uint16_t yPixel = 0;
  return TS_Point(xPixel, yPixel, zraw);
}

bool esp_touchscreen::touched()
{
  update();
  bool isTouched = (zraw >= Z_THRESHOLD);
  return isTouched;
}

void esp_touchscreen::update()
{
}

#endif
