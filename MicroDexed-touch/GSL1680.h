#if defined RA_DISPLAY
/**************************************************************************/
/*!
    @file       GSL1680.h
    @OG author  Skallwar
    @fixer      DOP/Syvel

    @thanks to wolfmanjm for the source code this lib is bassed on : https://github.com/wolfmanjm/GSL1680
*/

#ifndef GSL1680_F
#define GSL1680_F

#include <stdint.h>

struct TS_Point {
    int16_t x;
    int16_t y;
    TS_Point() : x(0), y(0) {}
    TS_Point(int16_t x0, int16_t y0) : x(x0), y(y0) {}
};

class GSL1680 {
    public:
        
        void begin();
        uint8_t dataread();
        uint8_t readFingerID(int NB);
        uint32_t readFingerX(int NB);
        uint32_t readFingerY(int NB);

    uint8_t touched();
    TS_Point getPoint();

    private:
        bool GSL1680_DEBUG_ERROR;
        bool GSL1680_DEBUG_INFO;
        void clear_reg();
        void reset();
        void loadfw();
        void startchip();
        void sleep();
        void datasend(uint8_t REG, uint8_t DATA[], uint16_t NB);
        uint8_t lastTouchCount;
    TS_Point lastPoint;
};

struct Finger {
    uint8_t fingerID;
    uint16_t x;
    uint16_t y;
};

struct Touch_event {
    uint8_t NBfingers;
    struct Finger fingers[2];
};

#endif
#endif