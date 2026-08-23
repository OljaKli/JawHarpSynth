/*
   MicroDexed-touch

   Song Arranger - Song section arranger keyboard for LSDJ-style sequencer

   (c)2018-2021 H. Wirtz <wirtz@parasitstudio.de>
   (c)2021-2026 H. Wirtz <wirtz@parasitstudio.de>, M. Koslowski <positionhigh@gmx.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "ui_song_arranger.h"
#include "UI.h"
#include "sequencer.h"
#include "touch.h"
#include "touchbutton.h"
#include "config.h"
#include <LCDMenuLib2.h>
#include "MD_REncoder.h"

#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined RA_DISPLAY
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined RA_DISPLAY
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

extern LCDMenuLib2 LCDML;
extern sequencer_t seq;
extern uint8_t generic_temp_select_menu;
extern uint8_t numTouchPoints;
extern ts_t ts;
extern void checkWebRemote();
extern void helptext_l(const char* text);
extern void helptext_r(const char* text);
extern void print_formatted_number(uint16_t number, uint8_t length);
extern void setCursor_textGrid_small(uint8_t pos_x, uint8_t pos_y);
extern void registerTouchHandler(void (*handler)(void));
extern void unregisterTouchHandler(void);
extern void handleStart(void);
extern void handleStop(void);
extern void draw_back_touchbutton(void);
extern const char noteNames[12][3];
extern void getNoteName(char* noteName, uint8_t noteNumber);

// Flag to track if Song Arranger data has been loaded from a performance
bool song_arranger_data_loaded_from_performance = false;
bool song_arranger_blink_state = false;
static TouchButton::Color last_button_colors[10] = { TouchButton::BUTTON_NORMAL };

// ============ Song Arranger INITIALIZATION ============

FLASHMEM void init_song_arranger() {
    const char* default_names[10] = {
        "INTRO", "VERS1", "VERS2", "VERS3", "CHRUS",
        "BRDGE", "SOLO", "OUTRO", "BREAK", "CUSTM"
    };
    
    uint16_t colors[10] = { 
        COLOR_INSTR,
        COLOR_CHORDS,
        COLOR_ARP,
        COLOR_DRUMS,
        COLOR_PITCHSMP,
        DX_DARKCYAN,
        GREEN,
        YELLOW,
        PINK,
        DARKGREEN
    };
    
    // Only set defaults if no data has been loaded from performance
    if (!song_arranger_data_loaded_from_performance) {
        for (uint8_t i = 0; i < 10; i++) {
            strncpy(seq.song_arranger_parts[i].name, default_names[i], 15);
            seq.song_arranger_parts[i].name[15] = '\0';
            seq.song_arranger_parts[i].start_step = 99;
            seq.song_arranger_parts[i].end_step = 99;
            seq.song_arranger_parts[i].continue_step = 99;
            seq.song_arranger_parts[i].color = colors[i];
            seq.song_arranger_parts[i].active = false;
            seq.song_arranger_parts[i].midi_note = 60 + i;
        }
        seq.song_arranger_active = false;
        seq.song_arranger_base_note = 60;
    }
    
    // Always reset playing state
    seq.song_arranger_playing = false;
    seq.song_arranger_current_part = 99;
    seq.song_arranger_next_part = 99;
    seq.song_arranger_part_chain_counter = 0;
    seq.song_arranger_part_chain_started = false;
    seq.song_arranger_requested_part = 99;
    seq.song_arranger_part_change_pending = false;
    seq.song_arranger_last_song_step = 0;
    seq.song_arranger_switch_mode = 0; // default: switch after pattern
    seq.song_arranger_need_redraw_buttons = false;
}

// ============ Song Arranger HELPER FUNCTIONS ============
// Forward declaration
void draw_song_arranger_button_states(void);

void apply_song_arranger_part_change(uint8_t part_index) {
    if (part_index >= 10) return;
    song_arranger_part_t& part = seq.song_arranger_parts[part_index];
    if (!part.active || part.start_step >= SONG_LENGTH) return;

    seq.song_arranger_current_part = part_index;
    seq.song_arranger_part_change_pending = false;
    seq.song_arranger_requested_part = 99;
    seq.song_arranger_next_part = 99;

    seq.current_song_step = part.start_step;
    for (uint8_t d = 0; d < NUM_SEQ_TRACKS; d++) {
        seq.chain_counter[d] = 0;
        seq.current_chain[d] = seq.song[d][seq.current_song_step];
    }
    seq.step = 0;

    // Signal that the UI needs to redraw buttons (will be picked up in touch loop)
    seq.song_arranger_need_redraw_buttons = true;
}

FLASHMEM void draw_song_arranger_switch_button() {
    uint16_t x = GRID.X[4];
    uint16_t y = GRID.Y[0];

    const char* label = "SWITCH";
    const char* sub = (seq.song_arranger_switch_mode == 0) ? "PATT" : "SNGSTP";

    TouchButton::drawButton(x, y, label, sub, TouchButton::BUTTON_ACTIVE);
}

// Get MDT part for a song step - based on step number
FLASHMEM uint8_t get_song_arranger_part_for_song_step(uint8_t step) {
    if (!seq.song_arranger_active) return 99;
    if (step >= SONG_LENGTH) return 99;
    
    for (uint8_t p = 0; p < 10; p++) {
        if (!seq.song_arranger_parts[p].active) continue;
        
        uint8_t start = seq.song_arranger_parts[p].start_step;
        uint8_t end = seq.song_arranger_parts[p].end_step;
        
        if (start < SONG_LENGTH && end < SONG_LENGTH && start <= end) {
            if (step >= start && step <= end) {
                return p;
            }
        }
    }
    return 99;
}

// Get the color for a song step
FLASHMEM uint16_t get_song_arranger_color_for_song_step(uint8_t step) {
    uint8_t part = get_song_arranger_part_for_song_step(step);
    if (part < 10) {
        return seq.song_arranger_parts[part].color;
    }
    return COLOR_BACKGROUND;
}

FLASHMEM void update_song_arranger_midi_notes() {
    for (uint8_t i = 0; i < 10; i++) {
        seq.song_arranger_parts[i].midi_note = seq.song_arranger_base_note + i;
    }
}

// ============ Song Arranger UI DRAWING FUNCTIONS ============

FLASHMEM void draw_song_arranger_buttons() {
    const uint8_t BTN_W = TouchButton::BUTTON_SIZE_X;
    const uint8_t BTN_H = TouchButton::BUTTON_SIZE_Y;
    const uint8_t SPACING = TouchButton::BUTTON_SPACING;

    for (uint8_t i = 0; i < 10; i++) {
        uint8_t row = i / 5;
        uint8_t col = i % 5;
        uint16_t x = col * (BTN_W + SPACING);
        uint16_t y = row * (BTN_H + SPACING) + 60;

        song_arranger_part_t& part = seq.song_arranger_parts[i];
        TouchButton::Color baseColor = part.active ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL;

        char upper_name[16];
        strncpy(upper_name, part.name, 15);
        upper_name[15] = '\0';
        for (uint8_t c = 0; c < strlen(upper_name); c++) {
            upper_name[c] = toupper(upper_name[c]);
        }

        // Cache the initial color
        last_button_colors[i] = baseColor;

        TouchButton::drawButton(x, y, upper_name, "", baseColor);
    }
}

FLASHMEM void draw_song_arranger_button_states() {
    const uint8_t BTN_W = TouchButton::BUTTON_SIZE_X;
    const uint8_t BTN_H = TouchButton::BUTTON_SIZE_Y;
    const uint8_t SPACING = TouchButton::BUTTON_SPACING;

    for (uint8_t i = 0; i < 10; i++) {
        uint8_t row = i / 5;
        uint8_t col = i % 5;
        uint16_t x = col * (BTN_W + SPACING);
        uint16_t y = row * (BTN_H + SPACING) + 60;

        song_arranger_part_t& part = seq.song_arranger_parts[i];

        TouchButton::Color color;

        if (seq.song_arranger_playing && seq.song_arranger_current_part == i) {
            color = TouchButton::BUTTON_RED;
        } else if (seq.song_arranger_part_change_pending && seq.song_arranger_requested_part == i) {
            color = song_arranger_blink_state ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL;
        } else if (part.active) {
            color = TouchButton::BUTTON_ACTIVE;
        } else {
            color = TouchButton::BUTTON_NORMAL;
        }

        // Only redraw if the color changed
        if (color != last_button_colors[i]) {
            last_button_colors[i] = color;

            char upper_name[16];
            strncpy(upper_name, part.name, 15);
            upper_name[15] = '\0';
            for (uint8_t c = 0; c < strlen(upper_name); c++) {
                upper_name[c] = toupper(upper_name[c]);
            }
            TouchButton::drawButton(x, y, upper_name, "", color);
        }
    }
}

FLASHMEM void draw_song_arranger_enable_button() {
    uint16_t x = GRID.X[5];
    uint16_t y = GRID.Y[0];
    
    TouchButton::Color color;
    if (seq.song_arranger_active) {
        color = TouchButton::BUTTON_RED;
    } else {
        color = TouchButton::BUTTON_NORMAL;
    }
    
    TouchButton::drawButton(x, y, "ARRANGER", seq.song_arranger_active ? "ON" : "OFF", color);
}

FLASHMEM void draw_song_arranger_config_grid() {
    // Draw the configuration grid: 10 parts x 3 fields (Start Step, End Step, Continue Step)
    // 2 columns: Parts 0-4 on left, Parts 5-9 on right
    
    int y_start = 15;
    int x_start_left = 0;
    int x_start_right = 28;
    
    // Column headers for left column - Full labels
    display.setTextSize(1);
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    setCursor_textGrid_small(x_start_left, y_start - 1);
    display.print(F("NOTE"));
    setCursor_textGrid_small(x_start_left + 5, y_start - 1);
    display.print(F("PART"));
    setCursor_textGrid_small(x_start_left + 11, y_start - 1);
    display.print(F("START"));
    setCursor_textGrid_small(x_start_left + 17, y_start - 1);
    display.print(F("END"));
    setCursor_textGrid_small(x_start_left + 21, y_start - 1);
    display.print(F("CONT"));
    
    // Column headers for right column - Full labels
    setCursor_textGrid_small(x_start_right, y_start - 1);
    display.print(F("NOTE"));
    setCursor_textGrid_small(x_start_right + 5, y_start - 1);
    display.print(F("PART"));
    setCursor_textGrid_small(x_start_right + 11, y_start - 1);
    display.print(F("START"));
    setCursor_textGrid_small(x_start_right + 17, y_start - 1);
    display.print(F("END"));
    setCursor_textGrid_small(x_start_right + 21, y_start - 1);
    display.print(F("CONT"));
    
    // Draw left column (parts 0-4)
    for (uint8_t i = 0; i < 5; i++) {
        int y = y_start + i;
        
        // MIDI Note
        setCursor_textGrid_small(x_start_left, y);
        if (seq.song_arranger_parts[i].active) {
            display.setTextColor(seq.song_arranger_parts[i].color, COLOR_BACKGROUND);
        } else {
            display.setTextColor(GREY2, COLOR_BACKGROUND);
        }
        char note_name[5];
        getNoteName(note_name, seq.song_arranger_parts[i].midi_note);
        display.print(note_name);
        
        // Part name
        setCursor_textGrid_small(x_start_left + 5, y);
        display.setTextColor(seq.song_arranger_parts[i].color, COLOR_BACKGROUND);
        char upper_name[6];
        strncpy(upper_name, seq.song_arranger_parts[i].name, 5);
        upper_name[5] = '\0';
        for (uint8_t c = 0; c < strlen(upper_name); c++) {
            upper_name[c] = toupper(upper_name[c]);
        }
        display.print(upper_name);
        
        // Start step - display 1-indexed (add 1 for display)
        setCursor_textGrid_small(x_start_left + 11, y);
        if (generic_temp_select_menu == (10 + i * 3) && seq.edit_state) {
            display.setTextColor(COLOR_SYSTEXT, RED);
        } else if (generic_temp_select_menu == (10 + i * 3)) {
            display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        } else {
            display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        }
        if (seq.song_arranger_parts[i].start_step < SONG_LENGTH) {
            print_formatted_number(seq.song_arranger_parts[i].start_step + 1, 2);
        } else {
            display.print("--");
        }
        
        // End step - display 1-indexed (add 1 for display)
        setCursor_textGrid_small(x_start_left + 17, y);
        if (generic_temp_select_menu == (10 + i * 3 + 1) && seq.edit_state) {
            display.setTextColor(COLOR_SYSTEXT, RED);
        } else if (generic_temp_select_menu == (10 + i * 3 + 1)) {
            display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        } else {
            display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        }
        if (seq.song_arranger_parts[i].end_step < SONG_LENGTH) {
            print_formatted_number(seq.song_arranger_parts[i].end_step + 1, 2);
        } else {
            display.print("--");
        }
        
        // Continue step - display 1-indexed (add 1 for display)
        setCursor_textGrid_small(x_start_left + 21, y);
        if (generic_temp_select_menu == (10 + i * 3 + 2) && seq.edit_state) {
            display.setTextColor(COLOR_SYSTEXT, RED);
        } else if (generic_temp_select_menu == (10 + i * 3 + 2)) {
            display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        } else {
            display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        }
        if (seq.song_arranger_parts[i].continue_step < SONG_LENGTH) {
            print_formatted_number(seq.song_arranger_parts[i].continue_step + 1, 2);
        } else {
            display.print("--");
        }
    }
    
    // Draw right column (parts 5-9)
    for (uint8_t i = 0; i < 5; i++) {
        int idx = i + 5;
        int y = y_start + i;
        
        // MIDI Note
        setCursor_textGrid_small(x_start_right, y);
        if (seq.song_arranger_parts[idx].active) {
            display.setTextColor(seq.song_arranger_parts[idx].color, COLOR_BACKGROUND);
        } else {
            display.setTextColor(GREY2, COLOR_BACKGROUND);
        }
        char note_name[5];
        getNoteName(note_name, seq.song_arranger_parts[idx].midi_note);
        display.print(note_name);
        
        // Part name
        setCursor_textGrid_small(x_start_right + 5, y);
        display.setTextColor(seq.song_arranger_parts[idx].color, COLOR_BACKGROUND);
        char upper_name[6];
        strncpy(upper_name, seq.song_arranger_parts[idx].name, 5);
        upper_name[5] = '\0';
        for (uint8_t c = 0; c < strlen(upper_name); c++) {
            upper_name[c] = toupper(upper_name[c]);
        }
        display.print(upper_name);
        
        // Start step - display 1-indexed (add 1 for display)
        setCursor_textGrid_small(x_start_right + 11, y);
        if (generic_temp_select_menu == (10 + idx * 3) && seq.edit_state) {
            display.setTextColor(COLOR_SYSTEXT, RED);
        } else if (generic_temp_select_menu == (10 + idx * 3)) {
            display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        } else {
            display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        }
        if (seq.song_arranger_parts[idx].start_step < SONG_LENGTH) {
            print_formatted_number(seq.song_arranger_parts[idx].start_step + 1, 2);
        } else {
            display.print("--");
        }
        
        // End step - display 1-indexed (add 1 for display)
        setCursor_textGrid_small(x_start_right + 17, y);
        if (generic_temp_select_menu == (10 + idx * 3 + 1) && seq.edit_state) {
            display.setTextColor(COLOR_SYSTEXT, RED);
        } else if (generic_temp_select_menu == (10 + idx * 3 + 1)) {
            display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        } else {
            display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        }
        if (seq.song_arranger_parts[idx].end_step < SONG_LENGTH) {
            print_formatted_number(seq.song_arranger_parts[idx].end_step + 1, 2);
        } else {
            display.print("--");
        }
        
        // Continue step - display 1-indexed (add 1 for display)
        setCursor_textGrid_small(x_start_right + 21, y);
        if (generic_temp_select_menu == (10 + idx * 3 + 2) && seq.edit_state) {
            display.setTextColor(COLOR_SYSTEXT, RED);
        } else if (generic_temp_select_menu == (10 + idx * 3 + 2)) {
            display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        } else {
            display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        }
        if (seq.song_arranger_parts[idx].continue_step < SONG_LENGTH) {
            print_formatted_number(seq.song_arranger_parts[idx].continue_step + 1, 2);
        } else {
            display.print("--");
        }
    }
}

FLASHMEM void draw_song_arranger_midi_note_selector() {
    int y_pos = 21;
    int x_pos = 1;
    
    display.setTextSize(1);
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    setCursor_textGrid_small(x_pos, y_pos);
    display.print(F("MIDI NOTES:"));
    
    bool is_selected = (generic_temp_select_menu == 40);
    bool is_editing = (is_selected && seq.edit_state);
    bool is_navigating = (is_selected && !seq.edit_state);
    
    char note_name[5];
    char note_name2[5];
    getNoteName(note_name, seq.song_arranger_base_note);
    getNoteName(note_name2, seq.song_arranger_base_note + 9);
    
    uint8_t value_x = x_pos + 12;
    setCursor_textGrid_small(value_x, y_pos);
    
    if (is_editing) {
        display.setTextColor(COLOR_SYSTEXT, RED);
        display.print(note_name);
    } else if (is_navigating) {
        display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        display.print(note_name);
    } else {
        display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        display.print(note_name);
    }
    
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.print(F(" -> "));
    
    if (is_selected) {
        if (is_editing) {
            display.setTextColor(COLOR_SYSTEXT, RED);
        } else if (is_navigating) {
            display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
        } else {
            display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        }
    } else {
        display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    }
    
    display.print(note_name2);
    
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
}

// ============ Song Arranger MAIN UI FUNCTION ============

FLASHMEM void UI_func_song_arranger(uint8_t param) {
    static bool mdt_ui_initialized = false;
    static uint8_t old_edit_state = 0;
    
     if (LCDML.FUNC_setup()) {
        registerTouchHandler(handle_touchscreen_song_arranger);

        display.fillScreen(COLOR_BACKGROUND);
        seq.edit_state = false;
        generic_temp_select_menu = 10;
        old_edit_state = 0;

        if (!mdt_ui_initialized) {
            if (!song_arranger_data_loaded_from_performance) {
                init_song_arranger();
            } else {
                for (uint8_t i = 0; i < 10; i++) {
                    if (seq.song_arranger_parts[i].name[0] == '\0') {
                        const char* default_names[10] = {
                            "INTRO", "VERS1", "VERS2", "VERS3", "CHRUS",
                            "BRDGE", "SOLO", "OUTRO", "BREAK", "CUSTM"
                        };
                        strncpy(seq.song_arranger_parts[i].name, default_names[i], 15);
                        seq.song_arranger_parts[i].name[15] = '\0';
                    }
                    if (seq.song_arranger_parts[i].color == 0) {
                        uint16_t colors[10] = {
                            COLOR_INSTR, COLOR_CHORDS, COLOR_ARP, COLOR_DRUMS, COLOR_PITCHSMP,
                            DX_DARKCYAN, GREEN, YELLOW, PINK, DARKGREEN
                        };
                        seq.song_arranger_parts[i].color = colors[i];
                    }
                }
            }
            mdt_ui_initialized = true;
        }

        display.setTextSize(1);
        display.setTextColor(RED, COLOR_BACKGROUND);
        setCursor_textGrid_small(0, 0);
        display.print(F("SONG ARRANGER"));

        display.setTextColor(GREY2, COLOR_BACKGROUND);
        setCursor_textGrid_small(0, 2);
        display.print(F("TOUCH A BUTTON TO START PLAYING"));
        setCursor_textGrid_small(0, 3);
        display.print(F("THAT PART. SET START, END & CONT-"));
        setCursor_textGrid_small(0, 4);
        display.print(F("INUE STEP. WHEN CONT. IS EMPTY, IT WILL LOOP ENDLESS."));
        display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);

        draw_song_arranger_buttons();
        TouchButton::drawButton(GRID.X[3], GRID.Y[0], "CHORD", "ARR", TouchButton::BUTTON_NORMAL);
        draw_song_arranger_enable_button();    // at GRID.X[5]
        draw_song_arranger_switch_button();    // at GRID.X[4]
        draw_song_arranger_config_grid();
        draw_song_arranger_midi_note_selector();

        helptext_l(back_text);
        helptext_r(F("EDIT PART PARAMETERS"));
    }
    
    if (LCDML.FUNC_loop()) {
        const EncoderEvents e = getEncoderEvents(ENC_R);
        
        if (e.longPressed) {
            init_song_arranger();
            draw_song_arranger_buttons();
            draw_song_arranger_config_grid();
            draw_song_arranger_enable_button();
            draw_song_arranger_button_states();
            draw_song_arranger_midi_note_selector();
            helptext_r(F("RESET TO DEFAULT"));
            delay(500);
            helptext_r(F("EDIT PART PARAMETERS"));
        }
        
        if (e.turned) {
            if (seq.edit_state == 0) {
                uint8_t num_fields = 30;
                uint8_t last_grid_index = 10 + num_fields - 1;
                uint8_t midi_note_index = 40;
                
                if (generic_temp_select_menu == midi_note_index) {
                    if (e.down) {
                        generic_temp_select_menu = last_grid_index;
                    } else {
                        generic_temp_select_menu = last_grid_index;
                    }
                } else {
                    int8_t new_index = generic_temp_select_menu + e.dir * e.speed;
                    
                    if (new_index < 10) {
                        new_index = 10;
                    } else if (new_index > last_grid_index) {
                        if (e.down) {
                            generic_temp_select_menu = midi_note_index;
                        } else {
                            generic_temp_select_menu = last_grid_index;
                        }
                        draw_song_arranger_config_grid();
                        draw_song_arranger_midi_note_selector();
                        return;
                    }
                    
                    generic_temp_select_menu = new_index;
                }
                draw_song_arranger_config_grid();
                draw_song_arranger_midi_note_selector();
            } else {
                if (generic_temp_select_menu == 40) {
                    seq.song_arranger_base_note = constrain(seq.song_arranger_base_note + e.dir * e.speed, 24, 108);
                  seq.arr_split_note = seq.song_arranger_base_note; 
                    update_song_arranger_midi_notes();
                    draw_song_arranger_config_grid();
                    draw_song_arranger_midi_note_selector();
                } else {
                    uint8_t part_idx = (generic_temp_select_menu - 10) / 3;
                    uint8_t field_idx = (generic_temp_select_menu - 10) % 3;
                    
                    if (part_idx < 10) {
                        song_arranger_part_t& part = seq.song_arranger_parts[part_idx];
                        
                        switch (field_idx) {
                            case 0: // Start step - user sees 1-64, store 0-63
                                if (part.start_step == 99) {
                                    part.start_step = 0;
                                } else {
                                    int16_t new_val = part.start_step + e.dir * e.speed;
                                    if (new_val < 0) new_val = 0;
                                    if (new_val >= SONG_LENGTH) new_val = SONG_LENGTH - 1;
                                    part.start_step = new_val;
                                }
                                part.active = (part.start_step < SONG_LENGTH);
                                if (part.end_step == 99 && part.active) {
                                    part.end_step = part.start_step;
                                }
                                if (part.end_step < part.start_step) {
                                    part.end_step = part.start_step;
                                }
                                break;
                                
                            case 1: // End step - user sees 1-64, store 0-63
                                if (part.end_step == 99) {
                                    part.end_step = part.start_step;
                                } else {
                                    int16_t new_val = part.end_step + e.dir * e.speed;
                                    if (new_val < part.start_step) new_val = part.start_step;
                                    if (new_val >= SONG_LENGTH) new_val = SONG_LENGTH - 1;
                                    part.end_step = new_val;
                                }
                                if (part.end_step < part.start_step) {
                                    part.end_step = part.start_step;
                                }
                                break;
                                
                            case 2: // Continue step - user sees 1-64, store 0-63 (99 = end)
                                if (part.continue_step == 99) {
                                    part.continue_step = 0;
                                } else {
                                    int16_t new_val = part.continue_step + e.dir * e.speed;
                                    if (new_val < 0) new_val = 0;
                                    if (new_val >= SONG_LENGTH) new_val = SONG_LENGTH - 1;
                                    part.continue_step = new_val;
                                }
                                break;
                        }
                        
                        draw_song_arranger_config_grid();
                        draw_song_arranger_buttons();
                    }
                }
            }
        }
        
        if (e.pressed) {
            seq.edit_state = !seq.edit_state;
            if (seq.edit_state) {
                helptext_r(F("EDIT VALUE"));
            } else {
                helptext_r(F("SELECT PARAMETER"));
            }
            draw_song_arranger_config_grid();
            draw_song_arranger_midi_note_selector();
        }
        
        draw_song_arranger_button_states();
        draw_song_arranger_switch_button(); // to update label if mode changes

        if (seq.edit_state != old_edit_state) {
            draw_song_arranger_config_grid();
            old_edit_state = seq.edit_state;
        }
    }
    
    if (LCDML.FUNC_close()) {
        unregisterTouchHandler();
        display.fillScreen(COLOR_BACKGROUND);
    }
}

// ============ Song Arranger Touch Handler ============

FLASHMEM void handle_song_arranger_touch(uint8_t button_index) {
    if (button_index >= 10) return;

    song_arranger_part_t& part = seq.song_arranger_parts[button_index];

    if (!part.active || part.start_step >= SONG_LENGTH) {
        return;
    }

    if (!seq.song_arranger_active) {
        seq.song_arranger_active = true;
        draw_song_arranger_enable_button();
    }

    if (!seq.running) {
        handleStart();
        seq.song_arranger_current_part = button_index;
        seq.song_arranger_playing = true;
        seq.song_arranger_next_part = 99;
        seq.song_arranger_requested_part = 99;
        seq.song_arranger_part_change_pending = false;
        seq.current_song_step = part.start_step;
        for (uint8_t d = 0; d < NUM_SEQ_TRACKS; d++) {
            seq.chain_counter[d] = 0;
            seq.current_chain[d] = seq.song[d][seq.current_song_step];
        }
        seq.step = 0;
        draw_song_arranger_button_states();
        return;
    }

    if (seq.song_arranger_current_part == button_index && seq.song_arranger_playing) {
        return;
    }

    if (seq.song_arranger_requested_part == button_index && seq.song_arranger_part_change_pending) {
        return;
    }

    seq.song_arranger_requested_part = button_index;
    seq.song_arranger_part_change_pending = true;
    seq.song_arranger_next_part = button_index;

    draw_song_arranger_button_states();   // Immediate redraw of scheduled part
}