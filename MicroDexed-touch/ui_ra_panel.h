#pragma once
#include <stdint.h>

#if defined(RA_DISPLAY)

extern volatile int8_t ra_panel_enc_l;
extern volatile int8_t ra_panel_enc_r;
extern volatile bool   ra_panel_btn_l;  // ENC_L press = back
extern volatile bool   ra_panel_btn_r;  // ENC_R press = enter/select

// Virtual joystick directions — set by panel, consumed by key_up/down/left/right()
extern volatile bool   ra_panel_virt_up;
extern volatile bool   ra_panel_virt_down;
extern volatile bool   ra_panel_virt_left;
extern volatile bool   ra_panel_virt_right;

// Number of entries in the TARGETS[] table (all assignable functions)
static constexpr uint8_t RA_NUM_TARGETS = 47;

void ra_panel_draw();
void ra_panel_handle_touch();
void ra_panel_update_buttons();

#endif // RA_DISPLAY && TOUCHMENU
