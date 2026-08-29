#if defined ESP_DISPLAY

#include "config.h"

#ifndef _espdisplayH_
#include "esp32.h"
#endif

extern bool remote_active;
extern uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val);

// 5x7 font
PROGMEM const unsigned char font[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, //
    0x3E, 0x5B, 0x4F, 0x5B, 0x3E, //
    0x3E, 0x6B, 0x4F, 0x6B, 0x3E, //
    0x1C, 0x3E, 0x7C, 0x3E, 0x1C, //
    0x18, 0x3C, 0x7E, 0x3C, 0x18, //
    0x1C, 0x57, 0x7D, 0x57, 0x1C, //
    0x1C, 0x5E, 0x7F, 0x5E, 0x1C, //
    0x00, 0x18, 0x3C, 0x18, 0x00, //
    0xFF, 0xE7, 0xC3, 0xE7, 0xFF, //
    0x00, 0x18, 0x24, 0x18, 0x00, //
    0xFF, 0xE7, 0xDB, 0xE7, 0xFF, //
    0x30, 0x48, 0x3A, 0x06, 0x0E, //
    0x26, 0x29, 0x79, 0x29, 0x26, //
    0x40, 0x30, 0x0c, 0x02, 0x7F, // BRAIDS Wave 1 SAW  // 0x39, 0x55, 0x55, 0x55, 0x59,  // note: grid is vertical slices
    0x10, 0x08, 0x04, 0x1e, 0x00, // BRAIDS Centered SAW
    0x40, 0x20, 0x10, 0x08, 0x7C, // BRAIDS BABY SAW
    0x60, 0x1C, 0x03, 0x1C, 0x60, // BRAIDS TRIANGLE
    0x7f, 0x01, 0x01, 0x01, 0x7f, // BRAIDS SQUARE //160
    0x78, 0x08, 0x08, 0x08, 0x78, // BRAIDS BABY SQUARE
    0x40, 0x40, 0x7F, 0x01, 0x7F, // BRAIDS PULSE  0x7D, 0x12, 0x11, 0x12, 0x7D A-umlaut
    0xF0, 0x28, 0x25, 0x28, 0xF0,
    0x3e, 0x08, 0x00, 0x08, 0x00, // Sample Play Mode : Trigger Mode. part1 //144
    0x08, 0x00, 0x3E, 0x1C, 0x08, // Sample Play Mode : Trigger Mode. part2 //145
    0x3e, 0x08, 0x08, 0x08, 0x08, // Sample Play Mode : Hold Mode part1
    0x08, 0x08, 0x08, 0x08, 0x3e, // Sample Play Mode : Hold Mode part2    0x10, 0x20, 0x7E, 0x20, 0x10, //
    0x06, 0x0F, 0x09, 0x0F, 0x06, //
    0x08, 0x08, 0x2A, 0x1C, 0x08, //
    0x08, 0x1C, 0x2A, 0x08, 0x08, //
    0x1E, 0x10, 0x10, 0x10, 0x10, //
    0x0C, 0x1E, 0x0C, 0x1E, 0x0C, //
    0x08, 0x0c, 0x0e, 0x0c, 0x08, // up arrow
    0x02, 0x06, 0x0e, 0x06, 0x02, // down arrow
    0x00, 0x00, 0x00, 0x00, 0x00, // space
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x7F, 0x51, 0x49, 0x45, 0x7F, // 0
    0x40, 0x41, 0x7F, 0x40, 0x40, // 1
    0x79, 0x49, 0x49, 0x49, 0x4F, // 2
    0x49, 0x49, 0x49, 0x49, 0x7F, // 3
    0x0F, 0x08, 0x08, 0x08, 0x7F, // 4
    0x4F, 0x49, 0x49, 0x49, 0x79, // 5
    0x7F, 0x49, 0x49, 0x49, 0x79, // 6
    0x01, 0x01, 0x71, 0x09, 0x07, // 7
    0x7F, 0x49, 0x49, 0x49, 0x7F, // 8
    0x0F, 0x09, 0x09, 0x09, 0x7F, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @  //64
    0x7E, 0x09, 0x09, 0x09, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x41, 0x3E, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x41, 0x51, 0x32, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x41, 0x41, 0x7F, 0x41, 0x41, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // M
    0x7F, 0x02, 0x04, 0x08, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x09, 0x19, 0x66, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x47, 0x48, 0x48, 0x48, 0x3F, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x00, 0x7F, 0x41, 0x41, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // "\"
    0x41, 0x41, 0x7F, 0x00, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x38, 0x44, 0x44, 0x44, 0x7C, // a
    0x7E, 0x44, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x44, // c
    0x38, 0x44, 0x44, 0x44, 0x7E, // d
    0x38, 0x54, 0x54, 0x54, 0x58, // e
    0x10, 0x7C, 0x12, 0x12, 0x12, // f
    0x1C, 0x54, 0x54, 0x54, 0x3C, // g
    0x7E, 0x04, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x00, 0x7F, 0x10, 0x28, 0x44, // k
    0x3C, 0x40, 0x40, 0x40, 0x40, // l
    0x7C, 0x04, 0x78, 0x04, 0x7C, // m
    0x7C, 0x04, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x24, 0x24, 0x24, 0x1C, // p
    0x38, 0x24, 0x24, 0x24, 0x7C, // q
    0x78, 0x04, 0x04, 0x04, 0x04, // r
    0x58, 0x54, 0x54, 0x54, 0x34, // s
    0x04, 0x3E, 0x44, 0x44, 0x44, // t
    0x3C, 0x40, 0x40, 0x40, 0x3C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x4C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08, // ->
    0x08, 0x1C, 0x2A, 0x08, 0x08, // <-
};

#if defined APC
extern uint8_t apc_scroll_message; //used to detect the size of font

extern void apc(uint8_t a, uint8_t b, uint8_t c);
// Draw a character on apc
FLASHMEM void apc_drawChar(int xoff, unsigned char c)
{
  uint8_t x, y, mx, my, my_clrvertline /*max x, max y, and max y for clearing 1st vertical line in scrolling mode */;
  uint8_t x3=0, y3=0; //these are needed for scaling font 5x7 to 3x5

  if(apc_scroll_message == true) // For welcome message at startup use big font 5x7
  {
    mx = 5; my = 7; my_clrvertline = my;
  }
  else // for value 2 of apc_scroll_message, use smaller font for pattern number, no scroll needed
  {
    mx = 5; my = 7; my_clrvertline = my+1;
  }
  for (y = 0; y <= my; y++)
  {
    for (x = 0,x3=0; x <= mx; x++)
    {
      uint16_t fontidx = c * mx + x;
      uint16_t fontbyt = font[fontidx];
      if(apc_scroll_message == 2) //special tweaks for font 5x7 to be scalled well to 3x5
      {
        if(c == '0' && fontbyt != 0x7F) fontbyt = 0x41; // clear slash from 0
        //if(c == '1' && fontbyt != 0x7F)   fontbyt &= ~0x40; //1 is a stick
        if(fontidx == '1' * mx)         fontbyt = 0x41;   //for 1: add top left dot, keep the base
        if(fontidx == '7' * mx + 2)     fontbyt = 0x79;   //for 7: add missing dot at middle
      }
      if (x + xoff >= 0 && x + xoff <= my_clrvertline && x < mx)
      {
        if (bitRead(fontbyt, y) == 1)
          apc((64 - 8 - 8 * y3 + x3 + xoff), 5, 6);
        else
          apc((64 - 8 - 8 * y3 + x3 + xoff), 0, 6);
      }
      else
        if (x + xoff >= 0 && x + xoff <= my && x == mx)
          apc((64 - 8 - 8 * y3 + x3 + xoff), 0, 6);
      if(apc_scroll_message == 2) //scale on x the font 5x7 to 3x5
      {
        if(x==0) x++;
        if(x==2) x++;
      }
      x3++;
    }
    y3++;
    if(apc_scroll_message == 2) //scale on y the font 5x7 to 3x5
    {
      if(c<'a')
       {if(y==1) y++;}
      else
      {if(y==2) y++;}
      if(y==3) y++;
    }
  }
}
#endif

// Predefined color table (must match order in ESP32)
static const uint16_t predefinedColors[] = {
  COLOR_BACKGROUND, // 0
  COLOR_SYSTEXT,    // 1
  DX_DARKCYAN,      // 2
  COLOR_INSTR,      // 3
  COLOR_CHORDS,     // 4
  COLOR_ARP,        // 5
  COLOR_DRUMS,      // 6
  COLOR_PITCHSMP,   // 7
  RED,              // 8
  GREEN,            // 9
  MIDDLEGREEN,      // 10
  DARKGREEN,        // 11
  GREY1,            // 12
  GREY2,            // 13
  GREY3,            // 14
  GREY4             // 15
};

// Returns index 0-15 if color is in table, else 255
static uint8_t getColorIndex(uint16_t color) {
  for (uint8_t i = 0; i < 16; i++) {
    if (predefinedColors[i] == color) return i;
  }
  return 255;
}

// MIDI sysex render header for remote
const uint8_t sysexRenderHeader[] = { 0xf0, 0x43, 0x21 };

// Packet protocol
#define PKT_SYNC        0xAA

uint16_t lastFillColor;
uint16_t lastLineColor;    // for drawLine and drawRect outline
uint16_t lastPixelColor;
uint16_t lastTextFgColor;
uint16_t lastTextBgColor;
uint8_t  lastTextSize;

extern void processIncomingSerial();

void sendPacket(uint8_t cmd, const uint8_t* payload, uint16_t len) {
  if (len > 250) return;   // keep the safety check

  Serial4.write(PKT_SYNC);
  Serial4.write(cmd);
  Serial4.write(len);
  for (uint8_t i = 0; i < len; i++) {
    Serial4.write(payload[i]);
  }
  uint8_t checksum = PKT_SYNC ^ cmd ^ (len >> 8) ^ (len & 0xFF);
  for (uint8_t i = 0; i < len; i++) checksum ^= payload[i];
  Serial4.write(checksum);
}

void sendGlowClear() {
  sendPacket(CMD_GLOW_CLEAR, nullptr, 0);
}

void sendScreensaverStop() {
  sendPacket(CMD_SCREENSAVER_STOP, nullptr, 0);
}

FLASHMEM void start_screensaver_on_esp(uint8_t id) {
  uint8_t buf[4] = { id };
  sendPacket(CMD_SCREENSAVER_START, buf, 1);
}

FLASHMEM espdisplay::espdisplay(uint8_t c)
{
  _width = TFT_WIDTH;
  _height = TFT_HEIGHT;
  rotation = 0;
  cursor_y = cursor_x = 0;
  textsize_x = textsize_y = 1;
  textcolor = textbgcolor = 0xFFFF;
  lastFillColor = 0xFFF1;
  lastLineColor = 0xFFF1;
  lastPixelColor = 0xFFF1;
  lastTextFgColor = 0xFFF1;
  lastTextBgColor = 0xFFF1;
  lastTextSize = 9;
  setClipRect();
  setOrigin();
};

//=======================================================================

void sendEditMode(bool editState) {
  uint8_t payload = editState ? 1 : 0;
  sendPacket(CMD_SET_EDIT_MODE, &payload, 1);
}

void espdisplay::drawPianoRollBackgroundRegion(uint16_t xFrom, uint16_t width, uint8_t height,
                                                uint8_t lowestNote, uint8_t highestNote,
                                                uint8_t noteHeight,
                                                const std::set<uint8_t>& notes) {
    uint8_t noteCount = highestNote - lowestNote;
    uint8_t maskBytes = (noteCount + 7) / 8;
    uint8_t payload[8 + maskBytes];  
    
    payload[0] = lowByte(xFrom);
    payload[1] = highByte(xFrom);
    payload[2] = lowByte(width);
    payload[3] = highByte(width);
    payload[4] = height;               // uint8_t
    payload[5] = lowestNote;           // uint8_t
    payload[6] = highestNote;          // uint8_t
    payload[7] = noteHeight;           // uint8_t
    
    uint8_t* mask = payload + 8;
    memset(mask, 0, maskBytes);
    
    if (notes.empty()) {
        memset(mask, 0xFF, maskBytes);
    } else {
        for (uint8_t note : notes) {
            if (note >= lowestNote && note < highestNote) {
                uint8_t bit = note - lowestNote;
                mask[bit >> 3] |= (1 << (bit & 7));
            }
        }
    }
    
    sendPacket(CMD_DRAW_PIANO_ROLL_BG_REGION, payload, 8 + maskBytes);
}

void espdisplay::drawPianoRollKeys(uint8_t rollWidth, uint8_t contentHeight,
                                   const std::vector<uint8_t>& whiteLinesY,
                                   uint8_t blackKeyHeight,
                                   const std::vector<uint8_t>& blackKeysY) {
    uint8_t numWhite = whiteLinesY.size();
    uint8_t numBlack = blackKeysY.size();
    uint16_t payloadSize = 1 + 1 + 1 + numWhite + 1 + 1 + numBlack;
    uint8_t payload[payloadSize];
    uint8_t idx = 0;

    payload[idx++] = rollWidth;
    payload[idx++] = contentHeight;
    payload[idx++] = numWhite;
    for (uint8_t y : whiteLinesY) {
        payload[idx++] = y;
    }
    payload[idx++] = numBlack;
    payload[idx++] = blackKeyHeight;
    for (uint8_t y : blackKeysY) {
        payload[idx++] = y;
    }

    sendPacket(CMD_DRAW_PIANO_ROLL_KEYS, payload, payloadSize);
}

void espdisplay::drawESPPanBar(uint8_t x, uint8_t y, uint8_t value, uint8_t highlight) {
  uint8_t payload[4];
  payload[0] = x;
  payload[1] = y;
  payload[2] = value;      // 0-40 (20 = center)
  payload[3] = highlight;  // 0=normal,1=selected,2=editing
  sendPacket(CMD_DRAW_PANBAR, payload, 4);
}

void espdisplay::drawESPParamBar16(uint8_t x, uint8_t y, uint16_t value, uint16_t min, uint16_t max,
                                   uint8_t highlight, bool show_bar, const char* zero_name) {
  uint8_t zero_len = (zero_name != nullptr) ? strlen(zero_name) : 0;
  uint8_t payload[9 + zero_len];  // 9 = x(1) + y(1) + value(2) + min(2) + max(2) + flags(1) + zero_len(1)
  uint8_t idx = 0;
  payload[idx++] = x;
  payload[idx++] = y;
  payload[idx++] = lowByte(value);
  payload[idx++] = highByte(value);
  payload[idx++] = lowByte(min);
  payload[idx++] = highByte(min);
  payload[idx++] = lowByte(max);
  payload[idx++] = highByte(max);
  payload[idx++] = (show_bar ? 1 : 0) | ((highlight & 3) << 1);
  payload[idx++] = zero_len;
  if (zero_len) {
    memcpy(&payload[idx], zero_name, zero_len);
  }
  sendPacket(CMD_DRAW_PARAM_BAR16, payload, 9 + zero_len);
}

void espdisplay::drawESPParamBar(uint8_t x, uint8_t y, uint8_t value, uint8_t min, uint8_t max,
  uint8_t highlight, bool show_bar, const char* zero_name) {
  uint8_t zero_len = (zero_name != nullptr) ? strlen(zero_name) : 0;
  uint8_t payload[7 + zero_len];
  uint8_t idx = 0;
  payload[idx++] = x;
  payload[idx++] = y;
  payload[idx++] = value;
  payload[idx++] = min;
  payload[idx++] = max;
  payload[idx++] = (show_bar ? 1 : 0) | ((highlight & 3) << 1);
  payload[idx++] = zero_len;
  if (zero_len) {
    memcpy(&payload[idx], zero_name, zero_len);
  }
  sendPacket(CMD_DRAW_PARAM_BAR, payload, 7 + zero_len); // FIXED: use actual payload size
}

//--------------------------------------------------------------------------------------

void espdisplay::drawLocalBitmap(int16_t x, int16_t y, uint8_t bitmap_idx, uint16_t color) {
  if (esp_draw) {
    uint8_t payload[6];
    payload[0] = lowByte(x);
    payload[1] = highByte(x);
    payload[2] = (uint8_t)y;
    payload[3] = bitmap_idx;
    payload[4] = lowByte(color);
    payload[5] = highByte(color);
    sendPacket(CMD_DRAW_BITMAP, payload, 6);
  }
}

void espdisplay::drawLocalBitmap(int16_t x, int16_t y, uint8_t bitmap_idx, uint16_t color, uint16_t bg) {
  if (esp_draw) {
    uint8_t payload[8];
    payload[0] = lowByte(x);
    payload[1] = highByte(x);
    payload[2] = (uint8_t)y;
    payload[3] = bitmap_idx;
    payload[4] = lowByte(color);
    payload[5] = highByte(color);
    payload[6] = lowByte(bg);
    payload[7] = highByte(bg);
    sendPacket(CMD_DRAW_BITMAP_BG, payload, 8);
  }
}

// void espdisplay::sendScopeData(uint16_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t* data) {
//   uint8_t payload[5 + w];   // x(2), y(1), w(1), h(1), data
//   payload[0] = lowByte(x);
//   payload[1] = highByte(x);
//   payload[2] = y;
//   payload[3] = w;
//   payload[4] = h;
//   memcpy(&payload[5], data, w);
//   sendPacket(CMD_SCOPE_DATA, payload, 5 + w);
// }

void espdisplay::sendScopeData(uint16_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t* data) {
  static uint16_t last_x = 0xFFFF, last_y = 0xFF, last_w = 0, last_h = 0;

  // Send configuration only when the window changes
  if (x != last_x || y != last_y || w != last_w || h != last_h) {
    uint8_t cfg[5];
    cfg[0] = lowByte(x);
    cfg[1] = highByte(x);
    cfg[2] = y;
    cfg[3] = w;
    cfg[4] = h;
    sendPacket(CMD_SET_SCOPE_CONFIG, cfg, 5);
    last_x = x; last_y = y; last_w = w; last_h = h;
  }

  // Send Y‑data only
  sendPacket(CMD_SCOPE_Y_DATA, data, w);
}

void espdisplay::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  x += _originx;
  y += _originy;
  if ((x < _displayclipx1) || (x >= _displayclipx2) || (y < _displayclipy1) || (y >= _displayclipy2))
    return;

  if (console && remote_active)
  {
    static uint8_t sysexDrawPixel[9] = {
      0x0, 0x0, 0x0, 0x0,
      0x0, 0x0, 0x0, 0x0, 0x0 // could be a unknown color
    };

    fillSysexData(sysexDrawPixel, 2, x, y);
    uint8_t colors[4];
    uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
    sysexDrawPixel[4] = nbBytes;
    memcpy(sysexDrawPixel + 4 + 1, colors, nbBytes);

    sendSysEx(90, 4 + 1 + nbBytes, sysexDrawPixel, true);
    delayMicroseconds(20); //40
  }

  if (esp_draw) {
    if (color == lastPixelColor) {
      // Fast pixel (no color)
      uint8_t payload[3]; // x(2), y(1)
      payload[0] = lowByte(x);
      payload[1] = highByte(x);
      payload[2] = (uint8_t)y;
      sendPacket(CMD_DRAWPIXEL_FAST, payload, 3);
    }
    else {
      uint8_t idx = getColorIndex(color);
      if (idx != 255) {
        // Indexed pixel
        uint8_t payload[4] = { lowByte(x), highByte(x), (uint8_t)y, idx };
        sendPacket(CMD_DRAWPIXEL_INDEXED, payload, 4);
        lastPixelColor = color;
      }
      else {
        // Full pixel
        uint8_t payload[5] = { lowByte(x), highByte(x), (uint8_t)y, lowByte(color), highByte(color) };
        sendPacket(CMD_DRAWPIXEL, payload, 5);
        lastPixelColor = color;
      }
    }
  }
}

void espdisplay::fillScreen(uint16_t color)
{
  if (remote_active)
    console = true;
  fillRect(0, 0, _width, _height, color);
}

void espdisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
  uint16_t color)
{
  x += _originx;
  y += _originy;

  // Rectangular clipping (drawChar w/big text requires this)
  if ((x >= _displayclipx2) || (y >= _displayclipy2))
    return;
  if (((x + w) <= _displayclipx1) || ((y + h) <= _displayclipy1))
    return;

  if (console && remote_active)
  {
    static uint8_t sysexFillRect[13] = {
      0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      0x0, 0x0, 0x0, 0x0, 0x0 // could be unknown color
    };

    fillSysexData(sysexFillRect, 4, x, y, w, h);
    uint8_t colors[4];
    uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
    sysexFillRect[8] = nbBytes;
    memcpy(sysexFillRect + 8 + 1, colors, nbBytes);

    sendSysEx(94, 8 + 1 + nbBytes, sysexFillRect, true);
  }

  if (esp_draw) {
    if (color == lastFillColor) {
      // Fast fillRect (no color)
      uint8_t payload[6]; // x(2), y(1), w(2), h(1)
      payload[0] = lowByte(x);
      payload[1] = highByte(x);
      payload[2] = (uint8_t)y;
      payload[3] = lowByte(w);
      payload[4] = highByte(w);
      payload[5] = (uint8_t)h;
      sendPacket(CMD_FILLRECT_FAST, payload, 6);
    }
    else {
      uint8_t idx = getColorIndex(color);
      if (idx != 255) {
        // Indexed fillRect
        uint8_t payload[7] = {
          lowByte(x),
          highByte(x),
          (uint8_t)y,
          lowByte(w),
          highByte(w),
          (uint8_t)h,
          idx };
        sendPacket(CMD_FILLRECT_INDEXED, payload, 7);
        lastFillColor = color;
      }
      else {
        // Full fillRect
        uint8_t payload[8] = {
          lowByte(x),
          highByte(x),
          (uint8_t)y,
          lowByte(w),
          highByte(w),
          (uint8_t)h,
          lowByte(color),
          highByte(color) };
        sendPacket(CMD_FILLRECT, payload, 8);
        lastFillColor = color;
      }
    }
  }
}

// draw rectangle with rainbow gradient
FLASHMEM void espdisplay::fillRectRainbow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t last_h)
{
  // int z = 0;
  // do
  // {
  //   uint16_t color = ColorHSV((100 - h + z), 200, 200);
  //   drawFastHLine(x, y - h + z, w, color);
  //   z++;
  // } while (z < h - last_h);

  // issue FILL_RECT_RAINBOW command in MicroDexed-WebRemote.
  if (console && remote_active)
  {
    static uint8_t sysex[8] = {
      0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    };

    fillSysexData(sysex, 4, x, y - h, w, h);
    sendSysEx(95, 8, sysex, true);
  }

  uint8_t payload[7];
  payload[0] = lowByte(x);
  payload[1] = highByte(x);
  payload[2] = (uint8_t)y;         // bottom Y (fits in 0‑240)
  payload[3] = lowByte(w);
  payload[4] = highByte(w);
  payload[5] = (uint8_t)h;         // height (≤ 100, fits in uint8_t)
  payload[6] = (uint8_t)last_h;     // previous height
  sendPacket(CMD_FILLRECT_RAINBOW, payload, 7);

}

FLASHMEM void espdisplay::setRotation(uint8_t m)
{
  rotation = m % 4; // can't be higher than 3
  switch (rotation)
  {
  case 0:
    _width = TFT_WIDTH;
    _height = TFT_HEIGHT;
    break;
  case 1:
    _width = TFT_HEIGHT;
    _height = TFT_WIDTH;
    break;
  case 2:
    _width = TFT_WIDTH;
    _height = TFT_HEIGHT;
    break;
  case 3:
    _width = TFT_HEIGHT;
    _height = TFT_WIDTH;
    break;
  }
  setClipRect();
  setOrigin();

  cursor_x = 0;
  cursor_y = 0;
}

void espdisplay::writeRect1BPP(int16_t x, int16_t y, int16_t w, int16_t h,
  const uint8_t* pixels,
  const uint16_t* palette)
{
  // Simply call through our helper
  writeRectNBPP(x, y, w, h, 1, pixels, palette);
}

FLASHMEM void espdisplay::begin(uint8_t c)

{
  delay(150); //necessary
}

/*
  This is the core graphics library for all our displays, providing a common
  set of graphics primitives (points, lines, circles, etc.).  It needs to be
  paired with a hardware-specific library for each display device we carry
  (to handle the lower-level functions).

  Adafruit invests time and resources providing this open source code, please
  support Adafruit & open-source hardware by purchasing products from Adafruit!

  Copyright (c) 2013 Adafruit Industries.  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  - Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
  - Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

/**************************************************************************/
/*!
   @brief    Draw a circle with filled color
    @param    x0   Center-point x coordinate
    @param    y0   Center-point y coordinate
    @param    r   Radius of circle
    @param    color 16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
FLASHMEM void espdisplay::fillCircle(int16_t x0, int16_t y0, int16_t r,
  uint16_t color)
{
  if (remote_active)
  {
    static uint8_t sysexFillCircle[11] = {
      0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      0x0, 0x0, 0x0, 0x0, 0x0, // could be unknow color
    };

    fillSysexData(sysexFillCircle, 3, x0, y0, r);
    uint8_t colors[4];
    uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
    sysexFillCircle[6] = nbBytes;
    memcpy(sysexFillCircle + 6 + 1, colors, nbBytes);

    sendSysEx(97, 6 + 1 + nbBytes, sysexFillCircle, true);
    console = false;
  }
  if (esp_draw) {
    if (color == lastFillColor) {
      uint8_t payload[4]; // x0(2), y0(1), r(1)
      payload[0] = lowByte(x0);
      payload[1] = highByte(x0);
      payload[2] = (uint8_t)y0;
      payload[3] = (uint8_t)r;
      sendPacket(CMD_FILLCIRCLE_FAST, payload, 4);
    }
    else {
      uint8_t idx = getColorIndex(color);
      if (idx != 255) {
        // Indexed circle
        uint8_t payload[5] = { lowByte(x0), highByte(x0), (uint8_t)y0, (uint8_t)r, idx };
        sendPacket(CMD_FILLCIRCLE_INDEXED, payload, 5);
        lastFillColor = color;
      }
      else {
        // Full circle
        uint8_t payload[6] = { lowByte(x0), highByte(x0), (uint8_t)y0, (uint8_t)r, lowByte(color), highByte(color) };
        sendPacket(CMD_FILLCIRCLE, payload, 6);
        lastFillColor = color;
      }
    }
  }
}

#define CLIPLEFT  1
#define CLIPRIGHT 2
#define CLIPLOWER 4
#define CLIPUPPER 8

// Cohen Sutherland Line Clipping - thx Wikipedia
FLASHMEM int get_clips(int16_t& x, int16_t& y, int16_t XMin, int16_t YMin, int16_t XMax, int16_t YMax)
{
  int K = 0;
  if (y < YMin) K = CLIPLOWER;
  if (y > YMax) K = CLIPUPPER;
  if (x < XMin) K |= CLIPLEFT;
  if (x > XMax) K |= CLIPRIGHT;

  return K;
}

FLASHMEM bool clipline(int16_t& x1, int16_t& y1, int16_t& x2, int16_t& y2, int16_t XMin, int16_t YMin, int16_t XMax, int16_t YMax)
{
  int K1 = 0, K2 = 0;
  int32_t dx, dy;

  dx = x2 - x1;
  dy = y2 - y1;

  //  if(dx == 0 && dy == 0)
  //    return false;

  K1 = get_clips(x1, y1, XMin, YMin, XMax, YMax);
  K2 = get_clips(x2, y2, XMin, YMin, XMax, YMax);

  int i = 0;
  while (K1 || K2)
  {
    if (i++ >= 2)
      return false;

    if (K1 & K2)
      return false;

    if (K1)
    {
      if (K1 & CLIPLEFT)
      {
        y1 += (XMin - x1) * dy / dx;
        x1 = XMin;
      }
      else if (K1 & CLIPRIGHT)
      {
        y1 += (XMax - x1) * dy / dx;
        x1 = XMax;
      }
      if (K1 & CLIPLOWER)
      {
        x1 += (YMin - y1) * dx / dy;
        y1 = YMin;
      }
      else if (K1 & CLIPUPPER)
      {
        x1 += (YMax - y1) * dx / dy;
        y1 = YMax;
      }

      K1 = get_clips(x1, y1, XMin, YMin, XMax, YMax);
    }

    if (K2)
    {
      if (K2 & CLIPLEFT)
      {
        y2 += (XMin - x2) * dy / dx;
        x2 = XMin;
      }
      else if (K2 & CLIPRIGHT)
      {
        y2 += (XMax - x2) * dy / dx;
        x2 = XMax;
      }
      if (K2 & CLIPLOWER)
      {
        x2 += (YMin - y2) * dx / dy;
        y2 = YMin;
      }
      else if (K2 & CLIPUPPER)
      {
        x2 += (YMax - y2) * dx / dy;
        y2 = YMax;
      }

      K2 = get_clips(x2, y2, XMin, YMin, XMax, YMax);
    }
  }
  return true;
}

// Bresenham's algorithm - thx wikpedia
void espdisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
  uint16_t color)
{
  if (!clipline(x0, y0, x1, y1, _displayclipx1, _displayclipy1, _displayclipx2, _displayclipy2))
    return;

  if (remote_active)
  {
    static uint8_t sysexDrawLine[13] = {
      0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      0x0, 0x0, 0x0, 0x0, 0x0, // could be unknown color
    };

    fillSysexData(sysexDrawLine, 4, x0, y0, x1, y1);
    uint8_t colors[4];
    uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
    sysexDrawLine[8] = nbBytes;
    memcpy(sysexDrawLine + 8 + 1, colors, nbBytes);

    sendSysEx(96, 8 + 1 + nbBytes, sysexDrawLine, true);

    console = false;
  }
  if (esp_draw) {

    if (color == lastLineColor) {
      uint8_t payload[6]; // x0(2), y0(1), x1(2), y1(1)
      payload[0] = lowByte(x0);
      payload[1] = highByte(x0);
      payload[2] = (uint8_t)y0;
      payload[3] = lowByte(x1);
      payload[4] = highByte(x1);
      payload[5] = (uint8_t)y1;
      sendPacket(CMD_DRAWLINE_FAST, payload, 6);
    }
    else {
      uint8_t idx = getColorIndex(color);
      if (idx != 255) {
        // Indexed line
        uint8_t payload[7] = {
          lowByte(x0),
          highByte(x0),
          (uint8_t)y0,
          lowByte(x1),
          highByte(x1),
          (uint8_t)y1,
          idx };
        sendPacket(CMD_DRAWLINE_INDEXED, payload, 7);
        lastLineColor = color;
      }
      else {
        // Full line
        uint8_t payload[8] = { lowByte(x0), highByte(x0), (uint8_t)y0, lowByte(x1), highByte(x1), (uint8_t)y1, lowByte(color), highByte(color) };
        sendPacket(CMD_DRAWLINE, payload, 8);
        lastLineColor = color;
      }
    }
  }
}

// Draw a rectangle
void espdisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  if (console && remote_active)
  {
    static uint8_t sysexDrawRect[13] = {
      0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
      0x0, 0x0, 0x0, 0x0, 0x0, // could be unknown color
    };

    fillSysexData(sysexDrawRect, 4, x, y, w, h);
    uint8_t colors[4];
    uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
    sysexDrawRect[8] = nbBytes;
    memcpy(sysexDrawRect + 8 + 1, colors, nbBytes);

    sendSysEx(98, 8 + 1 + nbBytes, sysexDrawRect, true);
    console = false;
  }

  if (esp_draw) {
    if (color == lastLineColor) {
      uint8_t payload[6];
      payload[0] = lowByte(x);
      payload[1] = highByte(x);
      payload[2] = (uint8_t)y;
      payload[3] = lowByte(w);
      payload[4] = highByte(w);
      payload[5] = (uint8_t)h;
      sendPacket(CMD_DRAWRECT_FAST, payload, 6);
    }
    else {
      uint8_t idx = getColorIndex(color);
      if (idx != 255) {
        // Indexed rect
        uint8_t payload[7] = {
        lowByte(x),
        highByte(x),
        (uint8_t)y,
        lowByte(w),
        highByte(w),
        (uint8_t)h,
        idx };
        sendPacket(CMD_DRAWRECT_INDEXED, payload, 7);
        lastLineColor = color;
      }
      else {
        // Full rect
        uint8_t payload[8] = {
          lowByte(x),
          highByte(x),
          (uint8_t)y,
          lowByte(w),
          highByte(w),
          (uint8_t)h,
          lowByte(color),
          highByte(color) };
        sendPacket(CMD_DRAWRECT, payload, 8);
        lastLineColor = color;
      }
    }
  }
}

// overwrite functions from class Print:

size_t espdisplay::write(uint8_t c)
{
  return write(&c, 1);
}

size_t espdisplay::write(const uint8_t* buffer, size_t size)
{

  if (remote_active)
  {
    uint8_t colors[8];
    uint8_t nbBytes = fillSysexDataColor(colors, 2, textcolor, textbgcolor);

    uint8_t sizeMsg = 2 * 2 + 1 + nbBytes + 1 + size;

    uint8_t* sysexDrawString = (uint8_t*)malloc(sizeMsg);
    if (sysexDrawString == NULL)
    {
#ifdef DEBUG
      LOG.println("malloc failed");
#endif
    }
    else
    {
      fillSysexData(sysexDrawString, 2, cursor_x, cursor_y);
      sysexDrawString[4] = nbBytes;
      memcpy(sysexDrawString + 4 + 1, colors, nbBytes);
      sysexDrawString[4 + 1 + nbBytes] = textsize_x;
      memcpy(sysexDrawString + 4 + 1 + nbBytes + 1, buffer, size);
      sendSysEx(72, sizeMsg, sysexDrawString, true);
      free(sysexDrawString);
    }
    console = false;
  }

  if (esp_draw) {
    bool settingsSame = (textcolor == lastTextFgColor &&
      textbgcolor == lastTextBgColor &&
      textsize_x == lastTextSize);

    if (settingsSame) {
      // Fast string command – length now 1 byte
      uint16_t payloadLen = 4 + size; // x(2), y(1), len(1), string
      uint8_t payload[payloadLen];
      payload[0] = lowByte(cursor_x);
      payload[1] = highByte(cursor_x);
      payload[2] = (uint8_t)cursor_y;
      payload[3] = (uint8_t)size;          // length as single byte
      memcpy(&payload[4], buffer, size);
      sendPacket(CMD_PRINT_STRING_FAST, payload, payloadLen);
    }
    else {
      uint8_t fgIdx = getColorIndex(textcolor);
      uint8_t bgIdx = getColorIndex(textbgcolor);
      bool bothIndexed = (fgIdx != 255 && bgIdx != 255);
      if (bothIndexed) {
        // Indexed string: pack fg/bg into one byte
        uint8_t packed = (fgIdx << 4) | bgIdx;
        uint16_t payloadLen = 6 + size; // x(2), y(1), packed(1), size_x(1), len(1), string
        uint8_t payload[payloadLen];
        payload[0] = lowByte(cursor_x);
        payload[1] = highByte(cursor_x);
        payload[2] = (uint8_t)cursor_y;
        payload[3] = packed;
        payload[4] = textsize_x;
        payload[5] = (uint8_t)size;
        memcpy(&payload[6], buffer, size);
        sendPacket(CMD_PRINT_STRING_INDEXED, payload, payloadLen);
      }
      else {
        // Full string command
        uint16_t payloadLen = 9 + size;
        uint8_t payload[payloadLen];
        payload[0] = lowByte(cursor_x);
        payload[1] = highByte(cursor_x);
        payload[2] = (uint8_t)cursor_y;
        payload[3] = lowByte(textcolor);
        payload[4] = highByte(textcolor);
        payload[5] = lowByte(textbgcolor);
        payload[6] = highByte(textbgcolor);
        payload[7] = textsize_x;
        payload[8] = (uint8_t)size;
        memcpy(&payload[9], buffer, size);
        sendPacket(CMD_PRINT_STRING, payload, payloadLen);
      }
      lastTextFgColor = textcolor;
      lastTextBgColor = textbgcolor;
      lastTextSize = textsize_x;
    }
  }
  size_t cb = size;
  while (cb--) {
    buffer++;
    cursor_x += textsize_x * 6;
  }

  //delayMicroseconds(50);
  return size;
}

FLASHMEM void espdisplay::setCursor(int16_t x, int16_t y, bool autoCenter)
{
  _center_x_text = autoCenter; // remember the state.
  _center_y_text = autoCenter; // remember the state.
  if (x == espdisplay::CENTER)
  {
    _center_x_text = true;
    x = _width / 2;
  }
  if (y == espdisplay::CENTER)
  {
    _center_y_text = true;
    y = _height / 2;
  }
  if (x < 0)
    x = 0;
  else if (x >= _width)
    x = _width - 1;
  cursor_x = x;
  if (y < 0)
    y = 0;
  else if (y >= _height)
    y = _height - 1;
  cursor_y = y;
}

FLASHMEM void espdisplay::getCursor(int16_t* x, int16_t* y)
{
  *x = cursor_x;
  *y = cursor_y;
}

FLASHMEM void espdisplay::setTextSize(uint8_t s_x, uint8_t s_y)
{
  textsize_x = (s_x > 0) ? s_x : 1;
  textsize_y = (s_y > 0) ? s_y : 1;
}

FLASHMEM void espdisplay::setTextColor(uint16_t c)
{
  // For 'transparent' background, we'll set the bg
  // to the same as fg instead of using a flag
  textcolor = textbgcolor = c;
}

FLASHMEM void espdisplay::setTextColor(uint16_t c, uint16_t b)
{
  textcolor = c;
  textbgcolor = b;
  // pre-expand colors for fast alpha-blending later
  textcolorPrexpanded =
    (textcolor | (textcolor << 16)) & 0b00000111111000001111100000011111;
  textbgcolorPrexpanded =
    (textbgcolor | (textbgcolor << 16)) & 0b00000111111000001111100000011111;
}

/***************************************************************************************
** Function name:           drawString (with or without user defined font)
** Description :            draw string with padding if it is defined
***************************************************************************************/
// Without font number, uses font set by setTextFont()
int16_t espdisplay::drawString(const String& string, int poX, int poY)
{
  int16_t len = string.length() + 2;
  char buffer[len];
  string.toCharArray(buffer, len);
  return drawString(buffer, len - 2, poX, poY);
}

int16_t espdisplay::drawString(const char string[], int16_t len, int poX, int poY)
{
  int16_t sumX = 0;

  setCursor(poX, poY);
  for (uint8_t i = 0; i < len; i++)
  {
    drawFontChar(string[i]);
    setCursor(cursor_x, cursor_y);
  }

  return sumX;
}

//////////////////////////////////////////////////////

// send sysex bundled.
FLASHMEM void espdisplay::flushSysEx() {
  if (sysex_len <= SYSEX_HEADER_SIZE)
    return;

  memcpy(sysex_buffer, sysexRenderHeader, SYSEX_HEADER_SIZE); // place sysex MIDI header
  sysex_buffer[sysex_len] = 0xF7; // place sysex end byte

  usbMIDI.sendSysEx(sysex_len + 1, sysex_buffer, true);

  sysex_len = SYSEX_HEADER_SIZE;
}


// append sysex data to buffer for sending in a bundled fashion
// a size field added to split the messages on receiver side.
FLASHMEM void espdisplay::sendSysEx(uint8_t renderCmd, uint8_t length, uint8_t* data, bool hasStartEnd)
{
  if (sysex_len + length > sizeof(sysex_buffer) - 2) // flush if full, keep space for end byte
    flushSysEx();

  if (hasStartEnd)
  {
    sysex_buffer[sysex_len] = length + 1; // store length (with renderCmd byte added) to slice bundled messages on receiver side
    sysex_len++;
    sysex_buffer[sysex_len] = renderCmd;
    sysex_len++;
    memcpy(&sysex_buffer[sysex_len], data, length);
    sysex_len += length;
  }
  else
  {
    // TODO IMPLEMENT or remove hasStartEnd parameter
  }
}

// Fill MIDI sysex data with two 7bits bytes for each value
FLASHMEM void espdisplay::fillSysexData(uint8_t arr[], uint8_t nbArg, ...)
{
  va_list args;
  va_start(args, nbArg);
  for (uint8_t x = 0; x < nbArg; x++)
  {
    int val = va_arg(args, int);
    uint8_t posInArray = x * 2;

    // Convert value to two 7bit bytes for MIDI
    arr[posInArray] = val / 128;
    arr[posInArray + 1] = val % 128;
  }
}

FLASHMEM uint8_t espdisplay::fillSysexDataColor(uint8_t* arr, uint8_t nbArg, ...)
{
  va_list args;
  va_start(args, nbArg);
  int prev_val = -1;

  uint8_t pos = 1; // first byte will be the color mode
  uint8_t colorCodes[2];

  for (uint8_t i = 0; i < nbArg; i++)
  {
    int val = va_arg(args, int);

    uint8_t colorCode = 0;
    switch (val)
    {
    case COLOR_BACKGROUND:
      colorCode = 0;
      break;
    case COLOR_SYSTEXT:
      colorCode = 1;
      break;
    case COLOR_INSTR:
      colorCode = 2;
      break;
    case COLOR_CHORDS:
      colorCode = 3;
      break;
    case COLOR_ARP:
      colorCode = 4;
      break;
    case COLOR_DRUMS:
      colorCode = 5;
      break;
    case COLOR_PITCHSMP:
      colorCode = 6;
      break;

    case RED:
      colorCode = 7;
      break;
    case PINK:
      colorCode = 8;
      break;
    case YELLOW:
      colorCode = 9;
      break;

    case GREEN:
      colorCode = 10;
      break;
    case MIDDLEGREEN:
      colorCode = 11;
      break;
    case DARKGREEN:
      colorCode = 12;
      break;

    case GREY1:
      colorCode = 13;
      break;
    case GREY2:
      colorCode = 14;
      break;
    case GREY3:
      colorCode = 15;
      break;
    case GREY4:
      colorCode = 16;
      break;
      // #define GREY4 0xC638 //only for UI test
    case DX_DARKCYAN:
      colorCode = 17;
      break;
    default:
      colorCode = 255;

      if (val == prev_val)
      {
        colorCode = 0;
      }
      else
      {
        // unknown color => 16 bits color value
        // to send on three 7bits bytes
        arr[pos] = (val >> 14) & 0x7F;
        arr[pos + 1] = (val >> 7) & 0x7F;
        arr[pos + 2] = val & 0x7F;
        pos += 3;
      }
    }

    colorCodes[i] = colorCode;

    // Serial.printf("  colorCodes [%d] %d\n", i, colorCodes[i]);
    if (colorCode != 255)
    {
      // avoid sending known color on 4 bytes, just send a 1 byte code by MIDI
      arr[pos] = colorCode;
      pos++;
    }
    prev_val = val;
  }

  // set color mode
  if (colorCodes[0] < 100)
  {
    if (nbArg == 2)
    {
      arr[0] = colorCodes[1] < 100 ? 0 : 1;
    }
    else
    {
      arr[0] = 0;
    }
  }
  else
  {
    if (nbArg == 2)
    {
      arr[0] = colorCodes[1] < 100 ? 2 : 3;
    }
    else
    {
      arr[0] = 1;
    }
  }

  return pos;
}

#endif