/**************************************************************************/
/*!
    @file       GSL1680.cpp
    @OG author  Skallwar
    @fixer      DOP/Syvel

    @thanks to wolfmanjm for the source code this lib is bassed on : https://github.com/wolfmanjm/GSL1680

    Heavily modified or MDT in 2026 by positionhigh
*/

#if defined RA_DISPLAY

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "touch.h"
extern ts_t ts;
#include <stdint.h>

#include "GSL1680.h"
#include "gslX680firmware.h"

#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined (RA_DISPLAY)
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined (RA_DISPLAY)
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

extern void bootMessage(const char* msg);

// Registres
#define I2CADDR 0x40
#define DATA_REG 0x80
#define STATUS_REG 0xE0
#define PAGE_REG 0xF0

extern int numTouchPoints;

struct Touch_event ts_event;

FLASHMEM void displaybootmessage()
{
    display.setTextSize(2);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setCursor(0, 0);
}

FLASHMEM void GSL1680::begin()
{
    //    bootMessage("INIT TOUCH");
    //     delay(10);

    bootMessage("GSL1680: Start boot up sequence");

    // pinMode(WAKE, OUTPUT);          //
    // digitalWrite(WAKE, LOW);        //
    // pinMode(TFT_TOUCH_IRQ, INPUT);  // Startup sequence PIN part

    // delay(30);
    //bootMessage("TOGGLE WAKE");
    // digitalWrite(WAKE, HIGH);
    // delay(50);
    // digitalWrite(WAKE, LOW);
    // delay(50);
    // digitalWrite(WAKE, HIGH);
    //delay(5);

    Wire.begin();
    Wire.setClock(500000);
    //Wire.setDefaultTimeout(2000);

    // CTP startup sequence
    bootMessage("GSL1680:Clear reg");
    clear_reg();
    bootMessage("GSL1680:Reset chip");
    reset();
    bootMessage("GSL1680: Load FW");
    loadfw();
    bootMessage("GSL1680: Reset chip 2");
    reset();
    bootMessage("GSL1680: Startup Chip");
    startchip();
    bootMessage("GSL1680: Boot up complete");
    Wire.setClock(200000);
}

FLASHMEM void GSL1680::clear_reg()
{
    uint8_t REG[4] = { 0xe0, 0x80, 0xe4, 0xe0 };
    uint8_t DATA[4] = { 0x88, 0x03, 0x04, 0x00 };
    uint8_t TIMER[4] = { 1, 1, 1, 1 };

    int i;
    for (i = 0; i < 4; ++i) {
        Wire.beginTransmission(I2CADDR);
        Wire.write(REG[i]);
        Wire.write(DATA[i]);
        int r = Wire.endTransmission();
        if (r != 0) {
#ifdef DEBUG
            LOG.print("i2c write error: ");
            LOG.print(r);
            LOG.print(" ");
            LOG.println(REG[i], HEX);
#endif
        }
        delay(TIMER[i]);
    }

}

FLASHMEM void GSL1680::reset()
{
    uint8_t REG[2] = { STATUS_REG, 0xE4 };
    uint8_t DATA[2] = { 0x88, 0x04 };
    uint8_t TIMER[2] = { 1, 1 };

    int i;
    for (i = 0; i < 2; ++i) {
        Wire.beginTransmission(I2CADDR);
        Wire.write(REG[i]);
        Wire.write(DATA[i]);
        int r = Wire.endTransmission();
#ifdef DEBUG
        if (r != 0) {
            LOG.print("i2c write error: "); LOG.print(r); LOG.print(" "); LOG.println(REG[i], HEX);
        }
#endif
        delay(TIMER[i]);
    }

    uint8_t DATA_2[4] = { 0,0,0,0 };

    datasend(0xBC, DATA_2, 4);
    delay(1);
}

FLASHMEM void GSL1680::loadfw()
{
    uint8_t addr;
    uint8_t Wrbuf[4];
    size_t source_len = sizeof(GSLX680_FW) / sizeof(struct fw_data);

    for (size_t source_line = 0; source_line < source_len; source_line++) {
        addr = GSLX680_FW[source_line].offset;
        Wrbuf[0] = (char)(GSLX680_FW[source_line].val & 0x000000ff);
        Wrbuf[1] = (char)((GSLX680_FW[source_line].val & 0x0000ff00) >> 8);
        Wrbuf[2] = (char)((GSLX680_FW[source_line].val & 0x00ff0000) >> 16);
        Wrbuf[3] = (char)((GSLX680_FW[source_line].val & 0xff000000) >> 24);

        datasend(addr, Wrbuf, 4);
    }
}

FLASHMEM void GSL1680::startchip()
{
    Wire.beginTransmission(I2CADDR);

    Wire.write(0xE0);   //Registre
    Wire.write(0x00);   //DATA

    int r = Wire.endTransmission();
#ifdef DEBUG
    if (r != 0) {
        LOG.print("i2c write error: "); LOG.print(r); LOG.print(" "); LOG.println(0xE0, HEX);
    }
#endif
}

void GSL1680::sleep()
{

}

void GSL1680::datasend(uint8_t REG, uint8_t DATA[], uint16_t NB)
{

    Wire.beginTransmission(I2CADDR);

    Wire.write(REG);

    for (uint16_t i = 0; i < NB; i++) {
        Wire.write(DATA[i]);
    }

    int r = Wire.endTransmission();
#ifdef DEBUG
    if (r != 0) {
        LOG.print("i2c write error: "); LOG.print(r); LOG.print(" "); LOG.println(REG, HEX);
    }
#endif

}

uint8_t GSL1680::dataread()
{
    uint8_t TOUCHRECDATA[60] = { 0 };

    Wire.beginTransmission(I2CADDR);

    Wire.write(DATA_REG);

    int n = Wire.endTransmission();
#ifdef DEBUG
    if (n != 0) {
        LOG.print("i2c write error: "); LOG.print(n); LOG.print(" "); LOG.println(DATA_REG, HEX);
    }
#endif
    n = Wire.requestFrom(I2CADDR, 60);
#ifdef DEBUG
    if (n != 60) {
        LOG.print("i2c read error: did not get expected count "); LOG.print(n); LOG.print("/"); LOG.println("24");
    }
#endif
    for (int i = 0; i < n; i++) {
        TOUCHRECDATA[i] = Wire.read();

    }

    ts_event.NBfingers = TOUCHRECDATA[0];
    for (int i = 1; i <= ts_event.NBfingers; i++) {
        ts_event.fingers[i].y = (((((uint16_t)TOUCHRECDATA[(i * 4) + 3]) << 8) | (uint16_t)TOUCHRECDATA[(i * 4) + 2]));
        ts_event.fingers[i].y = ts_event.fingers[i].y << 4;
        ts_event.fingers[i].y = ts_event.fingers[i].y >> 4;
        ts_event.fingers[i].x = ((((uint16_t)TOUCHRECDATA[(i * 4) + 1]) << 8) | (uint16_t)TOUCHRECDATA[(i * 4)]); // 12 bits of X coord
        ts_event.fingers[i].fingerID = i - 1; // finger that did the touch
    }

    return ts_event.NBfingers;
}

uint8_t GSL1680::readFingerID(int NB)
{
    return ts_event.fingers[NB].fingerID;
}

uint32_t GSL1680::readFingerX(int NB)
{
    return ts_event.fingers[NB].x;
}

uint32_t GSL1680::readFingerY(int NB)
{
    return ts_event.fingers[NB].y;
}


uint8_t GSL1680::touched() {

    //if (digitalRead(TFT_TOUCH_IRQ) == HIGH) {
    lastTouchCount = dataread();
    if (lastTouchCount > 0) {
        uint16_t rawX = ts_event.fingers[1].x;
        uint16_t rawY = ts_event.fingers[1].y;

        ts.p.x = (800 - rawX) / 2;
        ts.p.y = (480 - rawY) / 2;
    }
    // }

    return lastTouchCount;

}

TS_Point GSL1680::getPoint() {
    return lastPoint;
}

#endif