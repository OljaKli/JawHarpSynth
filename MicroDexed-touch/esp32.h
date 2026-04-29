#if defined ESP_DISPLAY

#include "config.h"
#include "scope.h"
#ifndef _espdisplayH_
#define _espdisplayH_

#include "Arduino.h"
#include <stdint.h>
//10 50   10 40 

// #define PACKETS_PER_BATCH   10      // Send this many packets before requesting ACK
// #define BATCH_SYNC_CMD      99      // Command code for SYNC packet
#define ACK_BYTE            0xAD
// #define ACK_TIMEOUT_MS      40       // Time to wait for ACK

#define CL(_r, _g, _b) ((((_r)&0xF8) << 8) | (((_g)&0xFC) << 3) | ((_b) >> 3))

#define sint16_t int16_t

#define SYSEX_HEADER_SIZE 3

// Minimum ESP firmware version required by this Teensy firmware
#define MIN_VERSION_ESP_DISPLAY "1.9.947"

// Command codes (renumbered consecutively, same as on ESP)
#define CMD_FILLRECT      2
#define CMD_DRAWRECT      3
#define CMD_DRAWLINE      4
#define CMD_FILLCIRCLE    6
#define CMD_DRAWPIXEL     7
#define CMD_PRINT_STRING  8
#define CMD_FILLRECT_FAST     9
#define CMD_DRAWRECT_FAST     10
#define CMD_DRAWLINE_FAST     11
#define CMD_FILLCIRCLE_FAST   12
#define CMD_DRAWPIXEL_FAST    13
#define CMD_PRINT_FAST         14
#define CMD_PRINT_STRING_FAST  15
#define CMD_SCOPE_DATA 16
#define CMD_FILLRECT_RAINBOW 17
#define CMD_SCREENSAVER_START  18
#define CMD_SCREENSAVER_STOP   19
#define CMD_FILLRECT_INDEXED     20
#define CMD_DRAWRECT_INDEXED     21
#define CMD_DRAWLINE_INDEXED     22
#define CMD_FILLCIRCLE_INDEXED   23
#define CMD_DRAWPIXEL_INDEXED    24
#define CMD_PRINT_STRING_INDEXED 25
#define CMD_GLOW_START 26
#define CMD_GLOW_UPDATE 27
#define CMD_GLOW_STOP 28
#define CMD_GLOW_CLEAR 29  
#define CMD_DRAW_BITMAP 30
#define CMD_DRAW_BITMAP_BG 31
#define CMD_DRAW_PARAM_BAR 32
#define CMD_DRAW_PANBAR 33
#define CMD_VERSION_REPORT 100   // unique command code
#define CMD_SET_VOLUME_ICONS_MODE 101
#define CMD_SET_EDIT_MODE 102
#define CMD_MIN_VERSION_ESP 103
#define CMD_MIDI_DATA  205

void sendPacket(uint8_t cmd, const uint8_t* payload, uint16_t len);
void sendEditMode(bool);

class espdisplay : public Print
{
private:
  uint8_t  sysex_buffer[512];
  uint16_t sysex_len = SYSEX_HEADER_SIZE;

public:

  espdisplay(uint8_t c);
  void begin(uint8_t c = 0);
  void flushSysEx();
  void sendSysEx(uint8_t cmd, uint8_t length, uint8_t* data, bool hasStartEnd);
  void fillSysexData(uint8_t arr[], uint8_t nbArg, ...);
  uint8_t fillSysexDataColor(uint8_t* arr, uint8_t nbArg, ...);
  void fillScreen(uint16_t color);
  inline void fillWindow(uint16_t color) { fillScreen(color); }
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void sendScopeData(uint16_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t* data);

  void drawESPParamBar(uint8_t x, uint8_t y, uint8_t value, uint8_t min, uint8_t max,
    uint8_t highlight, bool show_bar, const char* zero_name = nullptr);
  void drawESPPanBar(uint8_t x, uint8_t y, uint8_t value, uint8_t highlight);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void fillRectRainbow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t last_h);

  void drawLocalBitmap(int16_t x, int16_t y, uint8_t bitmap_idx, uint16_t color);
  void drawLocalBitmap(int16_t x, int16_t y, uint8_t bitmap_idx, uint16_t color, uint16_t bg);

  void setRotation(uint8_t r);
  void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
  // Pass 8-bit (each) R,G,B, get back 16-bit packed color
  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
  {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  uint8_t readcommand8(uint8_t reg, uint8_t index = 0);
  uint16_t readScanLine();
  void setFrameRateControl(uint8_t mode);

  void writeRect(int16_t x, int16_t y, int16_t w, int16_t h,
    const uint16_t* pcolors);

  void writeSubImageRect(int16_t x, int16_t y, int16_t w, int16_t h,
    int16_t image_offset_x, int16_t image_offset_y, int16_t image_width, int16_t image_height,
    const uint16_t* pcolors);
  void writeSubImageRectBytesReversed(int16_t x, int16_t y, int16_t w, int16_t h,
    int16_t image_offset_x, int16_t image_offset_y, int16_t image_width, int16_t image_height,
    const uint16_t* pcolors);

  void writeRect1BPP(int16_t x, int16_t y, int16_t w, int16_t h,
    const uint8_t* pixels, const uint16_t* palette);

  void writeRectNBPP(int16_t x, int16_t y, int16_t w, int16_t h,
    uint8_t bits_per_pixel, const uint8_t* pixels,
    const uint16_t* palette);

  // from Adafruit_GFX.h

  void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

  void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w,
    int16_t h, uint16_t color);
  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
    uint16_t bg, uint8_t size_x, uint8_t size_y);
  void inline drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
    uint16_t bg, uint8_t size)
  {
    drawChar(x, y, c, color, bg, size);
  }
  static const int16_t CENTER = 9998;
  void setCursor(int16_t x, int16_t y, bool autoCenter = false);
  void getCursor(int16_t* x, int16_t* y);
  void setTextColor(uint16_t c);
  void setTextColor(uint16_t c, uint16_t bg);
  void setTextSize(uint8_t sx, uint8_t sy);
  void inline setTextSize(uint8_t s)
  {
    setTextSize(s, s);
  }


  bool console;

  void setOrigin(int16_t x = 0, int16_t y = 0)
  {
    _originx = x;
    _originy = y;
    updateDisplayClip();
  }
  void getOrigin(int16_t* x, int16_t* y)
  {
    *x = _originx;
    *y = _originy;
  }

  void setClipRect(int16_t x1, int16_t y1, int16_t w, int16_t h)
  {
    _clipx1 = x1;
    _clipy1 = y1;
    _clipx2 = x1 + w;
    _clipy2 = y1 + h;
    updateDisplayClip();
  }
  void setClipRect()
  {
    _clipx1 = 0;
    _clipy1 = 0;
    _clipx2 = _width;
    _clipy2 = _height;
    updateDisplayClip();
  }

  // overwrite print functions:
  // virtual size_t write(uint8_t);
  // virtual size_t write(const uint8_t* buffer, size_t size);

  size_t write(uint8_t);
  size_t write(const uint8_t* buffer, size_t size);

  void repeatWrite(const uint8_t* buffer, size_t size, uint8_t nbRepeat);

  int16_t width(void)
  {
    return _width;
  }
  int16_t height(void)
  {
    return _height;
  }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  int16_t getCursorX(void) const
  {
    return cursor_x;
  }
  int16_t getCursorY(void) const
  {
    return cursor_y;
  }

  void drawFontChar(unsigned int c);

  // added support for drawing strings/numbers/floats with centering
  // modified from tft_ili9341_ESP github library

  // Handle char arrays
  int16_t drawString(const String& string, int poX, int poY);
  int16_t drawString(const char string[], int16_t len, int poX, int poY);

protected:

  int16_t _width, _height; // Display w/h as modified by current rotation
  int16_t cursor_x, cursor_y;
  bool _center_x_text = false;
  bool _center_y_text = false;
  int16_t _clipx1, _clipy1, _clipx2, _clipy2;
  int16_t _originx, _originy;
  int16_t _displayclipx1, _displayclipy1, _displayclipx2, _displayclipy2;
  bool _invisible = false;
  bool _standard = true; // no bounding rectangle or origin set.

  inline void updateDisplayClip()
  {
    _displayclipx1 = max(0, min(_clipx1 + _originx, width()));
    _displayclipx2 = max(0, min(_clipx2 + _originx, width()));

    _displayclipy1 = max(0, min(_clipy1 + _originy, height()));
    _displayclipy2 = max(0, min(_clipy2 + _originy, height()));
    _invisible =
      (_displayclipx1 == _displayclipx2 || _displayclipy1 == _displayclipy2);
    _standard = (_displayclipx1 == 0) && (_displayclipx2 == _width) && (_displayclipy1 == 0) && (_displayclipy2 == _height);
  }

  uint16_t textcolor, textbgcolor;
  uint32_t textcolorPrexpanded, textbgcolorPrexpanded;
  uint8_t textsize_x, textsize_y, rotation;

  uint32_t padX;
};

#endif
#endif