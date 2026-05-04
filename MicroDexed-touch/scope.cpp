#include "scope.h"
//#include "UI.h"
#ifndef ESP_DISPLAY
#include "ILI9341_t3n.h"
#else 
#include "esp32.h"
#endif

#if defined ESP_DISPLAY
extern espdisplay display;
#else
extern ILI9341_t3n display;
#endif

void Realtime_Scope::update(void)
{
  audio_block_t* block = receiveReadOnly(0);
  if (block) {
    if (updateBuffer) {
   //  if (currentScopeSettings.enabled && updateBuffer) {
      updateBuffer = false;

      if (scope_is_drawing == false) {
        __disable_irq();
        memcpy(scopebuffer, block->data, AUDIO_BLOCK_SAMPLES * 2);
        __enable_irq();
      }
    }
    release(block);
  }
}

void Realtime_Scope::draw_scope(uint16_t x, int y, uint8_t w, uint8_t h)
{
 // if (!currentScopeSettings.enabled) return;

#if defined ESP_DISPLAY
static uint8_t scopeDivider = 0;
if (++scopeDivider < 2) return;  // send every 4th block → ~86 Hz
scopeDivider = 0;
#endif

  scope_is_drawing = true;
  display.console = false;

  w = std::min(uint8_t(AUDIO_BLOCK_SAMPLES), w); // limit width

  const int16_t halfHeight = h / 2;

  // Compute new scope values from audio data (always)
  for (uint16_t i = 0; i < w; i++) {
    const int16_t signFactor = (scopebuffer[i] & 0x8000) ? -halfHeight : halfHeight;
    const uint16_t value = abs(scopebuffer[i]);

    scopebuffer[i] = signFactor * log10f(1.0 + (9 * value / 32768.0)) + halfHeight; // log plot
  }

//Check if the entire scope buffer is empty (no audio)
bool allZero = true;
for (uint16_t i = 0; i < w; i++) {
   if (abs(scopebuffer[i] - halfHeight) > 1) {
        allZero = false;
        break;
    }
}
if (allZero) {
    // Nothing to draw – skip sending
    updateBuffer = true;
    scope_is_drawing = false;
    return;
}

#if defined ESP_DISPLAY
  // Send Y‑values to ESP in a single packet
  uint8_t yvals[AUDIO_BLOCK_SAMPLES];
  for (uint16_t i = 0; i < w; i++) {
    yvals[i] = (uint8_t)scopebuffer[i];   // already pixel offset
  }
  display.sendScopeData(x, (uint8_t)y, w, h, yvals);
#else
  for (uint16_t i = 0; i < w; i++) {
    if (scopebuffer_old[i] != scopebuffer[i]) {
      display.drawPixel(x + i, scopebuffer_old[i] + y, COLOR_BACKGROUND);
      display.drawPixel(x + i, scopebuffer[i] + y, COLOR_SYSTEXT);
      scopebuffer_old[i] = scopebuffer[i];
    }
  }
#endif

  updateBuffer = true;
  scope_is_drawing = false;
}

