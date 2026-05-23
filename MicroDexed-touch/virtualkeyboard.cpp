#include "virtualkeyboard.h"
#include "touchbutton.h"
#include "UI.h"
#include "sequencer.h"
#include "livesequencer.h"

#include "touch.h"
#include "LCDMenuLib2.h"
#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined (RA_DISPLAY)
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif
#include "drums.h"

extern void checkWebRemote();

static constexpr float KEY_HEIGHT_WHITE = 74;
static constexpr float KEY_LABEL_OFFSET = 9;
static constexpr float KEY_HEIGHT_BLACK = 34;

extern LCDMenuLib2 LCDML;

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined (RA_DISPLAY)
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

extern ts_t ts;
extern sequencer_t seq;
extern uint8_t selected_instance_id; // dexed
extern uint8_t drum_midi_channel;
extern config_t configuration;
extern microsynth_t microsynth[NUM_MICROSYNTH];
extern braids_t braids_osc;
extern uint8_t microsynth_selected_instance;
extern drum_config_t drum_config[NUM_DRUMSET_CONFIG];
extern int numTouchPoints;
extern void set_sample_pitch(uint8_t sample, float playbackspeed);
extern float get_sample_p_offset(uint8_t sample);
extern void handleNoteOn_MIDI_DEVICE_DIN(byte inChannel, byte inNumber, byte inVelocity);
extern void handleNoteOff_MIDI_DEVICE_DIN(byte inChannel, byte inNumber, byte inVelocity);

void printDrumPad(uint8_t padIndex, TouchButton::Color color);
void virtual_keyboard_print_current_instrument(bool drawName = true);
void print_drumpads();
void touch_check_all_keyboard_buttons();
void print_octave_label(void);

bool dark_keyboard;

// Maximum white keys for the largest view (mode 2)
static constexpr uint8_t MAX_WHITE_KEYS = 20;

// Semitone offsets from the base octave for each white key (C=0, D=2, …).
// Only the first N (10/15/20) are used depending on the mode.
static constexpr uint8_t whiteKeyOffsets[MAX_WHITE_KEYS] = {
    0, 2, 4, 5, 7, 9, 11,      // C … B  (octave 0)
    12,14,16,17,19,21,23,     // C … B  (octave +1)
    24,26,28,29,31,33         // C … A  (octave +2, enough for 20 keys)
};

struct KeyboardParams {
  uint8_t  numWhite;
  float    whiteWidth;
  float    whiteSpacing;
  float    blackWidth;
  float    blackHeight;           // absolute height of black keys
};

static const KeyboardParams keyParams[3] = {
  // mode 0 – original 10 keys (unchanged)
  {10, 30.0f, 2.0f, 22.0f, 34.0f},
  // mode 1 – 15 keys (unchanged)
  {15, 19.0f, 2.0f, 13.0f, 34.0f},
  // mode 2 – 20 keys, now exactly fits the display width
  {20, 14.0f, 2.0f, 10.0f, 34.0f}
};

struct KeyboardLayout {
  uint8_t  numWhite;
  float    whiteX[MAX_WHITE_KEYS];
  float    whiteW;
  uint8_t  whiteNote[MAX_WHITE_KEYS];   // semitone offset from base octave
  uint8_t  numBlack;
  float    blackX[MAX_WHITE_KEYS - 1];
  uint8_t  blackNote[MAX_WHITE_KEYS - 1];
  float    blackW;
  float    blackH;
};

static void calculateKeyboardLayout(KeyboardLayout& layout) {
  uint8_t mode = ts.virtual_keyboard_view_mode;
  const KeyboardParams& p = keyParams[mode];
  layout.numWhite = p.numWhite;

  // Convert widths / spacing to exact pixels
  int whiteW = (int)(p.whiteWidth + 0.5f);
  int spacing = (int)(p.whiteSpacing + 0.5f);
  layout.whiteW = (float)whiteW;          // still a float for touch detection
  layout.blackW = p.blackWidth;
  layout.blackH = p.blackHeight;

  // Total required width
  int total = p.numWhite * whiteW + (p.numWhite - 1) * spacing;
  // Centre on the display
  int x = (DISPLAY_WIDTH - total) / 2;
  if (x < 0) x = 0;

  for (uint8_t i = 0; i < p.numWhite; i++) {
    layout.whiteX[i] = (float)x;
    layout.whiteNote[i] = whiteKeyOffsets[i];
    x += whiteW + spacing;                 // identical gap everywhere
  }

  // ---- Black keys (centres remain fine) ----
  layout.numBlack = 0;
  for (uint8_t i = 0; i < p.numWhite - 1; i++) {
    if (layout.whiteNote[i + 1] - layout.whiteNote[i] == 2) {
      float left = layout.whiteX[i] + layout.whiteW;
      float right = layout.whiteX[i + 1];
      float center = (left + right) * 0.5f;
      layout.blackX[layout.numBlack] = center - layout.blackW * 0.5f;
      layout.blackNote[layout.numBlack] = layout.whiteNote[i] + 1;
      layout.numBlack++;
    }
  }
}

FLASHMEM void draw_keyboard_control_buttons(void)
{
  const uint8_t col1 = 2;   // Dark/Bright
  const uint8_t col2 = 3;   // Keyb.Size

  int btnX1 = GRID.X[col1];
  int btnX2 = GRID.X[col2];
  int btnY = GRID.Y[3];
  int btnW = TouchButton::BUTTON_SIZE_X;
  int btnH = TouchButton::BUTTON_SIZE_Y;

  int halfH = btnH / 2;
  int yFill = btnY + halfH;                 // only the lower half is filled

  uint16_t fillColor = GREY3;
  uint16_t textColor = GREY1;

  // ── Left button: Dark / Bright ──────────────────────────────
  checkWebRemote();
  display.fillRect(btnX1, yFill, btnW, halfH, fillColor);
  display.console = false;

  display.setTextSize(1);
  display.setTextColor(textColor, fillColor);

  const char* toggleLabel = dark_keyboard ? "BRIGHT" : "DARK";
  int textW = strlen(toggleLabel) * CHAR_width_small;
  int textX = btnX1 + (btnW - textW) / 2;
  int textY = yFill + halfH / 2 - 4;

  display.setCursor(textX, textY);
  display.print(toggleLabel);
  display.setTextSize(2);

  // ── Right button: Keysize ───────────────────────────────────
  checkWebRemote();
  display.fillRect(btnX2, yFill, btnW, halfH, fillColor);
  display.console = false;

  display.setTextSize(1);
  display.setTextColor(textColor, fillColor);

  const char* sizeLabel = "KEYSIZE";
  textW = strlen(sizeLabel) * CHAR_width_small;
  textX = btnX2 + (btnW - textW) / 2;
  textY = yFill + halfH / 2 - 4;

  display.setCursor(textX, textY);
  display.print(sizeLabel);

print_octave_label();
}


FLASHMEM void print_octave_label(void)
{  
  // Position right of the instrument name, same line
  const int x = 32 * CHAR_width_small + 3;
  const int y = 16 * CHAR_height_small + 4;          // line 16

  display.setTextSize(1);                        // small font
  display.setTextColor(dark_keyboard ? GREY2 : COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setCursor(x, y);
  display.printf(F("OC%d"), ts.virtual_keyboard_octave);

}

FLASHMEM void print_octaves_on_keyboard()
{
  if (ts.current_virtual_keyboard_display_mode == 0) {
    KeyboardLayout layout;
    calculateKeyboardLayout(layout);

    for (uint8_t i = 0; i < layout.numWhite; i++) {
      if (layout.whiteNote[i] % 12 == 0) {
        if (ts.virtual_keyboard_state_white & (1 << i))
          display.setTextColor(COLOR_SYSTEXT, RED);
        else
          display.setTextColor(COLOR_BACKGROUND, dark_keyboard ? GREY3 : COLOR_SYSTEXT);

        constexpr int labelWidth = 2 * CHAR_width_small;
        int labelX = layout.whiteX[i] + (layout.whiteW - labelWidth) / 2;
        display.setCursor(labelX, VIRT_KEYB_YPOS + 57.75);
        display.print("C");
        uint8_t octave = ts.virtual_keyboard_octave + layout.whiteNote[i] / 12;
        display.print(octave);
      }
    }
  }
}

FLASHMEM void drawVirtualKeyboard(uint8_t drawFlags)
{
  if (ts.virtual_keyboard_instrument == VK_LIVESEQUENCER) {
    if (LCDML.FUNC_getID() != LCDML.OTHER_getIDFromFunction(UI_func_livesequencer)) {
      ts.virtual_keyboard_instrument = VK_DEXED0;
    }
    drawFlags &= ~VK_DRAW_INSTRUMENT_BUTTONS;
  }
  else {
    ts.current_virtual_keyboard_display_mode = (ts.virtual_keyboard_instrument == VK_DRUMS) ? 1 : 0;
  }
  const bool modeChanged = ts.current_virtual_keyboard_display_mode != ts.previous_virtual_keyboard_display_mode;
  if (modeChanged) {
    checkWebRemote();
    display.fillRect(0, GRID.Y[4], DISPLAY_WIDTH, DISPLAY_HEIGHT - GRID.Y[4], COLOR_BACKGROUND);
    drawFlags |= VK_DRAW_KEYS | VK_DRAW_OCTAVE;
  }
  ts.previous_virtual_keyboard_display_mode = ts.current_virtual_keyboard_display_mode;

  if (drawFlags & VK_DRAW_OCTAVE_BUTTONS) {
    TouchButton::drawButton(GRID.X[0], GRID.Y[3], "OCTAVE", "-", TouchButton::BUTTON_NORMAL);
    TouchButton::drawButton(GRID.X[5], GRID.Y[3], "OCTAVE", "+", TouchButton::BUTTON_NORMAL);
  }

  if (drawFlags & VK_DRAW_INSTRUMENT_BUTTONS) {
    TouchButton::drawButton(GRID.X[1], GRID.Y[3], "INSTR.", "-", TouchButton::BUTTON_NORMAL);
    TouchButton::drawButton(GRID.X[4], GRID.Y[3], "INSTR.", "+", TouchButton::BUTTON_NORMAL);
  }

  // Instrument name (line 16) and octave label
  if (drawFlags & VK_DRAW_INSTRUMENT_NAME) {
    virtual_keyboard_print_current_instrument(true);
   // print_octave_label();
  }

  display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
  display.setTextSize(1);

  if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) &&
    seq.cycle_touch_element == 1 && seq.step_recording) {
    virtual_keyboard_print_velocity_bar();
  }

  if (drawFlags & VK_DRAW_KEYS) {
    switch (ts.current_virtual_keyboard_display_mode) {
    case 0:
    {
      checkWebRemote();
      display.fillRect(0, VIRT_KEYB_YPOS, DISPLAY_WIDTH, KEY_HEIGHT_WHITE, COLOR_BACKGROUND);
      display.console = false;

      KeyboardLayout layout;
      calculateKeyboardLayout(layout);

      // White keys
      for (uint8_t i = 0; i < layout.numWhite; i++) {
        checkWebRemote();
        display.fillRect(layout.whiteX[i], VIRT_KEYB_YPOS,
          layout.whiteW, KEY_HEIGHT_WHITE,
          dark_keyboard ? GREY3 : COLOR_SYSTEXT);
        display.console = false;
      }

      // Black keys
      for (uint8_t b = 0; b < layout.numBlack; b++) {
        checkWebRemote();
        display.fillRect(layout.blackX[b], VIRT_KEYB_YPOS,
          layout.blackW, layout.blackH, COLOR_BACKGROUND);
        display.console = false;
      }
    }
    break;
    case 1:
      print_drumpads();
      break;
    }
    draw_keyboard_control_buttons();
  }



  // Octave labels on white keys (C notes)
  if (drawFlags & VK_DRAW_OCTAVE) {
    print_octaves_on_keyboard();
   // print_octave_label();
  }
  display.console = false;
}

static FLASHMEM void virtual_keyboard_key_off_white_layout(uint8_t idx, uint8_t note, KeyboardLayout& layout) {
  handleNoteOff_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note, 0);
}

FLASHMEM void virtual_keyboard_key_off_black_layout(uint8_t idx, uint8_t note, KeyboardLayout& layout) {
  handleNoteOff_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note, 0);
}

FLASHMEM void touch_button_oct_up()
{
  if (++ts.virtual_keyboard_octave > 8) {
    ts.virtual_keyboard_octave = 8;
  }
  if (ts.current_virtual_keyboard_display_mode == 1) {
    drawVirtualKeyboard(VK_DRAW_OCTAVE | VK_DRAW_KEYS);
  }
  else {
    drawVirtualKeyboard(VK_DRAW_OCTAVE);
  }
   print_octave_label();
}

FLASHMEM void touch_button_oct_down()
{
  if (--ts.virtual_keyboard_octave == 0) {
    ts.virtual_keyboard_octave = 1;
  }
  if (ts.current_virtual_keyboard_display_mode == 1) {
    drawVirtualKeyboard(VK_DRAW_OCTAVE | VK_DRAW_KEYS);
  }
  else {
    drawVirtualKeyboard(VK_DRAW_OCTAVE);
  }
   print_octave_label();
}

FLASHMEM void touch_button_inst_up()
{
  ts.virtual_keyboard_instrument++;
  if (ts.virtual_keyboard_instrument == VK_DRUMS && ts.virtual_keyboard_octave > 6) {
    ts.virtual_keyboard_octave = 6;
  }
  if (ts.virtual_keyboard_instrument > 14) {
    ts.virtual_keyboard_instrument = 14;
  }

  drawVirtualKeyboard(VK_DRAW_INSTRUMENT_NAME);
}
FLASHMEM void touch_button_inst_down()
{
  if (ts.virtual_keyboard_instrument > 1) {
    ts.virtual_keyboard_instrument--;
  }

  if (ts.virtual_keyboard_instrument == VK_DRUMS && ts.virtual_keyboard_octave > 6) {
    ts.virtual_keyboard_octave = 6;
  }

  drawVirtualKeyboard(VK_DRAW_INSTRUMENT_NAME);
}

FLASHMEM void touch_check_all_keyboard_buttons()
{
  // octave buttons
  if (TouchButton::isPressed(GRID.X[0], GRID.Y[3])) {
    touch_button_oct_down();
  }
  if (TouchButton::isPressed(GRID.X[5], GRID.Y[3])) {
    touch_button_oct_up();
  }

  // Dark/bright toggle
  if (TouchButton::isPressed(GRID.X[2], GRID.Y[3])) {
    dark_keyboard = !dark_keyboard;
    drawVirtualKeyboard(VK_DRAW_KEYS | VK_DRAW_OCTAVE);
  }

  // Keyboard size toggle
  if (TouchButton::isPressed(GRID.X[3], GRID.Y[3])) {
    ts.virtual_keyboard_view_mode = (ts.virtual_keyboard_view_mode + 1) % 3;
    drawVirtualKeyboard(VK_DRAW_KEYS | VK_DRAW_OCTAVE);
  }

  // instrument buttons (disabled for livesequencer)
  if (ts.virtual_keyboard_instrument != 0) {
    if (TouchButton::isPressed(GRID.X[1], GRID.Y[3])) {
      touch_button_inst_down();
    }
    if (TouchButton::isPressed(GRID.X[4], GRID.Y[3])) {
      touch_button_inst_up();
    }
  }
}

#ifdef GRANULAR
#include "granular.h"
extern granular_params_t granular_params;
#endif

FLASHMEM void virtual_keyboard_print_current_instrument(bool drawName)
{
  // Update MIDI channel assignment (always runs)
  switch (ts.virtual_keyboard_instrument) {
  case VK_LIVESEQUENCER:
    ts.virtual_keyboard_midi_channel = 20;
    break;
  case VK_GRANULAR:
#ifdef GRANULAR
    ts.virtual_keyboard_midi_channel = granular_params.midi_channel;
#endif
    break;
  case VK_CV1:
    ts.virtual_keyboard_midi_channel = configuration.sys.cv_midi_channel;
    break;
  case VK_DEXED0:
    ts.virtual_keyboard_midi_channel = configuration.dexed[0].midi_channel;
    break;
  case VK_DEXED1:
    ts.virtual_keyboard_midi_channel = configuration.dexed[1].midi_channel;
    break;
  case VK_DEXED2:
    if (NUM_DEXED > 2) ts.virtual_keyboard_midi_channel = configuration.dexed[2].midi_channel;
    break;
  case VK_DEXED3:
    if (NUM_DEXED > 2) ts.virtual_keyboard_midi_channel = configuration.dexed[3].midi_channel;
    break;
  case VK_MICROSYNTH0:
    ts.virtual_keyboard_midi_channel = microsynth[0].midi_channel;
    break;
  case VK_MICROSYNTH1:
    ts.virtual_keyboard_midi_channel = microsynth[1].midi_channel;
    break;
  case VK_EPIANO:
    ts.virtual_keyboard_midi_channel = configuration.epiano.midi_channel;
    break;
  case VK_DRUMS:
    ts.virtual_keyboard_midi_channel = drum_midi_channel;
    break;
  case VK_BRAIDS:
    ts.virtual_keyboard_midi_channel = braids_osc.midi_channel;
    break;
  default: // >9
    ts.virtual_keyboard_midi_channel = drum_midi_channel;
    break;
  }

  if (drawName) {
    display.setTextColor(dark_keyboard ? GREY2 : COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(2);
    // Now at line 16 instead of 18
    display.setCursor(18 * CHAR_width_small, 16 * CHAR_height_small);

    switch (ts.virtual_keyboard_instrument) {
    case VK_LIVESEQUENCER:
      display.print(F("LIVESEQ"));
      break;
    case VK_GRANULAR:
      display.print(F("GRANULE"));
      break;
    case VK_CV1:
      display.print(F("CV1 OUT"));
      break;
    case VK_DEXED0:
      display.print(F("DEXED1 "));
      break;
    case VK_DEXED1:
      display.print(F("DEXED2 "));
      break;
    case VK_DEXED2:
      display.print(F("DEXED3 "));
      break;
    case VK_DEXED3:
      display.print(F("DEXED4 "));
      break;
    case VK_MICROSYNTH0:
      display.print(F("MSYNTH1"));
      break;
    case VK_MICROSYNTH1:
      display.print(F("MSYNTH2"));
      break;
    case VK_EPIANO:
      display.print(F("EPIANO "));
      break;
    case VK_DRUMS:
      display.print(F("DRUMS  "));
      break;
    case VK_BRAIDS:
      display.print(F("BRAIDS "));
      break;
    default: // >9
      show_no_grid(16 * CHAR_height_small, 18 * CHAR_width_small, 7,
        get_drum_name_from_note(ts.virtual_keyboard_instrument - 8 + 210));
      break;
    }
  }
}

FLASHMEM void printDrumPad(uint8_t padIndex, TouchButton::Color color) {
  static constexpr uint8_t LINECHARS = 6;
  static constexpr uint8_t offset = 18;
  std::string line1;
  std::string line2;
  const int16_t sampleIndex = padIndex + ts.virtual_keyboard_octave * 12 - offset;
  if (sampleIndex >= 0 && sampleIndex < NUM_DRUMSET_CONFIG) {
    line1 = std::string(&drum_config[sampleIndex].name[0], LINECHARS);
    line2 = std::string(&drum_config[sampleIndex].name[LINECHARS], LINECHARS);
  }
  TouchButton::drawButton(GRID.X[padIndex % 6], GRID.Y[4 + (padIndex / 6)], line1, line2, color);
}

FLASHMEM void print_drumpads() {
  TouchButton::Color padColor = dark_keyboard ? TouchButton::BUTTON_NORMAL : TouchButton::BUTTON_ACTIVE;
  for (uint8_t x = 0; x < 12; x++) {
    printDrumPad(x, padColor);
  }
}

FLASHMEM void clear_inst_header()
{
  checkWebRemote();
  display.fillRect(18 * CHAR_width_small - 1, 16 * CHAR_height_small - 1,
    18 * CHAR_width_small - 2, 4 * CHAR_height_small + 3,
    COLOR_BACKGROUND);
  display.console = false;
}
FLASHMEM void handleTouchVirtualKeyboard()
{

  touch_check_all_keyboard_buttons();

  bool isPressed = numTouchPoints > 0;

  // Redraw the keyboard if the help overlay has expired
  // (also removed – no help overlay anymore)

  if (ts.current_virtual_keyboard_display_mode == 0)
  {
    KeyboardLayout layout;
    calculateKeyboardLayout(layout);

    display.setTextColor(COLOR_SYSTEXT);
    display.setTextSize(1);

    // ---- White keys ----
    bool isWithinWhiteY = ts.p.y > VIRT_KEYB_YPOS + 36;   // original lower‑half threshold
    for (uint8_t i = 0; i < layout.numWhite; i++) {
      bool isWithinKey = isPressed && ts.p.x > layout.whiteX[i]
        && ts.p.x < (layout.whiteX[i] + layout.whiteW) && isWithinWhiteY;
      bool isKeyPress = (ts.virtual_keyboard_state_white & (1 << i)) == 0 && isWithinKey;
      bool isKeyRelease = (ts.virtual_keyboard_state_white & (1 << i)) != 0 && !isWithinKey;

      if (isKeyPress) {
        ts.virtual_keyboard_state_white |= (1 << i);
        uint8_t note = ts.virtual_keyboard_octave * 12 + layout.whiteNote[i];

        // -------- EXACT instrument logic from original code --------
        if (ts.virtual_keyboard_instrument > VK_GRANULAR && ts.virtual_keyboard_instrument != VK_CV1) {
          set_sample_pitch(ts.virtual_keyboard_instrument - 8,
            (float)pow(2, (note - 72) / 12.00) * get_sample_p_offset(ts.virtual_keyboard_instrument - 8));
          if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) && seq.cycle_touch_element == 1) ||
            (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor) && seq.cycle_touch_element == 1)) {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, 210 + ts.virtual_keyboard_instrument - 8, ts.virtual_keyboard_velocity);
          }
          else {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, 210 + ts.virtual_keyboard_instrument - 8, 100);
          }
        }
        else if (ts.virtual_keyboard_instrument == VK_CV1) {
          handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note + 12, 120);
        }
        else {
          if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) && seq.cycle_touch_element == 1) ||
            (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor) && seq.cycle_touch_element == 1)) {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note, ts.virtual_keyboard_velocity);
          }
          else {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note, 120);
          }
        }

        // Draw full white key red
        checkWebRemote();
        display.fillRect(layout.whiteX[i], VIRT_KEYB_YPOS,
          layout.whiteW, KEY_HEIGHT_WHITE, RED);

        // Restore overlapping black keys **using rectangle intersection**
        float whiteLeft = layout.whiteX[i];
        float whiteRight = whiteLeft + layout.whiteW;
        for (uint8_t b = 0; b < layout.numBlack; b++) {
          float blackLeft = layout.blackX[b];
          float blackRight = blackLeft + layout.blackW;
          if (blackLeft < whiteRight && blackRight > whiteLeft) {
            if (ts.virtual_keyboard_state_black & (1 << b)) {
              display.fillRect(layout.blackX[b], VIRT_KEYB_YPOS,
                layout.blackW, layout.blackH, RED);
            }
            else {
              display.fillRect(layout.blackX[b], VIRT_KEYB_YPOS,
                layout.blackW, layout.blackH, COLOR_BACKGROUND);
            }
          }
        }
        display.console = false;
      }
      else if (isKeyRelease) {
        ts.virtual_keyboard_state_white &= ~(1 << i);
        checkWebRemote();
        display.fillRect(layout.whiteX[i], VIRT_KEYB_YPOS,
          layout.whiteW, KEY_HEIGHT_WHITE,
          dark_keyboard ? GREY3 : COLOR_SYSTEXT);

        // Restore overlapping black keys (same rectangle intersection)
        float whiteLeft = layout.whiteX[i];
        float whiteRight = whiteLeft + layout.whiteW;
        for (uint8_t b = 0; b < layout.numBlack; b++) {
          float blackLeft = layout.blackX[b];
          float blackRight = blackLeft + layout.blackW;
          if (blackLeft < whiteRight && blackRight > whiteLeft) {
            if (ts.virtual_keyboard_state_black & (1 << b)) {
              display.fillRect(layout.blackX[b], VIRT_KEYB_YPOS,
                layout.blackW, layout.blackH, RED);
            }
            else {
              display.fillRect(layout.blackX[b], VIRT_KEYB_YPOS,
                layout.blackW, layout.blackH, COLOR_BACKGROUND);
            }
          }
        }
        display.console = false;
        virtual_keyboard_key_off_white_layout(i, ts.virtual_keyboard_octave * 12 + layout.whiteNote[i], layout);
      }
      if (isKeyPress || isKeyRelease) {
        drawVirtualKeyboard(VK_DRAW_OCTAVE);
      }
    }

    // ---- Black keys ----
    bool isWithinBlackY = ts.p.y > VIRT_KEYB_YPOS && ts.p.y < VIRT_KEYB_YPOS + 34;
    for (uint8_t b = 0; b < layout.numBlack; b++) {
      bool isWithinKey = isPressed && ts.p.x > layout.blackX[b]
        && ts.p.x < (layout.blackX[b] + layout.blackW) && isWithinBlackY;
      bool isKeyPress = (ts.virtual_keyboard_state_black & (1 << b)) == 0 && isWithinKey;
      bool isKeyRelease = (ts.virtual_keyboard_state_black & (1 << b)) != 0 && !isWithinKey;

      if (isKeyPress) {
        ts.virtual_keyboard_state_black |= (1 << b);
        uint8_t note = ts.virtual_keyboard_octave * 12 + layout.blackNote[b];

        // -------- SAME instrument logic --------
        if (ts.virtual_keyboard_instrument > VK_GRANULAR && ts.virtual_keyboard_instrument != VK_CV1) {
          set_sample_pitch(ts.virtual_keyboard_instrument - 8,
            (float)pow(2, (note - 72) / 12.00) * get_sample_p_offset(ts.virtual_keyboard_instrument - 8));
          if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) && seq.cycle_touch_element == 1) ||
            (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor) && seq.cycle_touch_element == 1)) {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, 210 + ts.virtual_keyboard_instrument - 8, ts.virtual_keyboard_velocity);
          }
          else {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, 210 + ts.virtual_keyboard_instrument - 8, 100);
          }
        }
        else if (ts.virtual_keyboard_instrument == VK_CV1) {
          handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note + 12, 120);
        }
        else {
          if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) && seq.cycle_touch_element == 1) ||
            (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor) && seq.cycle_touch_element == 1)) {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note, ts.virtual_keyboard_velocity);
          }
          else {
            handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, note, 120);
          }
        }

        checkWebRemote();
        display.fillRect(layout.blackX[b], VIRT_KEYB_YPOS,
          layout.blackW, layout.blackH, RED);
        display.console = false;
      }
      else if (isKeyRelease) {
        ts.virtual_keyboard_state_black &= ~(1 << b);
        checkWebRemote();
        display.fillRect(layout.blackX[b], VIRT_KEYB_YPOS,
          layout.blackW, layout.blackH, COLOR_BACKGROUND);
        display.console = false;

        virtual_keyboard_key_off_black_layout(b, ts.virtual_keyboard_octave * 12 + layout.blackNote[b], layout);
      }
    }
  }

  else if (ts.current_virtual_keyboard_display_mode == 1) {
    if (ts.virtual_keyboard_instrument == VK_DRUMS || ts.virtual_keyboard_instrument == 0) {
      TouchButton::Color normalColor = dark_keyboard ? TouchButton::BUTTON_NORMAL : TouchButton::BUTTON_ACTIVE;

      for (uint8_t x = 0; x < 12; x++) {
        const bool isWithinPad = TouchButton::isInArea(GRID.X[x % 6], GRID.Y[4 + (x / 6)], TouchButton::BUTTON_SIZE_X, TouchButton::BUTTON_SIZE_Y);
        const bool isPadPress = (ts.virtual_keyboard_state_white & (1 << x)) == 0 && isWithinPad;
        const bool isPadRelease = (ts.virtual_keyboard_state_white & (1 << x)) != 0 && !isWithinPad;
        const uint8_t note = x + ts.virtual_keyboard_octave * 12 - 18;

        if (isPadPress) {
          printDrumPad(x, TouchButton::BUTTON_HIGHLIGHTED);
          ts.virtual_keyboard_state_white |= (1 << x);
          handleNoteOn_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, drum_config[note].midinote, ts.virtual_keyboard_velocity);
        }
        if (isPadRelease) {
          printDrumPad(x, normalColor);
          ts.virtual_keyboard_state_white &= ~(1 << x);
          handleNoteOff_MIDI_DEVICE_DIN(ts.virtual_keyboard_midi_channel, drum_config[note].midinote, ts.virtual_keyboard_velocity);
        }
      }
    }
  }

  display.setTextSize(2);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.console = false;
}

FLASHMEM void virtual_keyboard_print_velocity_bar()
{
  // velocity bar disabled

  // if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) && seq.step_recording == false && seq.cycle_touch_element == 1) ||
  //   (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor) && seq.step_recording == false && seq.cycle_touch_element == 1))
  // {
  //   display.console = 1;
  //   display.fillRect(0, 10 * CHAR_height_small, DISPLAY_WIDTH, 40, COLOR_BACKGROUND);
  //   display.console = 0;
  // } else

  if (seq.cycle_touch_element == 1)
  {
    // velocity bar enabled
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(1);
    display.setCursor(0, 11 * CHAR_height_small + 1);
    display.print(F("VELOCITY"));
    display.setCursor(0, 12 * CHAR_height_small + 3);
    print_formatted_number(ts.virtual_keyboard_velocity, 3);
    display.console = 1;
    display.drawRect(CHAR_width_small * 9 - 1, 11 * CHAR_height_small, CHAR_width * 17 - CHAR_width_small * 10 + 4, 20, GREY1);
    display.console = 1;
    display.fillRect(CHAR_width_small * 9, 11 * CHAR_height_small + 2, ts.virtual_keyboard_velocity * 1.140 + 1, 16, COLOR_PITCHSMP);
    if (ts.virtual_keyboard_velocity < 127 && ts.virtual_keyboard_velocity != 0)
    {
      display.console = 1;
      display.fillRect(CHAR_width_small * 9 + ts.virtual_keyboard_velocity * 1.142 + 1, 11 * CHAR_height_small + 2,
        CHAR_width * 17 - CHAR_width_small * 10 + 2 - ts.virtual_keyboard_velocity * 1.142, 16, GREY3);
      display.console = 0;
    }
  }
}

FLASHMEM void virtual_keyboard_smart_preselect_mode()
{
  //tying to auto select mode 

  if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) ||
    LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor))
  {
    if (seq.content_type[seq.active_pattern] == 0)
    {
      ts.current_virtual_keyboard_display_mode = 1;
      ts.virtual_keyboard_instrument = VK_DRUMS;
    }
    else
    {
      if (seq.content_type[seq.active_pattern] != 0)
      {
        ts.current_virtual_keyboard_display_mode = 0;
        ts.virtual_keyboard_instrument = 1;
      }
    }
  }
  else if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_voice_select))
  {
    ts.current_virtual_keyboard_display_mode = 0;
    if (seq.cycle_touch_element == 0)
      ts.virtual_keyboard_instrument = selected_instance_id + 1;
  }
  else if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_braids))
  {
    ts.current_virtual_keyboard_display_mode = 0;
    ts.virtual_keyboard_instrument = VK_BRAIDS;
  }
  else if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_microsynth))
  {
    ts.current_virtual_keyboard_display_mode = 0;
    ts.virtual_keyboard_instrument = microsynth_selected_instance + 5;
  }
  else if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_cv_page))
  {
    ts.current_virtual_keyboard_display_mode = 0;
    ts.virtual_keyboard_instrument = 14;
  }

}
