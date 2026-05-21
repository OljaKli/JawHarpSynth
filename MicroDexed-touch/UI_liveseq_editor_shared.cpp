#include "UI_liveseq_editor_shared.h"
#include "config.h"
#include "touch.h"
#include "UI.h"

extern bool remote_active;
#ifdef ESP_DISPLAY
extern espdisplay display;
#else
extern ILI9341_t3n display;
#endif
extern uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val);

uint16_t velocityToColor(uint8_t vel) {
    if (vel == 0) return 0;
    uint8_t brightness = 60 + (vel * 194) / 127;   // from drum editor
    return ColorHSV(80, 255, brightness);
}

FLASHMEM void drawVelocitySlider(uint8_t currentVelocity) {
    if (remote_active) display.console = true;
    display.fillRect(VEL_TEXT_X - 5, VEL_SLIDER_Y - 3, VEL_SLIDER_W + 45, VEL_SLIDER_H + 6, COLOR_BACKGROUND);
    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setCursor(VEL_TEXT_X, VEL_SLIDER_Y + 1);
    display.print("VEL");
    if (remote_active) display.console = true;
    display.fillRect(VEL_SLIDER_X, VEL_SLIDER_Y, VEL_SLIDER_W, VEL_SLIDER_H, GREY3);
    uint16_t fillWidth = (currentVelocity * VEL_SLIDER_W) / 127;
    if (fillWidth > 0) {
        uint16_t color = velocityToColor(currentVelocity);
        if (remote_active) display.console = true;
        display.fillRect(VEL_SLIDER_X, VEL_SLIDER_Y, fillWidth, VEL_SLIDER_H, color);
    }
    uint16_t markerX = VEL_SLIDER_X + fillWidth;
    display.drawLine(markerX, VEL_SLIDER_Y - 2, markerX, VEL_SLIDER_Y + VEL_SLIDER_H + 2, COLOR_SYSTEXT);
    display.setCursor(VEL_VALUE_X, VEL_SLIDER_Y);
    display.printf("%3d", currentVelocity);
}

FLASHMEM void setVelocityFromTouch(int16_t x, uint8_t &currentVelocity) {
    if (x < VEL_SLIDER_X) x = VEL_SLIDER_X;
    if (x > VEL_SLIDER_X + VEL_SLIDER_W) x = VEL_SLIDER_X + VEL_SLIDER_W;
    uint8_t newVel = ((x - VEL_SLIDER_X) * 127) / VEL_SLIDER_W;
    if (newVel < 1) newVel = 1;
    if (newVel != currentVelocity) {
        currentVelocity = newVel;
        drawVelocitySlider(currentVelocity);
    }
}

FLASHMEM void drawStartStopButton(uint16_t x, uint16_t y, bool isRunning) {
    if (isRunning) {
        TouchButton::drawButton(x, y, "STOP", "SEQ", TouchButton::BUTTON_RED);
    } else {
        TouchButton::drawButton(x, y, "START", "SEQ", TouchButton::BUTTON_ACTIVE);
    }
}

FLASHMEM void drawLayerButton(uint16_t x, uint16_t y, uint8_t activeLayer) {
    char layerNum[4];
    sprintf(layerNum, "%u", activeLayer + 1);
    TouchButton::drawButton(x, y, "LAYER", layerNum, TouchButton::BUTTON_HIGHLIGHTED);
}

FLASHMEM void drawCopyPasteButton(uint16_t x, uint16_t y, bool copyModeActive) {
    if (copyModeActive) {
        TouchButton::drawButton(x, y, "PASTE", "PAT", TouchButton::BUTTON_ACTIVE);
    } else {
        TouchButton::drawButton(x, y, "COPY", "PAT", TouchButton::BUTTON_ACTIVE);
    }
}