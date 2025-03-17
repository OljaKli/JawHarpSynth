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

struct ScopeSettings {
  bool enabled;
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
  bool onlyDrawWhenRunning;
};

void splash_draw_header();
void splash_draw_D();
void splash_draw_reverseD();
void splash_draw_X(uint8_t c);
void draw_logo2(uint8_t yoffset, uint8_t progress = 10);
void setCursor_textGrid(uint8_t pos_x, uint8_t pos_y);
void setCursor_textGrid_small(uint8_t pos_x, uint8_t pos_y);

void previewWavSampleEditor();
void previewWavFilemanager();

void getNoteName(char* noteName, uint8_t noteNumber);
void print_arp_start_stop_button();
void print_custom_mappings(void);
void update_midi_learn_button();
bool touch_button_back_page();
bool legacy_touch_button_back_page();
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
void loadDexedOrigADSR(int instance);

void UI_func_dexed_audio(uint8_t param);
void UI_func_dexed_controllers(uint8_t param);
void UI_func_dexed_setup(uint8_t param);
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
void UI_func_seq_settings(uint8_t param);
void UI_func_seq_tracker(uint8_t param);
void UI_func_drums(uint8_t param);
void UI_func_MultiSamplePlay(uint8_t param);
void UI_func_arpeggio(uint8_t param);
void UI_func_seq_mute_matrix(uint8_t param);
void UI_func_set_performance_name(uint8_t param);
void UI_func_volume(uint8_t param);
void UI_func_mixer(uint8_t param);
void UI_func_sidechain(uint8_t param);
void UI_func_song(uint8_t param);
void UI_func_load_performance(uint8_t param);
void UI_func_save_performance(uint8_t param);
void UI_func_midi_channels(uint8_t param);
void UI_func_sd_content_not_found(uint8_t param);
void UI_func_system_settings(uint8_t param);
void UI_func_voice_select(uint8_t param);
void UI_func_voice_editor(uint8_t param);
void UI_func_sysex_receive_bank(uint8_t param);
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
void print_voice_settings_in_dexed_voice_select(bool fullrefresh_text, bool fullrefresh_values);
void print_voice_select_fav_search_button();
void reset_live_modifiers();
void pattern_editor_menu_0();
void sub_touchscreen_test_page_init();
void border3_large();
void border3_large_clear();
void seq_pattern_editor_update_dynamic_elements();
void microsynth_refresh_lower_screen_static_text();
void microsynth_refresh_lower_screen_dynamic_text();
void panic_dexed_current_instance();
void printLiveModButtons(void);
void printLiveModButton(int8_t button); // 0 - 3 ADSR

void print_empty_spaces(uint8_t spaces, bool clear_background);
void print_voice_select_default_help();
void draw_volmeters_mixer();
void draw_volmeters_multiband_compressor();
void save_favorite(uint8_t p, uint8_t b, uint8_t v, uint8_t instance_id);

void draw_algo(bool forceDraw = false);
uint8_t get_algo();
void set_algo(uint8_t value);

const EncoderEvents getEncoderEvents(uint8_t id);

#define MAX_CC_DEST 15
PROGMEM static const uint8_t cc_dest_values[MAX_CC_DEST] = { 7, 10, 20, 21, 22, 23, 24,
  // 25,
  26, 27, 32, 91, 200, 201, 202, 203 };

PROGMEM static const char noteNames[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

enum {
  ENC_R = 0,
  ENC_L = 1
};

PROGMEM static const uint8_t cc_dest_values_UI_mapping[8] = { 20, 21, 22, 23, 24, 25, 26, 27 };

#endif //_UI_H_