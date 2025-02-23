/*
   MicroDexed-touch

   MicroDexed is a port of the Dexed sound engine
   (https://github.com/asb2m10/dexed) for the Teensy-3.5/3.6/4.x with audio shield.
   Dexed ist heavily based on https://github.com/google/music-synthesizer-for-android

   (c)2018-2021 H. Wirtz <wirtz@parasitstudio.de>

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

#ifndef _UI_H_
#define _UI_H_

#define _LCDML_DISP_cnt 64
#define SCREENSAVER_INTERVAL_MS 40  // 25Hz refresh rate
#define SCREENSAVER_STAY_TIME 1500  // 60s @ 25Hz rate
#define SCREENSAVER_FADE_TIME 25    // 1s @ 25Hz rate
#define SCREENSAVER_BRIGHTNESS_STEP (255/SCREENSAVER_FADE_TIME)
#define SCREENSAVER_MAX_COUNTHUE 359

#include "touchbutton.h"
#include "ILI9341_t3n.h"
#include "MD_REncoder.h"

#include "MD_MIDIFile.h"
#include "effect_stereo_panorama.h"
#include "effect_delay_ext8.h"
#include "effect_midside.h"
#include "effect_stereo_mono.h"
#include "effect_mono_stereo.h"
#include "effect_gate.h"
#include "psramloader.h"

#include "playarrayresmp.h"
#include "USBHost_t36.h"

extern JoystickController joysticks[];

struct ScopeSettings {
  bool enabled;
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
  bool onlyDrawWhenRunning;
} currentScopeSettings;


void splash_draw_header();
void splash_draw_D();
void splash_draw_reverseD();
void splash_draw_X(uint8_t c);
void draw_logo2(uint8_t yoffset, uint8_t progress = 10);
void setCursor_textGrid(uint8_t pos_x, uint8_t pos_y);
void setCursor_textGrid_small(uint8_t pos_x, uint8_t pos_y);

void previewWavSampleEditor();
void previewWavFilemanager();

bool touch_button_back_page();
bool legacy_touch_button_back_page();
void draw_back_touchbutton();
void draw_button_on_grid(uint8_t x, uint8_t y, const char* t1, const char* t2, uint8_t color);
void show_no_grid(uint8_t pos_y, uint8_t pos_x, uint8_t field_size, long num);
void show_no_grid(int pos_y, int pos_x, uint8_t field_size, const char* str);
uint8_t midiNoteToSampleNote(uint8_t note);
const char* get_drum_name_from_note(uint8_t note);
const char* seq_find_shortname(uint8_t sstep);
void seq_printAllVelocitySteps_single_step(uint8_t step, int color);
void print_track_steps_detailed_only_current_playing_note(int xpos, int ypos, uint8_t currentstep);
void resetScreenTimer();
void continueWavRecording();
ScopeSettings& getCurrentScopeSettings(void);
void update_display_functions_while_seq_running();

void UI_func_stereo_mono(uint8_t param);
void UI_func_dexed_audio(uint8_t param);
void UI_func_dexed_controllers(uint8_t param);
void UI_func_dexed_setup(uint8_t param);
void UI_handle_OP(uint8_t param);
void UI_func_information(uint8_t param);
void UI_func_master_effects(uint8_t param);
void UI_func_braids(uint8_t param);
void UI_func_multiband_dynamics(uint8_t param);
void UI_func_recorder(uint8_t param);
void UI_func_file_manager(uint8_t param);
void UI_func_custom_mappings(uint8_t param);
void UI_func_microsynth(uint8_t param);
void UI_func_seq_pattern_editor(uint8_t param);
void UI_func_seq_vel_editor(uint8_t param);
void UI_func_clear_song(uint8_t param);
void UI_func_clear_song_chains(uint8_t param);
void UI_func_clear_patterns(uint8_t param);
void UI_func_clear_all(uint8_t param);
void UI_func_seq_settings(uint8_t param);
void UI_func_seq_tracker(uint8_t param);
void UI_func_drums(uint8_t param);
void UI_func_MultiSamplePlay(uint8_t param);
void UI_func_arpeggio(uint8_t param);
void UI_func_seq_mute_matrix(uint8_t param);
void UI_func_set_performance_name(uint8_t param);
void UI_func_set_multisample_name(uint8_t param);
void UI_func_volume(uint8_t param);
void UI_func_smart_filter(uint8_t param);
void UI_func_seq_probabilities(uint8_t param);
void UI_func_mixer(uint8_t param);
void UI_func_sidechain(uint8_t param);
void UI_func_song(uint8_t param);
void UI_func_load_performance(uint8_t param);
void UI_func_save_performance(uint8_t param);
void UI_func_save_voice(uint8_t param);
void UI_func_midi_soft_thru(uint8_t param);
void UI_func_midi_channels(uint8_t param);
void UI_func_sd_content_not_found(uint8_t param);
void UI_func_system_settings(uint8_t param);
void UI_func_velocity_level(uint8_t param);
void UI_func_voice_select(uint8_t param);
void UI_func_voice_editor(uint8_t param);
void UI_func_sysex_send_voice(uint8_t param);
void UI_func_sysex_receive_bank(uint8_t param);
void UI_func_sysex_send_bank(uint8_t param);
void UI_func_startup_performance(uint8_t param);
void UI_func_startup_page(uint8_t param);
void UI_func_map_gamepad(uint8_t param);
void UI_func_favorites(uint8_t param);
void UI_func_epiano(uint8_t param);
void UI_func_sample_editor(uint8_t param);
void UI_func_midiplayer(uint8_t param);
void UI_func_test_touchscreen(uint8_t param);
void UI_func_sound_intensity(uint8_t param);
void UI_func_chorus_level(uint8_t param);
void UI_func_filter_resonance(uint8_t param);
void UI_func_filter_cutoff(uint8_t param);

void setup_ui(void);
void midiCallback(midi_event* pev);
uint8_t count_omni();
uint8_t count_midi_channel_duplicates(bool find_first);
void print_smf_parameters();
void selectSample(uint8_t sample);
void update_seq_speed();
void mb_set_mutes();
void mb_set_master();
void mb_set_compressor();
void clear_song_playhead();
void sub_song_print_tracknumbers();
bool load_performance_and_check_midi(uint8_t perf);
void setup_screensaver(void);

extern uint8_t external_psram_size;
uint32_t psram_free_bytes = 0;
PsramLoader sampleLoader(psram_free_bytes);
#ifdef COMPILE_FOR_PSRAM
AudioPlayArrayResmp WAV_preview_PSRAM;
#endif


ILI9341_t3n display(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCK, TFT_MISO);
MD_REncoder ENCODER[NUM_ENCODER]{ MD_REncoder(ENC_R_PIN_B, ENC_R_PIN_A), MD_REncoder(ENC_L_PIN_B, ENC_L_PIN_A) };
MD_MIDIFile SMF;
uint8_t activeSample;
uint8_t generic_temp_select_menu;
uint8_t generic_active_function = 99;
uint8_t generic_menu;
bool generic_full_draw_required = false;
uint8_t num_slices[2] = { 0,0 };
uint8_t selected_slice_sample[2] = { 99,99 };
uint32_t slice_start[2][16];
uint32_t slice_end[2][16];
uint16_t slices_scrollspeed = 1;
bool slices_autoalign = true;

bool loop_start_button = false;
bool loop_end_button = false;
bool remote_console_keystate_select;
bool remote_console_keystate_a;
bool remote_console_keystate_b;

#define MAX_CC_DEST 15
PROGMEM const uint8_t cc_dest_values[MAX_CC_DEST] = { 7, 10, 20, 21, 22, 23, 24,
  // 25,
  26, 27, 32, 91, 200, 201, 202, 203 };

uint32_t gamepad_buttons_neutral;
uint8_t gamepad_0_neutral;
uint8_t gamepad_1_neutral;

enum
{
  ENC_R,
  ENC_L
};

PROGMEM const uint8_t cc_dest_values_UI_mapping[8] = { 20, 21, 22, 23, 24, 25, 26, 27 };


#endif //_UI_H_