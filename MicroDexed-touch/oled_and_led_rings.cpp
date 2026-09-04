
#include <Arduino.h>
#include "config.h"
#include "sequencer.h"
#include "UI.h"
#include <LCDMenuLib2.h>
#include <Audio.h>                // for AudioAnalyzePeak
#include "synth_dexed.h"          // for AudioSynthDexed (if needed)
#include "touch.h"
#include <SD.h>

#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined RA_DISPLAY
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif


#if defined OLED_DISPLAYS
#include <Adafruit_SSD1306.h>
extern char last_right_helptext[20];
extern Adafruit_SSD1306 oled_display;
elapsedMillis OLEDTimer;
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
//extern bool ui_has_editors();
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

#ifdef I2C_ENCODERS
extern bool encoderActivity;
extern elapsedMillis encoderActivityTimer;
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

FLASHMEM  float get_led_brightness_factor(uint8_t relIdx, uint8_t gradient) {
  if (gradient == 0) return 1.0f;
  // Reversed: relIdx=0 (offset) -> low brightness, relIdx=23 (opposite) -> high brightness
  float t = (float)relIdx / 23.0f;             // t=0 at offset, t=1 at opposite
  float min_bright = 0.1f;
  float max_bright = 1.0f;
  float gamma = 1.0f;                          // linear fade (or adjust for curve)
  float factor = min_bright + (max_bright - min_bright) * powf(t, gamma);
  float mix = gradient / 100.0f;
  return (1.0f - mix) * 1.0f + mix * factor;
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

#if defined I2C_ENCODERS
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
}

#if defined I2C_ENCODERS
FLASHMEM void updateRightOLEDEncoder() {
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

// ---- Main OLED status update ----
FLASHMEM void OLED_status() {

#if defined I2C_ENCODERS
  if (tapTempoActive) {
    return;
  }
#endif
  static const uint8_t max_modes = 9;

  if (OLEDTimer >= 2500) {
    OLEDTimer = 0;
    oled_info_mode++;
    if (oled_info_mode >= max_modes) oled_info_mode = 0;

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

    bool inMainMenu = (LCDML.FUNC_getID() == 255);

    // ========== LEFT DISPLAY ==========
    mcp.write1(OLED_CS_L, LOW);
    mcp.write1(OLED_CS_R, HIGH);

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
        }
        else {
          oled_display.print(F("PSRAM:"));
          oled_display.print(totalMB);
          oled_display.print(F("MB"));
        }
        oled_display.setCursor(0, 16);
        if (totalMB == 0) {
          // leave blank
        }
        else if (psram_free_bytes == 0) {
          oled_display.print(F("FULL"));
        }
        else {
          uint8_t freePercent = (uint8_t)((psram_free_bytes * 100) / (totalMB * 1048576));
          oled_display.print(F("FREE:"));
          if (freePercent < 10) oled_display.print(F(" "));
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
        if (seq.clock == 0) oled_display.print(F("INTERN"));
        else if (seq.clock == 1) oled_display.print(F("MIDI SLAVE"));
        else oled_display.print(F("MIDI MASTER"));
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

    oled_display.display();

    // ========== RIGHT DISPLAY ==========
    mcp.write1(OLED_CS_L, HIGH);
    mcp.write1(OLED_CS_R, LOW);

    oled_display.clearDisplay();
    oled_display.setTextSize(2);
    oled_display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

    if (inMainMenu) {
      switch (oled_info_mode) {
      case 0: {
        oled_display.setCursor(0, 0);
        oled_display.print(F("SONG "));
        oled_display.print(configuration.sys.performance_number);
        oled_display.print(F(":"));
        oled_display.setCursor(0, 16);
        print_trunc(seq.name, 10);
        break;
      }
      case 1:
        oled_display.setCursor(0, 0);
        if (sd_card_internal == 0) {
          oled_display.print(F("SD:NONE"));
        }
        else {
          print_sd_total(volume_internal, sd_card_internal > 0, F("INT: "));
        }
        oled_display.setCursor(0, 16);

#ifdef SECOND_SD
        if (sd_card_external == 0) {
          oled_display.print(F("NONE"));
        }
        else {
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
      case 8: {
        oled_display.setCursor(0, 0);
        oled_display.print(F("SMARTFILT."));
        oled_display.setCursor(0, 16);
        oled_display.print(seq.smartfilter ? F("ON") : F("OFF"));
        break;
      }
      }
    }

    oled_display.display();

    // ---------- End SPI ----------
    mcp.write1(OLED_CS_L, HIGH);
    mcp.write1(OLED_CS_R, HIGH);
    SPI.endTransaction();
  }
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
