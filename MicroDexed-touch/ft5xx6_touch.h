#if defined RA_TOUCH_FT5XX6

#ifndef FT5XX6_TOUCH_H
#define FT5XX6_TOUCH_H

#include "config.h"
#include "touch.h"  // for TS_Point and ts_t
#include "SparkFun_FT5xx6.h"

class FT5xx6_Touch {
public:
    FT5xx6_Touch() : ft() {}
    bool begin();
    uint8_t touched();
    TS_Point getPoint();

private:
    FT5316 ft;
    uint8_t numTouches = 0;
};

extern FT5xx6_Touch touch;

#endif
#endif