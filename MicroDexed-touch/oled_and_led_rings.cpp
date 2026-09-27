#include <Arduino.h>
#include "config.h"
#include "sequencer.h"
#include "UI.h"
#include <LCDMenuLib2.h>
#include <Audio.h>                // for AudioAnalyzePeak
#include "synth_dexed.h"          // for AudioSynthDexed (if needed)
#include "touch.h"
#include <SD.h>
#include "screensaver.h"

#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined RA_DISPLAY
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif


#if defined OLED_DISPLAYS
#include <Adafruit_SSD1306.h>
extern char last_right_helptext[21];
extern char last_left_helptext[21];
extern Adafruit_SSD1306 oled_display;
elapsedMillis OLEDTimer;
extern bool oled_ring_screensaver_active;
uint8_t oled_fb_owner = 255;
#endif

#if defined MCP_23017
#include "MCP23017.h"
extern MCP23017 mcp;
#endif

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined RA_DISPLAY
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

#if defined LED_RINGS
#include "LEDRingSmall.h"

extern LEDRingSmall LEDRing_l;
extern LEDRingSmall LEDRing_r;
extern int glow_input_value;
extern int glow_limit_min;
extern int glow_limit_max;
extern bool led_ring_preview_mode;
#endif

#if defined GLOW
extern int glow_menu_item;
#endif

extern LCDMenuLib2 LCDML;
extern sequencer_t seq;
extern config_t configuration;
extern uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val);
extern uint16_t RGB24toRGB565(uint8_t r, uint8_t g, uint8_t b);
extern bool multiband_active;
extern bool sidechain_active;

//LED RING DEBUG

#if  defined LED_DEBUG
uint8_t ledRingL_colors[24][3];
uint8_t ledRingR_colors[24][3];

#include <string.h>   // for memcmp, memcpy

void draw_ring_simulator() {
  // Static arrays to remember the last drawn state
  static uint8_t lastColorsL[24][3] = { 0 };
  static uint8_t lastColorsR[24][3] = { 0 };
  bool changed = false;

  // Check if any color changed
  for (uint8_t i = 0; i < 24; i++) {
    if (memcmp(ledRingL_colors[i], lastColorsL[i], 3) != 0 ||
      memcmp(ledRingR_colors[i], lastColorsR[i], 3) != 0) {
      changed = true;
      break;
    }
  }
  if (!changed) return;

  // Update last known state
  for (uint8_t i = 0; i < 24; i++) {
    memcpy(lastColorsL[i], ledRingL_colors[i], 3);
    memcpy(lastColorsR[i], ledRingR_colors[i], 3);
  }

  // ---- Draw the rings ----
  const int x_center_left = 80, y_center_left = 50;
  const int x_center_right = 240, y_center_right = 50;
  const int ring_radius = 40, led_radius = 3;

  // Lambda to draw one ring
  auto draw_ring = [&](int cx, int cy, uint8_t colors[24][3]) {
    for (int i = 0; i < 24; i++) {
      // Physical LED index (respect hardware mapping)
      int physIdx = (configuration.sys.led_ring_start_offset + i) % 24;
      // Anti‑clockwise: angle = -π/2 - i * (2π/24)
      float angle = -M_PI / 2.0f - physIdx * (2.0f * M_PI / 24.0f);
      int x = cx + ring_radius * cos(angle);
      int y = cy + ring_radius * sin(angle);

      uint8_t r = colors[physIdx][0];
      uint8_t g = colors[physIdx][1];
      uint8_t b = colors[physIdx][2];
      uint16_t color = RGB24toRGB565(r, g, b);

      display.fillCircle(x, y, led_radius, color);
      //display.drawCircle(x, y, led_radius, GREY3); // border
    }
    };

  draw_ring(x_center_left, y_center_left, ledRingL_colors);
  draw_ring(x_center_right, y_center_right, ledRingR_colors);
}
#endif

#ifdef MINI_ENCODER
extern bool I2C_encoderActivity;
extern elapsedMillis I2C_encoderActivityTimer;
extern int16_t currentBPM;
extern int16_t currentVol;
extern uint8_t lastEncoder;
// ---- Tap-Tempo extern declarations ----
extern bool tapTempoActive;
extern uint8_t tapCount;
extern uint32_t lastTapTime;
#endif


#if defined LED_RINGS || defined LED_DEBUG

FLASHMEM void get_led_color(uint8_t ring, uint8_t scheme, float pos, uint8_t& r, uint8_t& g, uint8_t& b) {
  pos = constrain(pos, 0.0f, 1.0f);
  uint8_t hue = 0;
  uint8_t sat = 255;
  uint8_t val = 255;

  // Only handle schemes 0-9 here; scheme 10 is handled inline by the caller.
  switch (scheme) {
  case 0: r = 0;   g = 64 + 191 * pos; b = 0;                    return;
  case 1: r = 0;   g = 0;               b = 64 + 191 * pos;      return;
  case 2: r = 64 + 191 * pos; g = 0;   b = 0;                    return;
  case 3: hue = pos * 359; break;
  case 4: r = 255; g = 255 * (1 - pos); b = 0;                  return;
  case 5: r = 0;   g = 255 * (1 - pos); b = 255;                return;
  case 6: r = 255 * (1 - pos); g = 0;   b = 255;                return;
  case 7: r = 255; g = 128 + 127 * pos; b = 0;                  return;
  case 8: r = 255; g = 64 * pos;        b = 0;                  return;
  case 9: r = 0;   g = 255 * pos;       b = 255 * (1 - pos);    return;
  default: r = g = b = 0; return;
  }

  uint16_t color = ColorHSV(hue, sat, val);
  r = (color >> 11) & 0x1F; r <<= 3;
  g = (color >> 5) & 0x3F; g <<= 2;
  b = color & 0x1F; b <<= 3;
}

// ring: 0 = left, 1 = right
FLASHMEM void setLEDRing(uint8_t ring, uint8_t led, uint8_t r, uint8_t g, uint8_t b) {
  if (ring == 0) {
    // Update stored colors
#if defined LED_DEBUG
    ledRingL_colors[led][0] = r;
    ledRingL_colors[led][1] = g;
    ledRingL_colors[led][2] = b;
#endif
    // Write to hardware 
#if defined LED_RINGS
    LEDRing_l.LEDRingSmall_Set_RED(led, r);
    LEDRing_l.LEDRingSmall_Set_GREEN(led, g);
    LEDRing_l.LEDRingSmall_Set_BLUE(led, b);
#endif
  }
  else {
#if defined LED_DEBUG
    ledRingR_colors[led][0] = r;
    ledRingR_colors[led][1] = g;
    ledRingR_colors[led][2] = b;
#endif
#if defined LED_RINGS
    LEDRing_r.LEDRingSmall_Set_RED(led, r);
    LEDRing_r.LEDRingSmall_Set_GREEN(led, g);
    LEDRing_r.LEDRingSmall_Set_BLUE(led, b);
#endif
  }
}

#endif //LED RING WRAPPER/DEBUG END


#if defined LED_RINGS || defined LED_DEBUG

uint8_t leftRingMode = LEFT_RING_MODE_VOLUME;   // default to volume

// Convert a custom colour value to RGB.
//   0-100  -> hue on the colour wheel (255 sat, 255 val)
//   101-200 -> greyscale, 101 = dark grey, 200 = white
FLASHMEM inline void custom_value_to_rgb(uint8_t value,
                                         uint8_t& r, uint8_t& g, uint8_t& b)
{
  if (value <= 100) {
    float hue = (float)value * 3.59f;
    uint16_t color = ColorHSV((uint16_t)hue, 255, 255);
    r = (color >> 11) & 0x1F; r <<= 3;
    g = (color >> 5) & 0x3F; g <<= 2;
    b =  color        & 0x1F; b <<= 3;
  } else {
    // 101..200 -> linear ramp from dark grey to white
    const uint8_t GREY_MIN = 30;      // value at 101
    const uint8_t GREY_MAX = 255;     // value at 200
    float t = (value - 101) / 99.0f;  // 0.0 .. 1.0
    uint8_t v = GREY_MIN + (uint8_t)(t * (GREY_MAX - GREY_MIN) + 0.5f);
    r = g = b = v;
  }
}

FLASHMEM float get_led_brightness_factor(uint8_t relIdx, uint8_t gradient)
{
  // OFF
  if (configuration.sys.led_brightness == 0) return 0.0f;

  // Brightest end of the ring, per mode.
  float max_f;
  switch (configuration.sys.led_brightness) {
    case 1: max_f = 0.40f; break;   // LOW
    case 2: max_f = 0.55f; break;   // MID
    case 3: max_f = 0.75f; break;   // HIGH
    default: return 0.0f;
  }

  // Dimmest end of the ring.
  const float MIN_F = 0.30f;

  // ---- Shape ---------------------------------------------------
  //   HOLD        fraction of the ring held at the dim value
  //   RAMP        steepness of the ramp above HOLD
  //   FLICKER_FIX small additive lift applied only to the dim
  //               region, to keep LEDs above the PWM flicker
  //               threshold of the IS31FL3746A
  const float HOLD        = 0.90f;   // even more dark LEDs
  const float RAMP        = 4.5f;    // much steeper top end
  const float FLICKER_FIX = 0.06f;   // stops the first LED flickering

  // Gradient amount: 0 = flat, 1 = full sweep.
  float mix   = constrain(gradient / 100.0f, 0.0f, 1.0f);
  float dim_f = max_f + (MIN_F - max_f) * mix;
  if (dim_f > max_f) dim_f = max_f;

  // Position on the ring: 0 = dim end, 1 = bright end.
  float t = constrain(relIdx / 23.0f, 0.0f, 1.0f);

  // Flat hold, then steep ramp.
  float curve;
  if (t < HOLD) {
    curve = 0.0f;
  } else {
    float u = (t - HOLD) / (1.0f - HOLD);
    curve = powf(u, RAMP);
  }

  float factor = dim_f + (max_f - dim_f) * curve;

  // Additive lift, weighted so it is full in the flat/dim region
  // and fades out as the ramp climbs. This lifts LEDs that would
  // otherwise sit in the chip's flicker band.
  factor += FLICKER_FIX * (1.0f - curve);

  if (factor > max_f) factor = max_f;
  return factor;
}

FLASHMEM void draw_led_gradient_preview(uint8_t ring, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
  uint8_t scheme, uint8_t custom_start, uint8_t custom_end,
  uint8_t gradient) {
  for (uint16_t i = 0; i < w; i++) {
    // Map pixel position to LED index 0..23 (continuous)
    float pos = (float)i / (w - 1);
    uint8_t ledIdx = (uint8_t)(pos * 23.0f);   // 0..23
    float ledPos = (float)ledIdx / 23.0f;      // for colour interpolation within the scheme

    uint8_t r, g, b;
    if (scheme == 10) {
      float h_start = (float)custom_start * 3.59f;
      float h_end = (float)custom_end * 3.59f;
      float hue = h_start + (h_end - h_start) * ledPos;
      uint16_t color = ColorHSV((uint16_t)hue, 255, 255);
      r = (color >> 11) & 0x1F; r <<= 3;
      g = (color >> 5) & 0x3F; g <<= 2;
      b = color & 0x1F; b <<= 3;
    }
    else {
      get_led_color(ring, scheme, ledPos, r, g, b);
    }

    // Apply brightness gradient based on LED index
    float factor = get_led_brightness_factor(ledIdx, gradient);
    r = (uint8_t)(r * factor);
    g = (uint8_t)(g * factor);
    b = (uint8_t)(b * factor);
    uint16_t color = RGB24toRGB565(r, g, b);
    display.drawLine(x + i, y, x + i, y + h, color);
  }
}

#endif

#if defined OLED_DISPLAYS 
uint8_t oled_info_mode = 0;

extern uint8_t external_psram_size;
extern uint32_t psram_free_bytes;
extern uint8_t selected_instance_id;
extern AudioSynthDexed* MicroDexed[NUM_DEXED];
extern sequencer_t seq;
extern config_t configuration;
extern uint8_t sd_card_internal;
extern uint8_t LED_MIDI_IN_counter;
extern uint8_t LED_MIDI_OUT_counter;
extern char g_voice_name[NUM_DEXED][VOICE_NAME_LEN];
extern SdVolume volume_internal;
extern uint8_t midi_bpm;

#ifdef SECOND_SD
extern uint8_t sd_card_external;
extern SdVolume volume_external;
#endif

// ---- Helper: print truncated string ----
FLASHMEM void print_trunc(const char* str, uint8_t maxLen = 10) {
  uint8_t len = strlen(str);
  if (len > maxLen) len = maxLen;
  for (uint8_t i = 0; i < len; i++) oled_display.print(str[i]);
}


FLASHMEM void print_sd_total(SdVolume& vol, bool present, const __FlashStringHelper* label = nullptr) {
  if (!present) {
    if (label) display.print(label);
    display.print(F("NA"));
    return;
  }

  // Compute total size in MB using the same method as _print_filemanager_header()
  uint32_t volumesize = vol.blocksPerCluster();
  volumesize *= vol.clusterCount();
  if (volumesize < 0X800000) {
    volumesize *= 512;   // SD blocks are 512 bytes
    volumesize /= 1024;  // convert to KB
  }
  else {
    volumesize /= 2;     // already in KB (for large cards)
  }
  volumesize /= 1024;      // now in MB
  volumesize /= 1024;      // now in GB
  if (label) oled_display.print(label);
  oled_display.print(volumesize);
  oled_display.print(F(" GB"));
}

// ---- Helper: print Dexed instance on right display (2 lines) ----
FLASHMEM void print_dexed_right(uint8_t inst) {
  oled_display.setCursor(0, 0);
  oled_display.print(F("D"));
  oled_display.print(inst + 1);
  oled_display.print(F(":"));
  char name[8];
  strncpy(name, g_voice_name[inst], 7);
  name[7] = '\0';
  oled_display.print(name);
  oled_display.setCursor(0, 16);
  oled_display.print(F("CHANNEL:"));
  uint8_t ch = configuration.dexed[inst].midi_channel;
  if (ch == 0) oled_display.print(F("OMNI"));
  else if (ch == 17) oled_display.print(F("OFF"));
  else oled_display.print(ch);
}

// ---- Helper: print Dexed instance single line (for non-main menu) ----
FLASHMEM void print_dexed_single(uint8_t inst) {
  oled_display.print(F("D"));
  oled_display.print(inst + 1);
  oled_display.print(F(":"));
  char name[5];
  strncpy(name, g_voice_name[inst], 4);
  name[4] = '\0';
  oled_display.print(name);
  oled_display.print(F(" C"));
  uint8_t ch = configuration.dexed[inst].midi_channel;
  if (ch == 0) oled_display.print(F("O"));
  else if (ch == 17) oled_display.print(F("-"));
  else oled_display.print(ch);
}

// ---- Helper: print mute matrix 
FLASHMEM void print_mute_matrix() {
  oled_display.setCursor(0, 0);
  oled_display.print(F("T:"));
  for (uint8_t i = 0; i < NUM_SEQ_TRACKS; i++) {
    oled_display.print(i + 1);
  }

  oled_display.setCursor(0, 16);
  oled_display.print(F("M:"));
  for (uint8_t i = 0; i < NUM_SEQ_TRACKS; i++) {
    oled_display.print(seq.track_mute[i] ? 'o' : 'x');
  }
}

#if defined MINI_ENCODER
void updateOLEDsTapTempo() {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  // ---- Only right OLED ----
  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, LOW);

  oled_display.clearDisplay();
  oled_display.setTextSize(2);
  oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  oled_display.setCursor(0, 0);
  if (tapCount == 0) {
    oled_display.print(F("TAP TEMPO"));
  }
  else if (tapCount == 1) {
    oled_display.print(F("TAP..."));
  }
  else {
    oled_display.print(F("BPM:"));
    oled_display.print(currentBPM);
  }

  oled_display.display();

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();
}
#endif

void clearRightOLED() {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, LOW);

  oled_display.clearDisplay();
  oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  // Show the last help text from the TFT
  oled_display.setCursor(0, 0);
  oled_display.print(last_right_helptext);

   oled_display.display();

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();

  oled_fb_owner = 1;
}

#if defined MINI_ENCODER
FLASHMEM void I2C_enc_updateRightOLED() {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, LOW);

  oled_display.clearDisplay();
  oled_display.setTextSize(2);
  oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  // ----- OLED right -----
  if (lastEncoder == 1) {
    oled_display.setCursor(0, 0);
    oled_display.print("BPM:");
    oled_display.print(currentBPM);
  }
  else if (lastEncoder == 2) {
    oled_display.setCursor(0, 0);
    oled_display.print("DEXED #");
    oled_display.print(selected_instance_id + 1);
    oled_display.setCursor(0, 16);
    oled_display.print("VOL: ");
    oled_display.print(currentVol);
    oled_display.print("%");
  }

  oled_display.display();

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();
  oled_fb_owner = 1;

  // ----- TFT Update  -----
  uint8_t currentID = LCDML.FUNC_getID();

  //  Voice Select
  if (currentID == LCDML.OTHER_getIDFromFunction(UI_func_voice_select)) {

    display.setTextSize(1);
    setModeColor(16);
    setCursor_textGrid_small(28, 6);
    print_formatted_number(seq.bpm, 3);

    // Volume
    print_small_intbar(11, 6, configuration.dexed[selected_instance_id].sound_intensity, 4, 1, 1);
  }

  //  Sequencer Settings
  else if (currentID == LCDML.OTHER_getIDFromFunction(UI_func_seq_settings)) {
    // BPM 
    display.setTextSize(1);
    setModeColor(7);
    setCursor_textGrid_small(23, 15);
    print_formatted_number(seq.bpm, 3, 7, 1);
  }
}
#endif

// Draws the screensaver countdown bar into the current OLED buffer.
// The caller is responsible for opening/closing the SPI transaction and
// for calling oled_display.display(). Silently does nothing when the bar
// should not be shown.
static FLASHMEM void oled_draw_screensaver_bar()
{
  if (configuration.sys.screen_saver_mode == ScreenSaver::DISABLED) return;
  if (oled_ring_screensaver_active) return;

    // Countdown bar on top of whatever was drawn above.
  // The library itself knows the remaining time, so we simply ask it.
  const uint32_t total_ms = (uint32_t)configuration.sys.screen_saver_start * 60000UL;
  if (total_ms > 0) {
    uint32_t remaining = LCDML.SCREEN_getTimeRemaining();
    if (remaining > total_ms) remaining = total_ms;

    constexpr int BAR_Y = 31;
    constexpr int BAR_W = 128;
    const uint8_t fill_w = (uint8_t)(((uint64_t)remaining * BAR_W) / total_ms);

    oled_display.drawFastHLine(0, BAR_Y, BAR_W, SSD1306_BLACK);
    if (fill_w > 0) {
      oled_display.drawFastHLine(0, BAR_Y, fill_w, SSD1306_WHITE);
    }
  }

}

// Called from the main loop. Redraws the countdown bar on the left OLED
// once per second, regardless of which menu page is currently active.
// Skips entirely while the screensaver is disabled or active, and while
// the tap-tempo overlay owns the OLEDs.
void OLED_update_screensaver_bar(void)
{
  // Same guard as OLED_status(): while tap-tempo is on, the OLEDs are
  // managed elsewhere and we must not interfere.
#if defined MINI_ENCODER
  if (tapTempoActive) return;
#endif

   if (configuration.sys.screen_saver_mode == ScreenSaver::DISABLED) return;
  if (oled_ring_screensaver_active) return;

  static uint32_t last_update_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - last_update_ms < 300) return;
  last_update_ms = now_ms;


  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  mcp.write1(OLED_CS_L, LOW);
  mcp.write1(OLED_CS_R, HIGH);

  // Draw only the bottom line on top of whatever is already in the buffer.
  oled_draw_screensaver_bar();
  oled_display.display();

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();
}

// ---------------------------------------------------------------------------
// OLED framebuffer ownership.
//
// Adafruit_SSD1306 has a single shared framebuffer for both panels. Every
// writer must therefore know which panel's content it just placed there.
// 0 = left, 1 = right, 255 = stale/unknown.
// ---------------------------------------------------------------------------

extern bool oled_ring_screensaver_active;

// ---------------------------------------------------------------------------
// Draws only the bottom pixel row of the current framebuffer as the
// screensaver countdown bar. Safe to call on top of any content. Does
// nothing when the bar should not be shown. No SPI handling, no
// display() call - the caller owns the SPI transaction.
// ---------------------------------------------------------------------------
FLASHMEM void oled_draw_bar_row(void)
{
    if (seq.running) return;

  if (configuration.sys.screen_saver_mode == ScreenSaver::DISABLED) return;
  if (oled_ring_screensaver_active) return;

  const uint32_t total_ms = (uint32_t)configuration.sys.screen_saver_start * 60000UL;
  if (total_ms == 0) return;

  uint32_t remaining = LCDML.SCREEN_getTimeRemaining();
  if (remaining > total_ms) remaining = total_ms;

  constexpr int BAR_Y = 31;
  constexpr int BAR_W = 128;
  const uint8_t fill_w = (uint8_t)(((uint64_t)remaining * BAR_W) / total_ms);

  oled_display.drawFastHLine(0, BAR_Y, BAR_W, SSD1306_BLACK);
  if (fill_w > 0) {
    oled_display.drawFastHLine(0, BAR_Y, fill_w, SSD1306_WHITE);
  }
}

static FLASHMEM void oled_render_left(bool inMainMenu)
{
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  oled_display.clearDisplay();
  oled_display.setTextSize(2);
  oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  if (inMainMenu) {
    switch (oled_info_mode) {
    case 0:
      oled_display.setCursor(0, 0);
      oled_display.print(F("CPU:"));
      oled_display.print(AudioProcessorUsage(), 1);
      oled_display.print(F("%"));
      oled_display.setCursor(0, 16);
      oled_display.print(F("PEAK:"));
      oled_display.print(AudioProcessorUsageMax(), 1);
      oled_display.print(F("%"));
      break;

    case 1:
      oled_display.setCursor(0, 0);
      oled_display.print(F("TEMP:"));
      oled_display.print(tempmonGetTemp(), 1);
      oled_display.print(F("C"));
      break;

    case 2:
      oled_display.setCursor(0, 0);
      oled_display.print(F("LONG PUSH:"));
      oled_display.setCursor(0, 16);
      oled_display.print(seq.running ? F("STOP SEQ") : F("START SEQ"));
      break;

    case 3: {
      uint32_t totalMB = (uint32_t)external_psram_size;
      oled_display.setCursor(0, 0);
      if (totalMB == 0) {
        oled_display.print(F("PSRAM:NONE"));
      } else {
        oled_display.print(F("PSRAM:"));
        oled_display.print(totalMB);
        oled_display.print(F("MB"));
      }
      oled_display.setCursor(0, 16);
      if (totalMB == 0) {
        // leave blank
      } else if (psram_free_bytes == 0) {
        oled_display.print(F("FULL"));
      } else {
        uint8_t freePercent = (uint8_t)((psram_free_bytes * 100) / (totalMB * 1048576));
        oled_display.print(F("FREE:"));
        if (freePercent < 10)  oled_display.print(F(" "));
        if (freePercent < 100) oled_display.print(F(" "));
        oled_display.print(freePercent);
        oled_display.print(F("%"));
      }
      break;
    }

    case 4:
      oled_display.setCursor(0, 0);
      oled_display.print(F("VOL:"));
      oled_display.print(configuration.sys.vol);
      oled_display.print(F("%"));
      break;

    case 5:
      print_mute_matrix();
      break;

    case 6:
      oled_display.setCursor(0, 0);
      oled_display.print(F("CLOCK:"));
      oled_display.setCursor(0, 16);
      if (seq.clock == 0)      oled_display.print(F("INTERN"));
      else if (seq.clock == 1) oled_display.print(F("MIDI SLAVE"));
      else                     oled_display.print(F("MIDI MASTER"));
      break;

    case 7:
      oled_display.setCursor(0, 0);
      oled_display.print(F("SIDECHAIN:"));
      oled_display.setCursor(0, 16);
      oled_display.print(sidechain_active ? F("ON ") : F("OFF"));
      break;

    case 8:
      oled_display.setCursor(0, 0);
      oled_display.print(F("MULTIBAND:"));
      oled_display.setCursor(0, 16);
      oled_display.print(multiband_active ? F("ON") : F("OFF"));
      break;
    }
  }
  else {
    // Non-main-menu page: reproduce exactly what helptext_impl() case 0
    // drew, using the same variable that case 0 saved into.
    oled_display.setCursor(0, 0);
    oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    oled_display.print(last_left_helptext);

    oled_display.setCursor(0, 16);
    oled_display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    if (seq.running)
      oled_display.print(F("STOP SEQ. "));
    else
      oled_display.print(F("START SEQ."));
    oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);


  }

  // Countdown bar last, on top of whatever the page drew.
  oled_draw_bar_row();

  mcp.write1(OLED_CS_L, LOW);
  mcp.write1(OLED_CS_R, HIGH);
  oled_display.display();
  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);

  oled_fb_owner = 0;

  SPI.endTransaction();
}

// ---------------------------------------------------------------------------
// oled_render_right
//
// Pure renderer for the right OLED. Same contract as oled_render_left.
// ---------------------------------------------------------------------------
static FLASHMEM void oled_render_right(bool inMainMenu)
{
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  oled_display.clearDisplay();
  oled_display.setTextSize(2);
  oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  if (inMainMenu) {
    switch (oled_info_mode) {
    case 0:
      oled_display.setCursor(0, 0);
      oled_display.print(F("SONG "));
      oled_display.print(configuration.sys.performance_number);
      oled_display.print(F(":"));
      oled_display.setCursor(0, 16);
      print_trunc(seq.name, 10);
      break;

    case 1:
      oled_display.setCursor(0, 0);
      if (sd_card_internal == 0) {
        oled_display.print(F("SD:NONE"));
      } else {
        print_sd_total(volume_internal, sd_card_internal > 0, F("INT: "));
      }
      oled_display.setCursor(0, 16);
#ifdef SECOND_SD
      if (sd_card_external == 0) {
        oled_display.print(F("NONE"));
      } else {
        print_sd_total(volume_external, sd_card_external > 0, F("EXT: "));
      }
#else
      oled_display.print(F("N/A"));
#endif
      break;

    case 2: print_dexed_right(0); break;
    case 3: print_dexed_right(1); break;
    case 4: print_dexed_right(2); break;
    case 5: print_dexed_right(3); break;

    case 6:
      oled_display.setCursor(0, 0);
      oled_display.print(seq.bpm);
      oled_display.print(F(" BPM"));
      break;

    case 7: {
      int freeMem = AUDIO_MEM - AudioMemoryUsage();
      oled_display.setCursor(0, 0);
      oled_display.print(F("MEM FREE:"));
      oled_display.setCursor(0, 16);
      oled_display.print(freeMem);
      oled_display.print(F(" KB"));
      break;
    }

    case 8:
      oled_display.setCursor(0, 0);
      oled_display.print(F("SMARTFILT."));
      oled_display.setCursor(0, 16);
      oled_display.print(seq.smartfilter ? F("ON") : F("OFF"));
      break;
    }
  }

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, LOW);
  oled_display.display();
  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);

  oled_fb_owner = 1;

  SPI.endTransaction();
}

// ---------------------------------------------------------------------------
// OLED_update   (formerly OLED_status)
//
// Master tick, called from the main loop. Rotates the info mode every
// 2.5 s and repaints the right panel. When the screensaver is disabled
// there is no 1 Hz left-panel refresh, so we also repaint the left panel
// here on every rotation to keep it in sync.
// ---------------------------------------------------------------------------
FLASHMEM void OLED_update(void)
{
#if defined MINI_ENCODER
  if (tapTempoActive) return;
#endif

  static const uint8_t max_modes = 9;

  if (OLEDTimer < 4500) return;
  OLEDTimer = 0;

  oled_info_mode++;
  if (oled_info_mode >= max_modes) oled_info_mode = 0;

  const bool inMainMenu = (LCDML.FUNC_getID() == 255);

  oled_render_right(inMainMenu);

  if (configuration.sys.screen_saver_mode == ScreenSaver::DISABLED) {
    oled_render_left(inMainMenu);
  }
}


void OLED_update_left_panel(void)
{
#if defined MINI_ENCODER
  if (tapTempoActive) return;
#endif

  if (configuration.sys.screen_saver_mode == ScreenSaver::DISABLED) return;
  if (oled_ring_screensaver_active) return;

  // Hard 1 Hz throttle. Every call to oled_render_left() clears the
  // shared framebuffer, draws the whole left page, and pushes it over
  // SPI - so this must never run more often than we actually want the
  // left panel to refresh, regardless of what other code has done to
  // the framebuffer in between.
  static uint32_t last_update_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - last_update_ms < 1000) return;
  last_update_ms = now_ms;

  oled_render_left(LCDML.FUNC_getID() == 255);
}

FLASHMEM void init_oled_displays()
{
  SPI.setMOSI(11);
  SPI.setMISO(12);
  SPI.setSCK(13);
  SPI.begin();
  // MCP23017 Port B
  // mcp.pinMode1(OLED_CS_L, OUTPUT);       // MCP GPB0  = OLED_CS_L    (OLED L = 0,96" 128x32px)
  // mcp.pinMode1(OLED_CS_R, OUTPUT);       // MCP GPB1  = OLED_CS_R    (OLED R = 0,96" 128x32px)

  mcp.write1(OLED_CS_L, LOW);
  mcp.write1(OLED_CS_R, LOW);

  // // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!oled_display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  oled_display.setTextSize(2);
  oled_display.cp437(true);
  oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  mcp.write1(OLED_CS_L, LOW);
  mcp.write1(OLED_CS_R, HIGH);
  oled_display.clearDisplay();
  oled_display.display();
  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, LOW);
  oled_display.clearDisplay();
  oled_display.display();
  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();
}
#endif

//=============================================================================
// Universal OLED + LED-Ring screensaver animation  (v12)
//   Defender ship + discrete laser shots + sparking thrusters
//   + Vector asteroids: irregular rotating polygons the ship destroys
//   + Smart targeting: ship chases asteroids vertically and only fires
//     when a shot would actually connect.
//   + Bigger asteroids (5..10 px radius).
//   + More asteroids (pool of 8), spawned primarily on the OLED panel
//     opposite to the ship's current half so the ship always has new
//     targets to fly towards.
//=============================================================================
#if defined OLED_DISPLAYS || defined LED_RINGS || defined LED_DEBUG

// Defined once in screensaver.cpp
extern bool oled_ring_screensaver_active;

// ---------------------------------------------------------------------------
// 20 twinkling stars (OLED-local, x < 128)
// ---------------------------------------------------------------------------
static const uint8_t PROGMEM ols_stars[][2] = {
    {  8,  4}, { 22, 18}, { 35,  7}, { 47, 22}, { 61, 12},
    { 74,  5}, { 82, 25}, { 93,  9}, {105, 21}, {118, 27},
    {125, 13}, { 59, 28}, { 21, 12}, { 88, 18}, { 41, 26},
    {110,  3}, { 67,  2}, { 14, 28}, { 30,  2}, { 96, 30},
};
#define OLS_STAR_COUNT (sizeof(ols_stars) / sizeof(ols_stars[0]))

// Gentle bob (±3 px)
static const int8_t ols_bob[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
#define OLS_BOB_LEN (sizeof(ols_bob) / sizeof(ols_bob[0]))

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool     ols_running    = false;
static float    ols_oled_pos   = 30.0f;
static int8_t   ols_oled_dir   =  1;

static uint32_t ols_last_tick  = 0;
static uint32_t ols_tick_count = 0;
static uint32_t ols_muzzle_tick = 0;
static uint16_t ols_next_shot_tick = 0;

// Smart-ship state
static float    ols_ship_y          = 15.0f;  // smoothed vertical ship position
static int8_t   ols_target_idx      = -1;     // currently targeted asteroid, or -1
static bool     ols_has_firing_lock = false;  // true when a shot would connect right now

// ---------------------------------------------------------------------------
// Laser shots
// ---------------------------------------------------------------------------
#define OLS_MAX_SHOTS 4
struct OLS_Shot {
    float    pos;      // 0..255 global X, position of the beam's *head*
    int8_t   dir;      // +1 or -1
    int16_t  y;        // y at the moment the shot was fired
    bool     active;
};
static OLS_Shot ols_shots[OLS_MAX_SHOTS];

// ---------------------------------------------------------------------------
// Thruster sparks (small pool, spawn behind the tail)
// ---------------------------------------------------------------------------
#define OLS_MAX_THRUST 8
struct OLS_Thrust {
    int16_t x;       // global 0..255
    int8_t  dx;      // drift per tick (-1 / 0 / +1)
    uint8_t life;    // 255..0
};
static OLS_Thrust ols_thrust[OLS_MAX_THRUST];
static uint8_t    ols_thrust_wr = 0;

// ---------------------------------------------------------------------------
// Vector asteroids
//   Irregular polygons with 7 vertices. Each asteroid has its own set of
//   radii so no two look alike. They drift in from any of the four screen
//   edges. Horizontal-motion asteroids bounce off the top/bottom edges;
//   vertical-motion asteroids cross straight through.
//   Size range: 5..10 px radius.
//   Pool: 8 concurrent asteroids.
// ---------------------------------------------------------------------------
#define OLS_MAX_ASTEROIDS   8
#define OLS_ASTEROID_VERTS  7

struct OLS_Asteroid {
    float    x;                               // global 0..255, centre
    float    y;                               // 0..31, centre
    float    vx;                              // horizontal speed
    float    vy;                              // vertical speed
    int16_t  rotation;                        // degrees 0..359
    int16_t  rot_speed;                       // degrees per tick, signed
    uint8_t  size;                            // base radius in pixels (5..10)
    uint8_t  radii[OLS_ASTEROID_VERTS];       // 140..255 mapped to 0.55..1.0 * size
    bool     active;
    bool     bounces_vertically;              // true for edge==left/right spawns
};
static OLS_Asteroid ols_asteroids[OLS_MAX_ASTEROIDS];

static uint16_t ols_next_asteroid_tick = 0;

// ---------------------------------------------------------------------------
// Explosions (short-lived bursts)
// ---------------------------------------------------------------------------
#define OLS_MAX_EXPLOSIONS 6
#define OLS_EXPLOSION_LIFE 12

struct OLS_Explosion {
    float    x;
    float    y;
    uint8_t  life;      // counts down to 0
    bool     active;
};
static OLS_Explosion ols_explosions[OLS_MAX_EXPLOSIONS];

// ---------------------------------------------------------------------------
// Ring bands
// ---------------------------------------------------------------------------
#define OLS_MAX_BANDS 6

struct OLS_Band {
    float    pos;
    float    speed;
    float    width;
    float    hue;
    float    hue_target;
    uint8_t  intensity;
    uint32_t life;
    uint32_t total_life;
    bool     active;
};

static OLS_Band ols_bands[2][OLS_MAX_BANDS];

static uint32_t ols_rng = 0xA5A5A5A5u;
static inline uint8_t ols_rand8() {
    ols_rng = ols_rng * 1664525u + 1013904223u;
    return (uint8_t)(ols_rng >> 24);
}

static void ols_band_respawn(OLS_Band& b, bool firstTime)
{
    b.pos        = (float)(ols_rand8() % 24);
    b.speed      = ((ols_rand8() & 1) ? +1.0f : -1.0f)
                   * (0.144f + (float)(ols_rand8() % 9) * 0.0288f);
    b.width      = 8.0f + (float)(ols_rand8() % 5);
    b.hue_target = (float)(ols_rand8() * 1.4f);
    if (firstTime) b.hue = b.hue_target;
    b.intensity  = 50 + (ols_rand8() % 30);
    b.total_life = 208 + (uint32_t)(ols_rand8() & 0x7F) * 2;
    b.life       = b.total_life;
    b.active     = true;
}

// ==========================================================================
// Asteroid / explosion helpers
// ==========================================================================

static void ols_asteroid_spawn()
{
    for (uint8_t i = 0; i < OLS_MAX_ASTEROIDS; i++) {
        if (ols_asteroids[i].active) continue;

        // Size 5..10 pixel radius
        const uint8_t sz = 5 + (ols_rand8() % 6);
        ols_asteroids[i].size = sz;

        // Which half of the virtual world (0..255) is the ship in?
        // The left OLED covers 0..127, the right OLED covers 128..255.
        const bool ship_on_left = (ols_oled_pos < 128.0f);

        // Edge selection.
        //   0 = left edge (moving right)
        //   1 = right edge (moving left)
        //   2 = top edge (moving down)
        //   3 = bottom edge (moving up)
        //
        // Bias toward the opposite panel from the ship so most new
        // asteroids appear on the screen the ship is NOT currently on.
        // This gives the ship a stream of fresh targets ahead of it and
        // prevents asteroids from popping in right on top of it.
        uint8_t edge;
        const uint8_t r = ols_rand8();

        if (r < 140) {
            // ~55 % - opposite horizontal edge
            edge = ship_on_left ? 1 : 0;
        } else if (r < 180) {
            // ~15 % - same-side horizontal edge (occasional, keeps
            //         the ship alert and adds variety)
            edge = ship_on_left ? 0 : 1;
        } else if (r < 220) {
            // ~15 % - top edge
            edge = 2;
        } else {
            // ~15 % - bottom edge
            edge = 3;
        }

        // Y-range inside the visible band for horizontal spawns,
        // so the whole asteroid sits on-screen from the start.
        const uint8_t y_room = (uint8_t)(30 - 2 * sz);
        const float y_inside = (float)sz + 1.0f
                             + (float)(ols_rand8() % (y_room > 0 ? y_room : 1));

        // Horizontal-motion speed magnitude
        const float speed = 0.18f + (float)(ols_rand8() % 8) * 0.055f;

        // A little vertical drift for horizontal spawns, and horizontal
        // drift for vertical spawns, so nothing moves in a straight line.
        const float vdrift = ((ols_rand8() & 1) ? +1.0f : -1.0f)
                             * (0.05f + (float)(ols_rand8() % 4) * 0.03f);

        if (edge == 0 || edge == 1) {
            // ---- Horizontal: bounce vertically so it stays on-screen longer
            ols_asteroids[i].bounces_vertically = true;
            ols_asteroids[i].vy = vdrift;
            ols_asteroids[i].y  = y_inside;

            if (edge == 1) {
                // From the right, moving left
                ols_asteroids[i].x  = 256.0f + (float)sz + (float)(ols_rand8() % 24);
                ols_asteroids[i].vx = -speed;
            } else {
                // From the left, moving right
                ols_asteroids[i].x  = -20.0f - (float)sz - (float)(ols_rand8() % 24);
                ols_asteroids[i].vx = +speed;
            }
        } else {
            // ---- Vertical: do NOT bounce vertically, just cross the screen
            ols_asteroids[i].bounces_vertically = false;

            // Small horizontal drift so they still look alive
            ols_asteroids[i].vx = ((ols_rand8() & 1) ? +1.0f : -1.0f)
                                  * (0.04f + (float)(ols_rand8() % 4) * 0.025f);

            // X spawn biased toward the opposite half from the ship.
            // ~75 % of vertical spawns land on the opposite panel,
            // ~25 % anywhere (for variety).
            const bool opposite_half = (ols_rand8() < 192);

            if (opposite_half) {
                if (ship_on_left) {
                    // Ship on left panel -> spawn right panel: 128..247
                    ols_asteroids[i].x = 128.0f + (float)(ols_rand8() % 120);
                } else {
                    // Ship on right panel -> spawn left panel: 8..127
                    ols_asteroids[i].x = 8.0f + (float)(ols_rand8() % 120);
                }
            } else {
                ols_asteroids[i].x = 8.0f + (float)(ols_rand8() % 240);
            }

            if (edge == 2) {
                // From the top, moving down
                ols_asteroids[i].y  = -20.0f - (float)sz - (float)(ols_rand8() % 16);
                ols_asteroids[i].vy = +speed * 0.85f;   // slight speed bias
            } else {
                // From the bottom, moving up
                ols_asteroids[i].y  = 32.0f + (float)sz + (float)(ols_rand8() % 16);
                ols_asteroids[i].vy = -speed * 0.85f;
            }
        }

        // Initial rotation and rotation speed (-8..+8 deg/tick)
        ols_asteroids[i].rotation  = (int16_t)((uint16_t)ols_rand8() * 360 / 256);
        ols_asteroids[i].rot_speed = (int16_t)((ols_rand8() % 17)) - 8;

        // Give each asteroid its own irregular profile
        for (int v = 0; v < OLS_ASTEROID_VERTS; v++) {
            ols_asteroids[i].radii[v] = 140 + (ols_rand8() % 116);   // 140..255
        }

        ols_asteroids[i].active = true;
        return;
    }
    // No free slot - silently skip
}

static void ols_explosion_spawn(float x, float y)
{
    for (uint8_t i = 0; i < OLS_MAX_EXPLOSIONS; i++) {
        if (ols_explosions[i].active) continue;
        ols_explosions[i].x      = x;
        ols_explosions[i].y      = y;
        ols_explosions[i].life   = OLS_EXPLOSION_LIFE;
        ols_explosions[i].active = true;
        return;
    }
}

static void ols_asteroids_update()
{
    for (uint8_t i = 0; i < OLS_MAX_ASTEROIDS; i++) {
        if (!ols_asteroids[i].active) continue;

        ols_asteroids[i].x += ols_asteroids[i].vx;
        ols_asteroids[i].y += ols_asteroids[i].vy;

        // Bounce off top/bottom edges only for asteroids that came in
        // horizontally. Vertical-motion asteroids just cross through.
        if (ols_asteroids[i].bounces_vertically) {
            const float min_y = (float)ols_asteroids[i].size + 1.0f;
            const float max_y = 30.0f - (float)ols_asteroids[i].size;

            if (ols_asteroids[i].y < min_y) {
                ols_asteroids[i].y  = min_y;
                ols_asteroids[i].vy = -ols_asteroids[i].vy;
            } else if (ols_asteroids[i].y > max_y) {
                ols_asteroids[i].y  = max_y;
                ols_asteroids[i].vy = -ols_asteroids[i].vy;
            }
        }

        // Rotate
        ols_asteroids[i].rotation += ols_asteroids[i].rot_speed;
        if (ols_asteroids[i].rotation < 0)    ols_asteroids[i].rotation += 360;
        if (ols_asteroids[i].rotation >= 360) ols_asteroids[i].rotation -= 360;

        // Despawn once fully off-screen on any edge.
        // The virtual world is 256 wide (two 128-px OLEDs) and 32 tall.
        const float sz = (float)ols_asteroids[i].size;
        if (ols_asteroids[i].x < -20.0f - sz ||
            ols_asteroids[i].x > 276.0f + sz ||
            ols_asteroids[i].y < -20.0f - sz ||
            ols_asteroids[i].y >  52.0f + sz) {
            ols_asteroids[i].active = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Smart targeting
//
// Returns the index of the best target asteroid for the ship, or -1 if no
// asteroid is worth aiming at right now. Considers only asteroids ahead of
// the ship in its current facing direction (with a small margin), so the
// ship doesn't waste time turning around to chase something behind it.
//
// Scoring: larger asteroids are preferred, close X is preferred, and
// well-aligned Y is heavily preferred.
// ---------------------------------------------------------------------------
static int8_t ols_choose_target(int8_t facing_dir)
{
    int8_t best = -1;
    float  best_score = -1e9f;

    for (uint8_t i = 0; i < OLS_MAX_ASTEROIDS; i++) {
        if (!ols_asteroids[i].active) continue;

        const float dx = ols_asteroids[i].x - ols_oled_pos;
        // Only consider asteroids ahead (or nearly ahead) of us.
        if (facing_dir > 0 && dx < -8.0f) continue;
        if (facing_dir < 0 && dx >  8.0f) continue;

        const float dy = fabsf(ols_asteroids[i].y - ols_ship_y);

        // Normalise by rough screen extents so the weights are meaningful.
        const float dx_norm   = fabsf(dx) / 128.0f;
        const float dy_norm   = dy        / 16.0f;
        // Size spans 5..10, so normalise by 10 to keep the weight
        // in roughly the same "0..1" range it had originally.
        const float size_bonus = (float)ols_asteroids[i].size / 10.0f;

        // Y alignment is the most important term; X distance is secondary.
        const float score = size_bonus - dx_norm * 0.7f - dy_norm * 1.8f;

        if (score > best_score) {
            best_score = score;
            best = (int8_t)i;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Fire-solution test.
// Returns true if a shot spawned at the given y (and going in facing_dir)
// would intersect at least one active asteroid before leaving the screen.
// Uses the same Y tolerance as the real collision code so the ship doesn't
// fire at asteroids the beam would narrowly miss.
// ---------------------------------------------------------------------------
static bool ols_shot_would_hit(int ship_y, int8_t facing_dir)
{
    for (uint8_t i = 0; i < OLS_MAX_ASTEROIDS; i++) {
        if (!ols_asteroids[i].active) continue;

        const float dx = ols_asteroids[i].x - ols_oled_pos;

        // Asteroid must be clearly ahead of the ship in the firing direction.
        if (facing_dir > 0 && dx < 8.0f) continue;
        if (facing_dir < 0 && dx > -8.0f) continue;

        // Y overlap: same tolerance used by the actual collision check.
        if (fabsf(ols_asteroids[i].y - (float)ship_y)
            <= (float)ols_asteroids[i].size) {
            return true;
        }
    }
    return false;
}

// Check every active laser against every asteroid
static void ols_check_laser_asteroid_hits()
{
    for (uint8_t si = 0; si < OLS_MAX_SHOTS; si++) {
        if (!ols_shots[si].active) continue;

        const float head = ols_shots[si].pos;
        const int8_t sdir = ols_shots[si].dir;
        const float shot_y = (float)ols_shots[si].y;

        // Beam extent in virtual space
        constexpr float SHOT_LEN = 6.0f;
        const float beam_a = (sdir > 0) ? (head - SHOT_LEN) : head;
        const float beam_b = (sdir > 0) ? head              : (head + SHOT_LEN);

        for (uint8_t ai = 0; ai < OLS_MAX_ASTEROIDS; ai++) {
            if (!ols_asteroids[ai].active) continue;

            const float ax = ols_asteroids[ai].x;
            const float ay = ols_asteroids[ai].y;
            const float r  = (float)ols_asteroids[ai].size;

            // Simple AABB-style test against the asteroid's bounding circle
            if (fabsf(ay - shot_y) <= r &&
                ax + r >= beam_a &&
                ax - r <= beam_b) {

                ols_explosion_spawn(ax, ay);
                ols_asteroids[ai].active = false;
                ols_shots[si].active     = false;   // consume the shot
                break;
            }
        }
    }
}

static void ols_explosions_update()
{
    for (uint8_t i = 0; i < OLS_MAX_EXPLOSIONS; i++) {
        if (!ols_explosions[i].active) continue;
        if (ols_explosions[i].life > 0) {
            ols_explosions[i].life--;
        } else {
            ols_explosions[i].active = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering helpers (called with the framebuffer of one OLED already active)
// ---------------------------------------------------------------------------
#if defined OLED_DISPLAYS

static void ols_asteroid_draw_one(uint8_t oled)
{
    const int x_off = oled * 128;

    for (uint8_t i = 0; i < OLS_MAX_ASTEROIDS; i++) {
        if (!ols_asteroids[i].active) continue;

        const int cx = (int)ols_asteroids[i].x - x_off;
        const int cy = (int)ols_asteroids[i].y;
        const int sz = ols_asteroids[i].size;

        // Skip if entirely outside this OLED
        if (cx + sz < 0 || cx - sz >= 128) continue;

        // Compute polygon vertices once per asteroid
        int px[OLS_ASTEROID_VERTS];
        int py[OLS_ASTEROID_VERTS];

        for (int v = 0; v < OLS_ASTEROID_VERTS; v++) {
            const float angle_deg = (float)ols_asteroids[i].rotation
                                    + (float)v * 360.0f / (float)OLS_ASTEROID_VERTS;
            const float angle_rad = angle_deg * (float)M_PI / 180.0f;
            const float rr = ((float)ols_asteroids[i].radii[v] / 255.0f)
                             * (float)sz;
            px[v] = cx + (int)(cosf(angle_rad) * rr);
            py[v] = cy + (int)(sinf(angle_rad) * rr);
        }

        // Draw closed polygon
        for (int v = 0; v < OLS_ASTEROID_VERTS; v++) {
            const int n = (v + 1) % OLS_ASTEROID_VERTS;
            oled_display.drawLine(px[v], py[v], px[n], py[n], SSD1306_WHITE);
        }

        // Optional: a single internal dot to hint at solidity
        if (cx >= 0 && cx < 128 && cy >= 0 && cy < 32) {
            oled_display.drawPixel(cx, cy, SSD1306_WHITE);
        }
    }
}

static void ols_explosion_draw_one(uint8_t oled)
{
    const int x_off = oled * 128;

    for (uint8_t i = 0; i < OLS_MAX_EXPLOSIONS; i++) {
        if (!ols_explosions[i].active) continue;

        const int cx = (int)ols_explosions[i].x - x_off;
        const int cy = (int)ols_explosions[i].y;

        if (cx < -10 || cx > 138) continue;

        const uint8_t age = OLS_EXPLOSION_LIFE - ols_explosions[i].life;
        int radius = 1 + (age / 3);
        if (radius > 5) radius = 5;

        // 8-point star burst
        for (int a = 0; a < 8; a++) {
            const float ang = a * (float)M_PI / 4.0f;
            const int px = cx + (int)(cosf(ang) * (float)radius);
            const int py = cy + (int)(sinf(ang) * (float)radius);
            if (px >= 0 && px < 128 && py >= 0 && py < 32) {
                oled_display.drawPixel(px, py, SSD1306_WHITE);
            }
        }

        // Bright center for the first few frames
        if (age < 4 && cx >= 0 && cx < 128 && cy >= 0 && cy < 32) {
            oled_display.drawPixel(cx, cy, SSD1306_WHITE);
        }
    }
}

#endif  // OLED_DISPLAYS

// ==========================================================================
FLASHMEM void oled_ring_screensaver_start(void)
{
    ols_running        = true;
    ols_oled_pos       = 30.0f;
    ols_oled_dir       = 1;
    ols_last_tick      = 0;
    ols_tick_count     = 0;
    ols_muzzle_tick    = 0;
    ols_next_shot_tick = 0;
    ols_thrust_wr      = 0;

    // Smart-ship state
    ols_ship_y          = 15.0f;
    ols_target_idx      = -1;
    ols_has_firing_lock = false;

    // Clear any leftover asteroid/explosion state
    memset(ols_asteroids,  0, sizeof(ols_asteroids));
    memset(ols_explosions, 0, sizeof(ols_explosions));
    ols_next_asteroid_tick = 40;   // first asteroid after ~1.6 s

    // === Reseed the LCG so every screensaver run is different. ===
    // Mix millis() and micros() together - they advance independently and
    // together give enough entropy for a fresh sequence on each start.
    ols_rng = ((uint32_t)millis() * 2654435761u)
              ^ (uint32_t)micros()
              ^ 0xA5A5A5A5u;
    if (ols_rng == 0) ols_rng = 0xDEADBEEFu;   // avoid a degenerate zero seed
    // Burn a few iterations to diffuse low-entropy low bits from millis()/micros()
    for (uint8_t i = 0; i < 4; i++) (void)ols_rand8();
    // =============================================================

    for (uint8_t ring = 0; ring < 2; ring++) {
        const uint8_t cnt = 4 + (ols_rand8() & 1);   // 4 or 5 bands
        for (uint8_t bi = 0; bi < OLS_MAX_BANDS; bi++) {
            if (bi < cnt) {
                ols_band_respawn(ols_bands[ring][bi], true);
            } else {
                ols_bands[ring][bi].active = false;
                ols_bands[ring][bi].life   = 0;
            }
        }
    }

    memset(ols_thrust, 0, sizeof(ols_thrust));
    memset(ols_shots,  0, sizeof(ols_shots));

    oled_ring_screensaver_active = true;

#if defined OLED_DISPLAYS
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    mcp.write1(OLED_CS_L, LOW);  mcp.write1(OLED_CS_R, HIGH);
    oled_display.clearDisplay(); oled_display.display();
    mcp.write1(OLED_CS_L, HIGH); mcp.write1(OLED_CS_R, LOW);
    oled_display.clearDisplay(); oled_display.display();
    mcp.write1(OLED_CS_L, HIGH); mcp.write1(OLED_CS_R, HIGH);
    SPI.endTransaction();
#endif

#if defined LED_RINGS || defined LED_DEBUG
    for (uint8_t i = 0; i < 24; i++) {
        setLEDRing(0, i, 0, 0, 0);
        setLEDRing(1, i, 0, 0, 0);
    }
#endif
}

// ---------------------------------------------------------------------------
// Defender ship
// ---------------------------------------------------------------------------
#if defined OLED_DISPLAYS
static void draw_defender_ship(int cx, int cy, int dir)
{
    // Body
    for (int x = -7; x <= 5; x++) {
        int sx = cx + dir * x;
        if (sx < 0 || sx >= 128) continue;
        for (int y = -1; y <= 1; y++) {
            int sy = cy + y;
            if (sy >= 0 && sy < 32)
                oled_display.drawPixel(sx, sy, SSD1306_WHITE);
        }
    }
    // Nose
    for (int i = 0; i <= 6; i++) {
        int x = cx + dir * (5 + i);
        if (x < 0 || x >= 128) continue;
        int half = (i < 4) ? 1 : 0;
        for (int y = -half; y <= half; y++) {
            int sy = cy + y;
            if (sy >= 0 && sy < 32)
                oled_display.drawPixel(x, sy, SSD1306_WHITE);
        }
    }
    // Cockpit
    for (int x = -3; x <= 2; x++) {
        int sx = cx + dir * x;
        if (sx < 0 || sx >= 128) continue;
        int h = (x == -3 || x == 2) ? 1 : 2;
        int y_top = cy - 1 - h;
        for (int y = y_top; y <= cy - 2; y++) {
            if (y >= 0 && y < 32)
                oled_display.drawPixel(sx, y, SSD1306_WHITE);
        }
    }
    // Upper fin
    for (int i = 0; i <= 6; i++) {
        int x = cx + dir * (-4 - i);
        if (x < 0 || x >= 128) continue;
        int rise = (i * 3) / 6;
        int y_top = cy - 1 - rise;
        for (int y = y_top; y <= cy - 1; y++) {
            if (y >= 0 && y < 32)
                oled_display.drawPixel(x, y, SSD1306_WHITE);
        }
    }
    // Lower fin
    for (int i = 0; i <= 6; i++) {
        int x = cx + dir * (-4 - i);
        if (x < 0 || x >= 128) continue;
        int fall = (i * 3) / 6;
        int y_bot = cy + 1 + fall;
        for (int y = cy + 1; y <= y_bot; y++) {
            if (y >= 0 && y < 32)
                oled_display.drawPixel(x, y, SSD1306_WHITE);
        }
    }
    // Exhaust port
    int ex = cx + dir * (-8);
    if (ex >= 0 && ex < 128 && cy >= 0 && cy < 32)
        oled_display.drawPixel(ex, cy, SSD1306_BLACK);
}
#endif  // OLED_DISPLAYS


// ==========================================================================
FLASHMEM void oled_ring_screensaver_stop(void)
{
    ols_running = false;
    oled_ring_screensaver_active = false;

#if defined OLED_DISPLAYS
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    mcp.write1(OLED_CS_L, LOW);  mcp.write1(OLED_CS_R, HIGH);
    oled_display.clearDisplay(); oled_display.display();
    mcp.write1(OLED_CS_L, HIGH); mcp.write1(OLED_CS_R, LOW);
    oled_display.clearDisplay(); oled_display.display();
    mcp.write1(OLED_CS_L, HIGH); mcp.write1(OLED_CS_R, HIGH);
    SPI.endTransaction();
#endif

#if defined LED_RINGS || defined LED_DEBUG
    for (uint8_t i = 0; i < 24; i++) {
        setLEDRing(0, i, 0, 0, 0);
        setLEDRing(1, i, 0, 0, 0);
    }
#endif
}

// ==========================================================================
FLASHMEM void oled_ring_screensaver_update(void)
{
    if (!ols_running) return;

    const uint32_t now = millis();
    if (now - ols_last_tick < 40) return;    // ~25 Hz
    ols_last_tick = now;
    ols_tick_count++;

    // ======================================================================
    // Ship horizontal motion
    // ======================================================================
    constexpr float SHIP_SPEED = 1.6f;
    constexpr float SHOT_SPEED = 12.0f;
    constexpr float SHOT_LEN   = 6.0f;
    constexpr float SHIP_HALF_WIDTH = 35.0f;

    // We slow down when we have a firing solution so the ship stays
    // aligned with the target asteroid for a couple of extra ticks.
    const float ship_speed = ols_has_firing_lock ? SHIP_SPEED * 0.65f : SHIP_SPEED;

    ols_oled_pos += ols_oled_dir * ship_speed;
    if (ols_oled_pos >= 255.0f + SHIP_HALF_WIDTH) {
        ols_oled_pos = 255.0f + SHIP_HALF_WIDTH;
        ols_oled_dir = -1;
    }
    if (ols_oled_pos <=   0.0f - SHIP_HALF_WIDTH) {
        ols_oled_pos =   0.0f - SHIP_HALF_WIDTH;
        ols_oled_dir =  1;
    }

    // ======================================================================
    // Smart targeting: pick a target, chase its y, refresh firing lock.
    // ======================================================================
    ols_target_idx = ols_choose_target(ols_oled_dir);

    // Desired vertical position: the target's y, or mid-screen if nothing
    // is worth chasing. Clamped so the ship's fins stay on-screen
    // (the ship spans roughly y_c-4 .. y_c+4).
    float desired_y = 15.0f;
    if (ols_target_idx >= 0) {
        desired_y = ols_asteroids[ols_target_idx].y;
    }
    desired_y = constrain(desired_y, 6.0f, 26.0f);

    // Exponential approach toward the target y. The factor is small enough
    // that motion looks deliberate, not snappy.
    ols_ship_y += (desired_y - ols_ship_y) * 0.12f;

    // Small decorative bob on top of the tracked y so the ship still
    // looks alive without wandering off target.
    const int bob = ols_bob[(ols_tick_count >> 3) % OLS_BOB_LEN];
    int y_c = (int)(ols_ship_y + bob * 0.4f);
    y_c = constrain(y_c, 4, 27);

    // Refresh the firing-lock flag for this tick.
    ols_has_firing_lock = ols_shot_would_hit(y_c, ols_oled_dir);

    // ======================================================================
    // LED ring bands: advance position, crossfade hue, respawn on death.
    // ======================================================================
    for (uint8_t ring = 0; ring < 2; ring++) {
        for (uint8_t bi = 0; bi < OLS_MAX_BANDS; bi++) {
            OLS_Band& b = ols_bands[ring][bi];
            if (!b.active) continue;

            // Constant-speed drift, wrapping around the 24-LED ring.
            b.pos += b.speed;
            if (b.pos <  0.0f) b.pos += 24.0f;
            if (b.pos >= 24.0f) b.pos -= 24.0f;

            // Smooth hue interpolation (shortest path around the wheel).
            float dh = b.hue_target - b.hue;
            while (dh >  180.0f) dh -= 360.0f;
            while (dh < -180.0f) dh += 360.0f;
            b.hue += dh * 0.03f;
            if (b.hue <   0.0f) b.hue += 360.0f;
            if (b.hue >= 360.0f) b.hue -= 360.0f;

            // Age. On death, respawn in place without resetting the hue,
            // so the colour crossfades seamlessly across the lifetime
            // boundary. The life envelope hides the direction change.
            if (b.life > 0) b.life--;
            if (b.life == 0) ols_band_respawn(b, false);
        }
    }

    // ======================================================================
    // Advance active shots
    // ======================================================================
    for (uint8_t i = 0; i < OLS_MAX_SHOTS; i++) {
        if (!ols_shots[i].active) continue;
        ols_shots[i].pos += ols_shots[i].dir * SHOT_SPEED;
        if (ols_shots[i].pos < -SHOT_LEN || ols_shots[i].pos > 255.0f + SHOT_LEN)
            ols_shots[i].active = false;
    }

    // ======================================================================
    // Spawn thruster sparks behind the tail (every 2 ticks)
    // ======================================================================
    if ((ols_tick_count & 1) == 0) {
        OLS_Thrust& t = ols_thrust[ols_thrust_wr];
        ols_thrust_wr = (ols_thrust_wr + 1) % OLS_MAX_THRUST;

        t.x    = (int16_t)(ols_oled_pos - ols_oled_dir * 10);
        t.dx   = (int8_t)((ols_rand8() % 3) - 1);
        t.life = 220;
    }

    for (uint8_t i = 0; i < OLS_MAX_THRUST; i++) {
        if (ols_thrust[i].life == 0) continue;
        ols_thrust[i].x += ols_thrust[i].dx;
        ols_thrust[i].life = (ols_thrust[i].life > 32) ? ols_thrust[i].life - 32 : 0;
    }

    // ======================================================================
    // Spawn a new asteroid at random intervals.
    // With a larger pool (8 slots) and a shorter interval, the sky stays
    // busy and the ship always has a fresh target to turn towards.
    // ======================================================================
    if (ols_tick_count >= ols_next_asteroid_tick) {
        // Spawn up to 2 asteroids back-to-back when slots are free, so
        // the population refills quickly after a burst of kills.
        ols_asteroid_spawn();
        ols_asteroid_spawn();
        ols_next_asteroid_tick = ols_tick_count + 20 + (ols_rand8() % 30);
    }
    ols_asteroids_update();
    ols_explosions_update();

    // ======================================================================
    // Smart firing: fire only when the shot would actually connect.
    // If we're not aligned, retry soon (2 ticks). If we just fired,
    // wait a short while before firing again.
    // ======================================================================
    if (ols_tick_count >= ols_next_shot_tick) {
        if (ols_shot_would_hit(y_c, ols_oled_dir)) {
            for (uint8_t i = 0; i < OLS_MAX_SHOTS; i++) {
                if (!ols_shots[i].active) {
                    ols_shots[i].pos    = ols_oled_pos + ols_oled_dir * 11.0f;
                    ols_shots[i].dir    = ols_oled_dir;
                    ols_shots[i].y      = (int16_t)y_c;
                    ols_shots[i].active = true;
                    ols_muzzle_tick     = ols_tick_count;
                    break;
                }
            }
            // Fired: brief cooldown so we don't machine-gun.
            ols_next_shot_tick = ols_tick_count + 6 + (ols_rand8() % 5);
        } else {
            // Not aligned yet: keep checking frequently.
            ols_next_shot_tick = ols_tick_count + 2;
        }
    }

    // ======================================================================
    // Laser-vs-asteroid collisions (before drawing)
    // ======================================================================
    ols_check_laser_asteroid_hits();

    // ======================================================================
    // OLED rendering
    // ======================================================================
#if defined OLED_DISPLAYS
    {
        constexpr int OLED_W = 128;
        constexpr int OLED_H = 32;

        const bool muzzle_flash =
            (ols_tick_count >= ols_muzzle_tick &&
             ols_tick_count <  ols_muzzle_tick + 2);

        SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

        for (uint8_t oled = 0; oled < 2; oled++) {
            if (oled == 0) { mcp.write1(OLED_CS_L, LOW);  mcp.write1(OLED_CS_R, HIGH); }
            else           { mcp.write1(OLED_CS_L, HIGH); mcp.write1(OLED_CS_R, LOW);  }

            oled_display.clearDisplay();

            // ---------- 20 twinkling stars, mixed speeds ----------
            const uint8_t phase_fast = (uint8_t)((ols_tick_count >> 4) + oled * 29) & 0x7F;
            const uint8_t phase_slow = (uint8_t)((ols_tick_count >> 5) + oled * 41) & 0x7F;

            for (uint8_t s = 0; s < OLS_STAR_COUNT; s++) {
                uint8_t sx = pgm_read_byte(&ols_stars[s][0]);
                uint8_t sy = pgm_read_byte(&ols_stars[s][1]);
                if (sx >= OLED_W) continue;

                uint8_t ph = ((s & 1) ? phase_fast : phase_slow) + s * 11;

                if ((ph & 0x3F) < 6) {
                    oled_display.drawPixel(sx, sy, SSD1306_WHITE);
                } else if ((ph & 0x3F) == 6) {
                    // flare
                    oled_display.drawPixel(sx, sy, SSD1306_WHITE);
                    if (sx > 0)          oled_display.drawPixel(sx - 1, sy, SSD1306_WHITE);
                    if (sx < OLED_W - 1) oled_display.drawPixel(sx + 1, sy, SSD1306_WHITE);
                    if (sy > 0)          oled_display.drawPixel(sx, sy - 1, SSD1306_WHITE);
                    if (sy < OLED_H - 1) oled_display.drawPixel(sx, sy + 1, SSD1306_WHITE);
                }
            }

            // ---------- Vector asteroids (behind the ship) ----------
            ols_asteroid_draw_one(oled);

            // ---------- thruster sparks behind the tail ----------
            for (uint8_t i = 0; i < OLS_MAX_THRUST; i++) {
                if (ols_thrust[i].life == 0) continue;
                int tx = ols_thrust[i].x - oled * 128;
                if (tx < 0 || tx >= OLED_W) continue;

                // central spark
                oled_display.drawPixel(tx, y_c, SSD1306_WHITE);

                // sparkle: use the thruster's own ageing as a per-particle seed
                uint8_t seed = (uint8_t)(ols_thrust[i].life + (i * 37));
                if ((seed & 3) == 0) {
                    if (tx + 1 < OLED_W) oled_display.drawPixel(tx + 1, y_c - 1, SSD1306_WHITE);
                    if (tx - 1 >= 0)     oled_display.drawPixel(tx - 1, y_c + 1, SSD1306_WHITE);
                } else if ((seed & 7) == 1) {
                    if (tx + 1 < OLED_W) oled_display.drawPixel(tx + 1, y_c,     SSD1306_WHITE);
                    if (tx - 1 >= 0)     oled_display.drawPixel(tx - 1, y_c,     SSD1306_WHITE);
                } else if ((seed & 7) == 3) {
                    if (tx < OLED_W && y_c - 1 >= 0) oled_display.drawPixel(tx, y_c - 1, SSD1306_WHITE);
                    if (tx < OLED_W && y_c + 1 < OLED_H) oled_display.drawPixel(tx, y_c + 1, SSD1306_WHITE);
                }
            }

            // ---------- laser shots (short, discrete, thin) ----------
            for (uint8_t si = 0; si < OLS_MAX_SHOTS; si++) {
                if (!ols_shots[si].active) continue;

                const float head = ols_shots[si].pos;
                const int8_t sdir = ols_shots[si].dir;
                const int shot_y = (int)ols_shots[si].y;

                // beam = [head - sdir*LEN, head]
                float beam_a = (sdir > 0) ? (head - SHOT_LEN) : head;
                float beam_b = (sdir > 0) ? head              : (head + SHOT_LEN);

                const float oled_min = (oled == 0) ?   0.0f : 128.0f;
                const float oled_max = (oled == 0) ? 127.0f : 255.0f;

                float ls = fmaxf(beam_a, oled_min);
                float le = fminf(beam_b, oled_max);
                if (ls > le) continue;

                int x0 = (int)ls - oled * 128;
                int x1 = (int)le - oled * 128;
                if (x0 < 0)          x0 = 0;
                if (x1 > OLED_W - 1) x1 = OLED_W - 1;
                if (x0 > x1) continue;

                // Thin 1-pixel-tall beam
                oled_display.drawFastHLine(x0, shot_y, x1 - x0 + 1, SSD1306_WHITE);

                // Small 1-pixel tip marker (one row above the beam) so the
                // head still reads as a "front edge" without bulking up
                // the beam into a fat bar.
                int hx = (int)head - oled * 128;
                if (hx >= 0 && hx < OLED_W && shot_y - 1 >= 0) {
                    oled_display.drawPixel(hx, shot_y - 1, SSD1306_WHITE);
                }
            }

            // ---------- explosions ----------
            ols_explosion_draw_one(oled);

            // ---------- ship ----------
            const int ship_cx = (int)ols_oled_pos - oled * OLED_W;
            draw_defender_ship(ship_cx, y_c, (int)ols_oled_dir);

            // ---------- muzzle flash (single-pixel spark) ----------
            if (muzzle_flash) {
                int fx = ship_cx + ols_oled_dir * 13;
                if (fx >= 0 && fx < OLED_W) {
                    oled_display.drawPixel(fx, y_c, SSD1306_WHITE);
                }
            }

            oled_display.display();
        }

        mcp.write1(OLED_CS_L, HIGH); mcp.write1(OLED_CS_R, HIGH);
        SPI.endTransaction();
    }
#endif  // OLED_DISPLAYS

    // ======================================================================
    // LED rings rendering
    // ======================================================================
#if defined LED_RINGS || defined LED_DEBUG
    {
        uint8_t rr[48], rg[48], rb[48];
        memset(rr, 0, sizeof(rr));
        memset(rg, 0, sizeof(rg));
        memset(rb, 0, sizeof(rb));

        auto hue_to_rgb = [](float hue, uint8_t& r, uint8_t& g, uint8_t& b) {
            int32_t h = (int32_t)hue % 360;
            if (h < 0) h += 360;
            uint16_t c = ColorHSV((uint16_t)h, 255, 255);
            uint8_t r5 = (c >> 11) & 0x1F; r = (r5 << 3) | (r5 >> 2);
            uint8_t g6 = (c >>  5) & 0x3F; g = (g6 << 2) | (g6 >> 4);
            uint8_t b5 =  c        & 0x1F; b = (b5 << 3) | (b5 >> 2);
        };

        for (uint8_t ring = 0; ring < 2; ring++) {
            const int base = ring ? 24 : 0;

            // Precompute per-band colour and effective peak brightness
            // once per tick, so hue_to_rgb and the life envelope run
            // at most OLS_MAX_BANDS * 2 times per update, not per LED.
            uint8_t band_r[OLS_MAX_BANDS];
            uint8_t band_g[OLS_MAX_BANDS];
            uint8_t band_b[OLS_MAX_BANDS];
            float   band_peak[OLS_MAX_BANDS];
            float   band_half[OLS_MAX_BANDS];

            for (uint8_t bi = 0; bi < OLS_MAX_BANDS; bi++) {
                const OLS_Band& b = ols_bands[ring][bi];

                if (!b.active || b.life == 0) {
                    band_peak[bi] = 0.0f;
                    band_half[bi] = 0.0f;
                    band_r[bi] = band_g[bi] = band_b[bi] = 0;
                    continue;
                }

                // Life envelope: 0 at spawn, 1 mid-life, 0 at death.
                const float phase    = 1.0f - (float)b.life / (float)b.total_life;
                const float life_env = 0.5f * (1.0f - cosf(phase * 2.0f * (float)M_PI));

                band_peak[bi] = (float)b.intensity * life_env;
                band_half[bi] = b.width * 0.5f;

                hue_to_rgb(b.hue, band_r[bi], band_g[bi], band_b[bi]);
            }

            // Compose this ring LED by LED, additively across all bands.
            for (int led = 0; led < 24; led++) {
                uint16_t sumR = 0, sumG = 0, sumB = 0;

                for (uint8_t bi = 0; bi < OLS_MAX_BANDS; bi++) {
                    if (band_peak[bi] <= 0.0f) continue;

                    const OLS_Band& b = ols_bands[ring][bi];

                    // Circular distance from band centre to this LED,
                    // wrapped to -12..+12.
                    float diff = (float)led - b.pos;
                    while (diff >  12.0f) diff -= 24.0f;
                    while (diff < -12.0f) diff += 24.0f;

                    const float half = band_half[bi];
                    if (fabsf(diff) > half) continue;

                    // Bell profile: 1 at centre, 0 at both edges.
                    const float t   = 1.0f - fabsf(diff) / half;
                    const float env = powf(t, 0.65f);

                    const uint16_t scale = (uint16_t)(env * band_peak[bi]);
                    if (scale == 0) continue;

                    // Additive blend: overlapping bands mix colours and
                    // get brighter, without ever hitting pure white.
                    sumR += ((uint16_t)band_r[bi] * scale) / 255;
                    sumG += ((uint16_t)band_g[bi] * scale) / 255;
                    sumB += ((uint16_t)band_b[bi] * scale) / 255;
                }

                rr[base + led] = (sumR > 255) ? 255 : (uint8_t)sumR;
                rg[base + led] = (sumG > 255) ? 255 : (uint8_t)sumG;
                rb[base + led] = (sumB > 255) ? 255 : (uint8_t)sumB;
            }
        }

        for (int i = 0; i < 24; i++) {
            setLEDRing(0, i, rr[i],      rg[i],      rb[i]);
            setLEDRing(1, i, rr[24 + i], rg[24 + i], rb[24 + i]);
        }
    }
#endif  // LED_RINGS || LED_DEBUG
}

#if defined OLED_DISPLAYS

FLASHMEM void mam_show_code_on_right_oled(const char* code)
{
  if (!code || strlen(code) == 0) return;

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  mcp.write1(OLED_CS_R, LOW);
  mcp.write1(OLED_CS_L, HIGH);

  oled_display.clearDisplay();
  oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  oled_display.setTextSize(2);
  oled_display.setCursor(0, 0);
  oled_display.print(F("CODE:"));

  oled_display.setCursor(0, 16);
  oled_display.print(code);

  oled_display.display();

  oled_fb_owner = 1;

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();
}

FLASHMEM void mam_clear_right_oled(void)
{
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  mcp.write1(OLED_CS_R, LOW);
  mcp.write1(OLED_CS_L, HIGH);

  oled_display.clearDisplay();
  oled_display.display();

  oled_fb_owner = 1;

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();
}

#endif // OLED_DISPLAYS

#else
// ---------------------------------------------------------------------------
// Safe stubs
// ---------------------------------------------------------------------------
FLASHMEM void oled_ring_screensaver_start(void)  {}
FLASHMEM void oled_ring_screensaver_stop(void)   {}
FLASHMEM void oled_ring_screensaver_update(void) {}

#endif  // OLED_DISPLAYS || LED_RINGS || LED_DEBUG