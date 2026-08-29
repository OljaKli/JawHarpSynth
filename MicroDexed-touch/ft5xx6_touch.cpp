#if defined RA_TOUCH_FT5XX6

#include "ft5xx6_touch.h"
#include <Wire.h>

extern ts_t ts;
extern config_t configuration;

bool FT5xx6_Touch::begin() {
    Wire.begin();
    ft.begin(Wire, FT5XX6_UNUSED_PIN, NULL);
    ft.setMode(FT5xx6_Mode_Polling);
    return true;
}

uint8_t FT5xx6_Touch::touched() {
    ft.update();
    if (ft.newTouch) {
        FT5xx6_TouchRecord_TypeDef rec = ft.read();
        numTouches = rec.numTouches;
        if (numTouches > 0) {
            // uint16_t rawX = rec.t1x;
            // uint16_t rawY = rec.t1y;
               
 //#if defined ...
         ts.p.x = (rec.t1x) / 2;
         ts.p.y = (rec.t1y) / 2;
// #else
//         ts.p.x = (800 - rec.t1x) / 2;
//         ts.p.y = (480 - rec.t1y) / 2;
// #endif
        }
    }
    return numTouches;
}

TS_Point FT5xx6_Touch::getPoint() {
    return TS_Point(ts.p.x, ts.p.y);
}

// Global instance
FT5xx6_Touch touch;
#endif