#include "config.h"

#ifdef MINI_ENCODER

#include "i2c_encoders.h"
#include <Wire.h>
#include <LCDMenuLib2.h>

#include "UI.h"
#include "touch.h"
#include "sequencer.h"

// ---- externals from the main sketch ----------------------------------
extern LCDMenuLib2 LCDML;

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined RA_DISPLAY
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

extern config_t     configuration;
extern sequencer_t  seq;
extern uint8_t      selected_instance_id;
// ----------------------------------------------------------------------

// The two physical encoders
i2cEncoderMiniLib enc1(I2C_ENC_L_ADDR);   // 0x24
i2cEncoderMiniLib enc2(I2C_ENC_R_ADDR);   // 0x25

// Bus scan results
uint8_t encoder_detected_0x20 = 0;
uint8_t encoder_detected_0x24 = 0;
uint8_t encoder_detected_0x25 = 0;

// Kept for source compatibility; the new flow does not use it.
volatile uint8_t encoder_setup_requested_addr = 0;

// -------- weak callback stubs (override in your sketch if you want) ----
__attribute__((weak)) void onEncoder1Increment(i2cEncoderMiniLib*) {}
__attribute__((weak)) void onEncoder1Decrement(i2cEncoderMiniLib*) {}
__attribute__((weak)) void onEncoder2Increment(i2cEncoderMiniLib*) {}
__attribute__((weak)) void onEncoder2Decrement(i2cEncoderMiniLib*) {}
__attribute__((weak)) void onEncoder1Push(i2cEncoderMiniLib*) {}
// ----------------------------------------------------------------------

// =====================================================================
//  Direct register access to the I2C Encoder Mini
// =====================================================================
static constexpr uint8_t ENC_REG_GCONF   = 0x00;
static constexpr uint8_t ENC_REG_INTCONF = 0x01;
static constexpr uint8_t ENC_REG_ESTATUS = 0x02;
static constexpr uint8_t ENC_REG_I2CADDR = 0x72;

static constexpr uint8_t ENC_ST_PUSHP = 0x02;
static constexpr uint8_t ENC_ST_PUSHL = 0x08;

static uint8_t _enc_read_reg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0xFF;
  return Wire.available() ? Wire.read() : 0xFF;
}

static bool _enc_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// =====================================================================
//  Low-level bus helpers
// =====================================================================
static bool i2c_probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void encoder_rescan_bus(void) {
  encoder_detected_0x20 = i2c_probe(I2C_ENC_DEFAULT) ? 1 : 0;
  encoder_detected_0x24 = i2c_probe(I2C_ENC_L_ADDR)  ? 1 : 0;
  encoder_detected_0x25 = i2c_probe(I2C_ENC_R_ADDR)  ? 1 : 0;
}
// ----------------------------------------------------------------------

// ---- normal initialisation (only when addresses are correct) ---------
static void _apply_encoder_config(void) {
  enc1.begin(i2cEncoderMiniLib::WRAP_DISABLE);
  enc2.begin(i2cEncoderMiniLib::WRAP_DISABLE);
  enc1.autoconfigInterrupt();
  enc2.autoconfigInterrupt();

  enc1.writeCounter(seq.bpm);
  enc2.writeCounter(configuration.dexed[selected_instance_id].sound_intensity);

  enc1.onIncrement  = onEncoder1Decrement;   // reversed
  enc1.onDecrement  = onEncoder1Increment;   // reversed
  enc2.onIncrement  = onEncoder2Decrement;   // reversed
  enc2.onDecrement  = onEncoder2Increment;   // reversed

  enc1.onButtonPush = onEncoder1Push;
}

bool i2c_encoders_setup(void) {
  encoder_rescan_bus();

  if (!encoder_detected_0x20 && encoder_detected_0x24 && encoder_detected_0x25) {
    _apply_encoder_config();
    return false;
  }
  return true;
}
// ----------------------------------------------------------------------

// =====================================================================
//  UI state machine
// =====================================================================
enum EncSetupPhase : uint8_t {
  ENC_SETUP_CONFIRM = 0,
  ENC_SETUP_SELECT,
  ENC_SETUP_BUTTON,
  ENC_SETUP_DONE,
};

static EncSetupPhase       enc_setup_phase       = ENC_SETUP_CONFIRM;
static uint8_t             enc_setup_source_addr = 0;
static uint8_t             enc_setup_target_addr = 0;
static uint8_t             enc_setup_select_idx  = 0;   // 0 -> 0x24, 1 -> 0x25
static bool                enc_setup_is_reset    = false;
static bool                enc_setup_page_active = false;

// ---------------------------------------------------------------------
// Layout constants (small-font grid: 1 unit = 6 x 10 px)
// ---------------------------------------------------------------------
static constexpr uint16_t ENC_SETUP_TITLE_X_PX = 6;
static constexpr uint16_t ENC_SETUP_TITLE_Y_PX = 17;

static constexpr uint8_t  ENC_SETUP_COL_TEXT   = 1;
static constexpr uint8_t  ENC_SETUP_COL_STATUS = 25;
static constexpr uint8_t  ENC_SETUP_ROW_HEADER = 4;
static constexpr uint8_t  ENC_SETUP_ROW_ADDR0  = 6;
static constexpr uint16_t ENC_SETUP_ACTION_Y_PX = 92;

// ---- back-touch button helper ---------------------------------------
static void _enc_setup_draw_back_button(void) {
  TouchButton::drawButton(GRID.X[0], GRID.Y[5], "GO", back_text,
                          TouchButton::BUTTON_NORMAL);
}

// Return the address of the currently assignable encoder, or 0 if none.
static uint8_t _enc_setup_find_source(void) {
  if (encoder_detected_0x20) return 0x20;
  if (encoder_detected_0x24 && !encoder_detected_0x25) return 0x24;
  if (encoder_detected_0x25 && !encoder_detected_0x24) return 0x25;
  return 0;
}

static void _enc_setup_find_targets(uint8_t source, bool& can_24, bool& can_25) {
  can_24 = false;
  can_25 = false;
  if (source == 0) return;
  if (source == 0x20) {
    can_24 = !encoder_detected_0x24;
    can_25 = !encoder_detected_0x25;
  } else if (source == 0x24) {
    can_25 = !encoder_detected_0x25;
  } else if (source == 0x25) {
    can_24 = !encoder_detected_0x24;
  }
}

// "First pass" = neither target address is taken yet.
static bool _enc_setup_is_first_pass(void) {
  return !encoder_detected_0x24 && !encoder_detected_0x25;
}

// =====================================================================
//  Drawing helpers
// =====================================================================
static void _enc_setup_draw_confirm(void);
static void _enc_setup_draw_select(void);
static void _enc_setup_draw_select_buttons(void);
static void _enc_setup_draw_button(void);
static void _enc_setup_draw_done(void);
static void _enc_setup_draw_action(void);
static void _enc_setup_start_assignment(uint8_t target);
static void _enc_setup_do_reset(uint8_t source);
static void _enc_setup_begin_button_wait(void);

static void _enc_setup_draw_title(void) {
  display.setTextSize(2);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setCursor(ENC_SETUP_TITLE_X_PX, ENC_SETUP_TITLE_Y_PX);
  display.print(F("I2C ENCODER SETUP"));
}

static void _enc_setup_draw_static(void) {
  display.setTextSize(1);

  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, ENC_SETUP_ROW_HEADER);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.print(F("I2C BUS SCAN:"));

  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, ENC_SETUP_ROW_ADDR0 + 0);
  display.print(F("ADDRESS 0X20 (DEFAULT):"));
  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, ENC_SETUP_ROW_ADDR0 + 1);
  display.print(F("ADDRESS 0X24 (ENC #1):"));
  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, ENC_SETUP_ROW_ADDR0 + 2);
  display.print(F("ADDRESS 0X25 (ENC #2):"));
}

static void _enc_setup_draw_status(void) {
  display.setTextSize(1);

  // Clear the status area first so shorter new values cannot leave
  // trailing characters from old values.
//   display.fillRect(ENC_SETUP_COL_STATUS * 6, ENC_SETUP_ROW_ADDR0 * 10,
//                    8 * 6, 3 * 10, COLOR_BACKGROUND);

  setCursor_textGrid_small(ENC_SETUP_COL_STATUS, ENC_SETUP_ROW_ADDR0 + 0);
  display.setTextSize(1);
  display.setTextColor(encoder_detected_0x20 ? GREEN : GREY2, COLOR_BACKGROUND);
  display.print(encoder_detected_0x20 ? F("FOUND  ") : F("       "));

  setCursor_textGrid_small(ENC_SETUP_COL_STATUS, ENC_SETUP_ROW_ADDR0 + 1);
  display.setTextSize(1);
  display.setTextColor(encoder_detected_0x24 ? GREEN : RED, COLOR_BACKGROUND);
  display.print(encoder_detected_0x24 ? F("FOUND  ") : F("MISSING"));

  setCursor_textGrid_small(ENC_SETUP_COL_STATUS, ENC_SETUP_ROW_ADDR0 + 2);
  display.setTextSize(1);
  display.setTextColor(encoder_detected_0x25 ? GREEN : RED, COLOR_BACKGROUND);
  display.print(encoder_detected_0x25 ? F("FOUND  ") : F("MISSING"));
}

// Blank the status area entirely. Used on the DONE screen after an
// address change, where the bus state is not reliable until reboot.
static void _enc_setup_clear_status(void) {
  display.fillRect(ENC_SETUP_COL_STATUS * 6, ENC_SETUP_ROW_ADDR0 * 10,
                   8 * 6, 3 * 10, COLOR_BACKGROUND);
}

static void _enc_setup_rescan_and_draw_status(void) {
  encoder_rescan_bus();
  _enc_setup_draw_status();
}

static void _enc_setup_clear_action_area(void) {
  display.fillRect(0, ENC_SETUP_ACTION_Y_PX,
                   DISPLAY_WIDTH, DISPLAY_HEIGHT - ENC_SETUP_ACTION_Y_PX,
                   COLOR_BACKGROUND);
  _enc_setup_draw_back_button();
}

// ---- shared step transitions ----------------------------------------
static void _enc_setup_do_confirm(void) {
  if (!encoder_detected_0x20) return;
  uint8_t src = _enc_setup_find_source();
  if (src == 0) return;
  enc_setup_source_addr = src;
  enc_setup_select_idx  = 0;
  enc_setup_is_reset    = false;
  enc_setup_phase       = ENC_SETUP_SELECT;
  _enc_setup_draw_select();
}

static void _enc_setup_begin_button_wait(void) {
  enc_setup_phase = ENC_SETUP_BUTTON;

  const uint8_t a = enc_setup_source_addr;

  // Enable interrupt generation for push / long-push, then clear any
  // pending status so we only see fresh presses.
  //
  // NOTE: We deliberately do NOT write GCONF.RESET (0x80) here.
  // On the I2C Encoder Mini, the RESET bit can cause the chip to reload
  // its I2C address from EEPROM. If the encoder currently lives at 0x24
  // or 0x25 but its EEPROM still says 0x20, the reset silently moves the
  // device back to 0x20. Every subsequent read at 0x24/0x25 then NAKs,
  // and the button press is never seen — which is exactly what happens
  // during a "reset to default" flow.
  //
  // We only need the button event, not a chip reset, so skip it.
  _enc_write_reg(a, ENC_REG_INTCONF, ENC_ST_PUSHP | ENC_ST_PUSHL);
  (void)_enc_read_reg(a, ENC_REG_ESTATUS);  // clear stale flags

  _enc_setup_draw_button();
}

static void _enc_setup_start_assignment(uint8_t target) {
  enc_setup_target_addr = target;
  enc_setup_is_reset    = false;
  _enc_setup_begin_button_wait();
}

static void _enc_setup_do_reset(uint8_t source) {
  if (source != I2C_ENC_L_ADDR && source != I2C_ENC_R_ADDR) return;

  enc_setup_source_addr = source;
  enc_setup_target_addr = I2C_ENC_DEFAULT;
  enc_setup_is_reset    = true;
  _enc_setup_begin_button_wait();
}

// Leaves DONE (reset variant) and returns to CONFIRM with fresh state.
static void _enc_setup_return_to_confirm(void) {
  enc_setup_is_reset    = false;
  enc_setup_source_addr = 0;
  enc_setup_target_addr = 0;
  _enc_setup_rescan_and_draw_status();
  enc_setup_phase       = ENC_SETUP_CONFIRM;
  _enc_setup_draw_action();
}

// ----- CONFIRM phase --------------------------------------------------
static void _enc_setup_draw_confirm(void) {
  _enc_setup_clear_action_area();

  const bool have20 = encoder_detected_0x20;
  const bool have24 = encoder_detected_0x24;
  const bool have25 = encoder_detected_0x25;

  if (!have20 && !have24 && !have25) {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.print(F("NO I2C ENCODER DETECTED."));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("CONNECT ONE I2C ENCODER AND POWER-CYCLE."));
    helptext_r(F("CONNECT ONE I2C ENCODER"));
    return;
  }

  if (have20 && have24 && have25) {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.print(F("CONFLICT: ALL THREE ADDRESSES RESPOND."));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("DISCONNECT ALL BUT ONE AND POWER-CYCLE."));
    helptext_r(F("DISCONNECT ALL BUT ONE"));
    return;
  }

  if (_enc_setup_find_source() == 0 && !have24 && !have25 ) {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.print(F("UNRESOLVABLE I2C ENCODER STATE."));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("DISCONNECT ALL AND POWER-CYCLE."));
    helptext_r(F("RECONNECT ONE AT A TIME"));
    return;
  }

  if (!have20 && have24 && have25) {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(GREEN, COLOR_BACKGROUND);
    display.print(F("BOTH I2C ENCODERS ARE SET UP CORRECTLY."));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("POWER-CYCLE OR RESET AN ENCODER BELOW."));
  } else if (!have20) {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(COLOR_ARP, COLOR_BACKGROUND);
    display.print(F("WAITING FOR SECOND I2C ENCODER"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("CONNECT IT AT 0X20 OR RESET BELOW."));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 13);
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    if (have24) {
      display.print(F("I2C-ENC #1 IS CURRENTLY AT 0X24."));
    } else {
      display.print(F("I2C-ENC #2 IS CURRENTLY AT 0X25."));
    }
  } else if (_enc_setup_is_first_pass()) {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(COLOR_ARP, COLOR_BACKGROUND);
    display.print(F("STEP 1 OF 3: PREPARE THE SETUP"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("DISCONNECT ALL BUT ONE I2C ENCODER, THEN"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 12);
    display.print(F("TOUCH BUTTON OR PUSH ENC_R:"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 14);
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.print(F("I2C CANNOT DETECT TWO DEVICES WITH THE SAME"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 15);
    display.print(F("ADDRESS. ONLY ONE I2C ENCODER MAY BE CONNECTED."));
  } else {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(COLOR_ARP, COLOR_BACKGROUND);
    display.print(F("STEP 1 OF 3: READY TO CONTINUE"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("THE ALREADY-ASSIGNED I2C ENCODER CAN STAY"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 12);
    display.print(F("CONNECTED. THE REMAINING I2C ENCODER WILL"));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 13);
    display.print(F("NOW BE ASSIGNED."));
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 15);
    display.print(F("TOUCH BUTTON OR PUSH ENC_R TO CONTINUE."));
  }

  // Show "proceed" button whenever a 0x20 encoder is available.
  // Label reflects what pressing it will actually do.
  if (have20) {
    const char* btn_l1;
    const char* btn_l2;
    if (_enc_setup_is_first_pass()) {
      btn_l1 = "PROCEED";
      btn_l2 = "SETUP";
    } else {
      btn_l1 = "ASSIGN";
      btn_l2 = "2ND ENC";
    }
    TouchButton::drawButton(GRID.X[0], GRID.Y[4], btn_l1, btn_l2,
                            TouchButton::BUTTON_ACTIVE);
  }

  // Always show the reset buttons whenever the corresponding encoder
  // is detected at 0x24 / 0x25 — regardless of whether 0x20 is occupied.
  if (have24) {
    TouchButton::drawButton(GRID.X[3], GRID.Y[4], "RESET", "#1 0X20",
                            TouchButton::BUTTON_NORMAL);
  }
  if (have25) {
    TouchButton::drawButton(GRID.X[4], GRID.Y[4], "RESET", "#2 0X20",
                            TouchButton::BUTTON_NORMAL);
  }

  // Help text reflects whichever buttons are actually on screen.
  if (have20 && (have24 || have25)) {
    helptext_r(F("TOUCH ENC OR RESET TO 0X20"));
  } else if (have20) {
    helptext_r(F("TOUCH OR PUSH ENC_R"));
  } else if (have24 || have25) {
    helptext_r(F("TOUCH RESET TO RESTORE 0X20"));
  }
}

// ----- BUTTON phase ---------------------------------------------------
static void _enc_setup_draw_button(void) {
  _enc_setup_clear_action_area();
  display.setTextSize(1);
  char buf[48];

  if (enc_setup_is_reset) {
    const uint8_t enc_num = (enc_setup_source_addr == I2C_ENC_L_ADDR) ? 1 : 2;

    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(COLOR_ARP, COLOR_BACKGROUND);
    snprintf_P(buf, sizeof(buf), PSTR("STEP 3 OF 3: RESET I2C-ENC #%d"), enc_num);
    display.print(buf);

    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("PRESS THE PHYSICAL BUTTON ON I2C-ENC"));

    snprintf_P(buf, sizeof(buf), PSTR("#%d TO RESET IT TO 0X20."), enc_num);
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 12);
    display.print(buf);

    snprintf_P(buf, sizeof(buf), PSTR("CURRENT ADDRESS: 0X%02X."), enc_setup_source_addr);
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 13);
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.print(buf);
  } else {
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(COLOR_ARP, COLOR_BACKGROUND);
    display.print(F("STEP 3 OF 3: CONFIRM ON THE I2C ENCODER"));

    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.print(F("PRESS THE PHYSICAL BUTTON ON THE I2C ENCODER"));

    snprintf_P(buf, sizeof(buf), PSTR("TO ASSIGN ADDRESS 0X%02X."), enc_setup_target_addr);
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 12);
    display.print(buf);

    snprintf_P(buf, sizeof(buf), PSTR("CURRENT SOURCE ADDRESS: 0X%02X."), enc_setup_source_addr);
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 13);
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.print(buf);
  }

  helptext_r(F("PRESS ENCODER OR ENC_R TO CANCEL"));
}

// ----- SELECT phase ---------------------------------------------------
static void _enc_setup_draw_select_buttons(void) {
  TouchButton::drawButton(GRID.X[0], GRID.Y[4], "I2C-ENC", "#1 0X24",
                          (enc_setup_select_idx == 0) ? TouchButton::BUTTON_ACTIVE
                                                      : TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[1], GRID.Y[4], "I2C-ENC", "#2 0X25",
                          (enc_setup_select_idx == 1) ? TouchButton::BUTTON_ACTIVE
                                                      : TouchButton::BUTTON_NORMAL);
}

static void _enc_setup_draw_select(void) {
  _enc_setup_clear_action_area();

  bool can24 = false, can25 = false;
  _enc_setup_find_targets(enc_setup_source_addr, can24, can25);

  if (!can24 && !can25) {
    enc_setup_phase = ENC_SETUP_CONFIRM;
    _enc_setup_draw_confirm();
    return;
  }

  if (can24 && !can25) {
    _enc_setup_start_assignment(I2C_ENC_L_ADDR);
    return;
  }
  if (!can24 && can25) {
    _enc_setup_start_assignment(I2C_ENC_R_ADDR);
    return;
  }

  if (enc_setup_select_idx > 1) enc_setup_select_idx = 0;

  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
  display.setTextColor(COLOR_ARP, COLOR_BACKGROUND);
  display.print(F("STEP 2 OF 3: CHOOSE TARGET I2C ENCODER"));
  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 11);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.print(F("TURN ENC_R OR TOUCH, PUSH ENC_R TO CONFIRM"));

  _enc_setup_draw_select_buttons();

  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 13);
  display.setTextColor(GREY1, COLOR_BACKGROUND);
  display.print(F("ASSIGN THE CONNECTED I2C ENCODER TO SLOT #1"));
  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 14);
  display.print(F("OR SLOT #2. ORDER DOES NOT MATTER."));

  helptext_r(F("TURN+PUSH ENC_R OR TOUCH"));
}

// ----- DONE phase -----------------------------------------------------
static void _enc_setup_draw_done(void) {
  _enc_setup_clear_action_area();
  display.setTextSize(1);
  char buf[48];

  // Blank the address list: after an address change, the bus state on
  // this power cycle is not reliable, and the user is being told to
  // reboot. Leaving stale FOUND/MISSING values on screen would be
  // misleading.
  _enc_setup_clear_status();

  if (enc_setup_is_reset) {
    const uint8_t enc_num = (enc_setup_source_addr == I2C_ENC_L_ADDR) ? 1 : 2;
    snprintf_P(buf, sizeof(buf), PSTR("I2C-ENC #%d RESET TO 0X20."), enc_num);
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(GREEN, COLOR_BACKGROUND);
    display.print(buf);
  } else {
    snprintf_P(buf, sizeof(buf), PSTR("I2C ENCODER ASSIGNED TO 0X%02X."), enc_setup_target_addr);
    setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 10);
    display.setTextColor(GREEN, COLOR_BACKGROUND);
    display.print(buf);
  }

  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 12);
  display.setTextColor(COLOR_ARP, COLOR_BACKGROUND);
  display.print(F("PLEASE REBOOT THE DEVICE FOR THE NEW"));

  setCursor_textGrid_small(ENC_SETUP_COL_TEXT, 13);
  display.print(F("ADDRESS TO TAKE EFFECT."));

  TouchButton::drawButton(GRID.X[0], GRID.Y[4], "BACK", "STEP 1",
                          TouchButton::BUTTON_NORMAL);

  helptext_r(F("REBOOT TO APPLY"));
}

static void _enc_setup_draw_action(void) {
  switch (enc_setup_phase) {
    case ENC_SETUP_CONFIRM: _enc_setup_draw_confirm(); break;
    case ENC_SETUP_SELECT:  _enc_setup_draw_select();  break;
    case ENC_SETUP_BUTTON:  _enc_setup_draw_button();  break;
    case ENC_SETUP_DONE:    _enc_setup_draw_done();    break;
  }
}

// =====================================================================
//  Regular mechanical right encoder: only readable inside
//  LCDML.FUNC_loop(), while the LCDML BT flags are still valid.
// =====================================================================
static void _enc_setup_process_encoder(void) {
  const EncoderEvents e = getEncoderEvents(ENC_R);

  switch (enc_setup_phase) {
    case ENC_SETUP_CONFIRM:
      if (e.pressed && encoder_detected_0x20) {
        _enc_setup_do_confirm();
      }
      break;

    case ENC_SETUP_SELECT: {
      bool can24 = false, can25 = false;
      _enc_setup_find_targets(enc_setup_source_addr, can24, can25);
      if (can24 && can25) {
        if (e.turned) {
          enc_setup_select_idx = 1 - enc_setup_select_idx;
          _enc_setup_draw_select_buttons();
        }
        if (e.pressed) {
          uint8_t target = (enc_setup_select_idx == 0) ? I2C_ENC_L_ADDR
                                                       : I2C_ENC_R_ADDR;
          _enc_setup_start_assignment(target);
        }
      }
      break;
    }

    case ENC_SETUP_BUTTON:
      // Panel-side cancel: go back to CONFIRM without touching the bus.
      if (e.pressed) {
        _enc_setup_rescan_and_draw_status();
        enc_setup_phase    = ENC_SETUP_CONFIRM;
        enc_setup_is_reset = false;
        _enc_setup_draw_action();
      }
      break;

    case ENC_SETUP_DONE:
      if (enc_setup_is_reset && e.pressed) {
        _enc_setup_return_to_confirm();
      }
      break;
  }
}

// =====================================================================
//  Touch input + I2C polling: from the touch handler, which runs from
//  the main loop every ~10 ms. This is the reliable loop for reading
//  the encoder's ESTATUS register.
// =====================================================================
static void _enc_setup_process_touch_and_polling(void) {

  // -----------------------------------------------------------------
  // Touch buttons
  // -----------------------------------------------------------------
  switch (enc_setup_phase) {
    case ENC_SETUP_CONFIRM: {
      if (encoder_detected_0x20 &&
          TouchButton::isPressed(GRID.X[0], GRID.Y[4])) {
        _enc_setup_do_confirm();
      }
      if (encoder_detected_0x24 &&
          TouchButton::isPressed(GRID.X[3], GRID.Y[4])) {
        _enc_setup_do_reset(I2C_ENC_L_ADDR);
      } else if (encoder_detected_0x25 &&
                 TouchButton::isPressed(GRID.X[4], GRID.Y[4])) {
        _enc_setup_do_reset(I2C_ENC_R_ADDR);
      }
      break;
    }

    case ENC_SETUP_SELECT: {
      bool can24 = false, can25 = false;
      _enc_setup_find_targets(enc_setup_source_addr, can24, can25);
      if (can24 && TouchButton::isPressed(GRID.X[0], GRID.Y[4])) {
        _enc_setup_start_assignment(I2C_ENC_L_ADDR);
      } else if (can25 && TouchButton::isPressed(GRID.X[1], GRID.Y[4])) {
        _enc_setup_start_assignment(I2C_ENC_R_ADDR);
      }
      break;
    }

    case ENC_SETUP_BUTTON:
      break;

    case ENC_SETUP_DONE:
      if (enc_setup_is_reset &&
          TouchButton::isPressed(GRID.X[0], GRID.Y[4])) {
        _enc_setup_return_to_confirm();
      }
      break;
  }

  // -----------------------------------------------------------------
  // Poll the encoder that is waiting to be confirmed. No timeout;
  // the user must either press the encoder button, long-press to
  // cancel, or press ENC_R on the panel to cancel.
  // -----------------------------------------------------------------
  if (enc_setup_phase == ENC_SETUP_BUTTON && enc_setup_source_addr != 0) {

    uint8_t st = _enc_read_reg(enc_setup_source_addr, ENC_REG_ESTATUS);
    bool have_response = (st != 0xFF);

    // If the source no longer answers, and it isn't already the target,
    // try the target address too.
    if (!have_response && enc_setup_target_addr != 0 &&
        enc_setup_target_addr != enc_setup_source_addr) {
      uint8_t st2 = _enc_read_reg(enc_setup_target_addr, ENC_REG_ESTATUS);
      if (st2 != 0xFF) {
        enc_setup_source_addr = enc_setup_target_addr;
        st = st2;
        have_response = true;
      }
    }

    if (have_response && (st & ENC_ST_PUSHP)) {

      const uint8_t old_addr = enc_setup_source_addr;
      const uint8_t new_addr = enc_setup_target_addr;

      _enc_write_reg(old_addr, ENC_REG_I2CADDR, new_addr);
      _enc_write_reg(old_addr, ENC_REG_I2CADDR, new_addr);
      _enc_write_reg(old_addr, ENC_REG_I2CADDR, new_addr);

      // No post-change rescan. The bus state on this power cycle is
      // not reliable after an address change. The DONE screen blanks
      // the list and tells the user to reboot.
      enc_setup_phase = ENC_SETUP_DONE;
      _enc_setup_draw_done();

    } else if (have_response && (st & ENC_ST_PUSHL)) {
      // Long press on the encoder button = cancel
      encoder_rescan_bus();
      _enc_setup_draw_status();
      enc_setup_phase    = ENC_SETUP_CONFIRM;
      enc_setup_is_reset = false;
      _enc_setup_draw_action();
    }
  }
}

// =====================================================================
//  Touch handler 
// =====================================================================
void handle_touchscreen_encoder_address_setup(void) {
  if (!enc_setup_page_active) return;
  _enc_setup_process_touch_and_polling();
}

// =====================================================================
//  LCDML page
// =====================================================================
void UI_func_encoder_address_setup(uint8_t param) {
  if (LCDML.FUNC_setup()) {
    registerTouchHandler(handle_touchscreen_encoder_address_setup);

    enc_setup_phase          = ENC_SETUP_CONFIRM;
    enc_setup_source_addr    = 0;
    enc_setup_target_addr    = 0;
    enc_setup_select_idx     = 0;
    enc_setup_is_reset       = false;
    enc_setup_page_active    = true;

    seq.cycle_touch_element = 0;

    encoder_rescan_bus();

    display.fillScreen(COLOR_BACKGROUND);

    _enc_setup_draw_title();
    _enc_setup_draw_static();
    _enc_setup_draw_status();
    _enc_setup_draw_action();

    _enc_setup_draw_back_button();
  }

  if (LCDML.FUNC_loop()) {
    if (!enc_setup_page_active) return;
    _enc_setup_process_encoder();
  }

  if (LCDML.FUNC_close()) {
    enc_setup_page_active = false;
    enc_setup_phase       = ENC_SETUP_CONFIRM;
    enc_setup_is_reset    = false;
    unregisterTouchHandler();
    display.fillScreen(COLOR_BACKGROUND);
  }
}

#endif // I2C_ENCODERS