#ifndef LIVESEQ_EDITOR_SHARED_H
#define LIVESEQ_EDITOR_SHARED_H

#include <stdint.h>

// Velocity slider geometry (identical for both editors)
static constexpr uint16_t VEL_SLIDER_X = 200;
static constexpr uint16_t VEL_SLIDER_W = 98;
static constexpr uint16_t VEL_SLIDER_Y = 8;
static constexpr uint16_t VEL_SLIDER_H = 8;
static constexpr uint16_t VEL_VALUE_X = VEL_SLIDER_X + VEL_SLIDER_W + 5;
static constexpr uint16_t VEL_TEXT_X = 178;
static constexpr uint16_t TOP_TEXT_Y = 12;

// Shared UI functions
uint16_t velocityToColor(uint8_t vel);
void drawVelocitySlider(uint8_t currentVelocity);
void setVelocityFromTouch(int16_t x, uint8_t &currentVelocity);

void drawStartStopButton(uint16_t x, uint16_t y, bool isRunning);
void drawLayerButton(uint16_t x, uint16_t y, uint8_t activeLayer);
void drawCopyPasteButton(uint16_t x, uint16_t y, bool copyModeActive);

#endif