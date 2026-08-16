
#include <Arduino.h>
#include "config.h"
#include "sequencer.h"
//#include "UI.h"
#include <LCDMenuLib2.h>
#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined RA_DISPLAY
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif

#if defined OLED_DISPLAYS
#include <Adafruit_SSD1306.h>
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
extern bool ui_has_editors();
#endif

#if defined GLOW
extern int glow_menu_item;
#endif



extern LCDMenuLib2 LCDML;
extern sequencer_t seq;
extern config_t configuration;
extern uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val);
extern uint16_t RGB24toRGB565(uint8_t r, uint8_t g, uint8_t b);


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
      int physIdx = (LED_RING_START_OFFSET + i) % 24;
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

  // Labels
  // display.setTextSize(1);
  // display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  // display.setCursor(x_center_left - 16, y_center_left + ring_radius + led_radius + 10);
  // display.print("LEFT");
  // display.setCursor(x_center_right - 18, y_center_right + ring_radius + led_radius + 10);
  // display.print("RIGHT");
}
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
void setLEDRing(uint8_t ring, uint8_t led, uint8_t r, uint8_t g, uint8_t b) {
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



#if defined LED_RINGS || defined LED_DEBUG
uint8_t leftRingMode = LEFT_RING_MODE_VOLUME;   // default to volume
#endif

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

#if defined LED_RINGS || defined LED_DEBUG

#endif

#if defined OLED_DISPLAYS 

FLASHMEM void OLED_status() {
  // static bool state = false;   // false = LOW, true = HIGH

  if (OLEDTimer >= 1500) {   // 1,5 seconds elapsed
    OLEDTimer = 0;         // reset timer
    //state = !state;          // flip the state

    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

    mcp.write1(OLED_CS_L, LOW);
    mcp.write1(OLED_CS_R, HIGH);
    delay(1);

    oled_display.setCursor(4 * 6, 3 * 7);
    oled_display.print(AudioProcessorUsage());
    oled_display.display();

    mcp.write1(OLED_CS_L, HIGH);
    mcp.write1(OLED_CS_R, LOW);
    delay(1);

    oled_display.setCursor(5 * 6, 3 * 7);
    oled_display.print(tempmonGetTemp());
    oled_display.display();

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
  delay(1);
  oled_display.display();
  oled_display.clearDisplay();
  oled_display.setCursor(0, 0);
  oled_display.println("WELCOME TO");
  // oled_display.setCursor(0, 3 * 7);
  // oled_display.print("CPU:   %");
   oled_display.display();

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, LOW);
    delay(1);
  oled_display.display();
  oled_display.clearDisplay();
  oled_display.setCursor(0, 0);
  oled_display.println("MICRODEXED");
  // oled_display.setCursor(0, 3 * 7);
  // oled_display.print("TEMP:");
   oled_display.display();

  mcp.write1(OLED_CS_L, HIGH);
  mcp.write1(OLED_CS_R, HIGH);
  SPI.endTransaction();
}
#endif