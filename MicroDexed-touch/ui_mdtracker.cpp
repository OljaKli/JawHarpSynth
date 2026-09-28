/*
   MicroDexed-touch  -  MDTracker/New Pattern Editor

   (c) 2024-2026 MicroDexed-touch contributors. GPLv3.
*/

#include "config.h"
#include "sequencer.h"
#include "synth_dexed.h"
#include "synth_mda_epiano.h"
#include "microsynth.h"
#include "braids.h"
#include "drums.h"
#include "dexed_sd.h"
#include "template_mixer.hpp"
#include "effect_mono_stereo.h"
#include "effect_stereo_panorama.h"
#include "effect_delay_ext8.h"
#include "effect_modulated_delay.h"

#include "UI.h"
#include "touch.h"
#include "touchbutton.h"
#include "virtualkeyboard.h"

#include <LCDMenuLib2.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#if defined ESP_DISPLAY
#include "esp32.h"
extern espdisplay display;
#elif defined RA_DISPLAY
#include "RA8875.h"
extern RA8875 display;
#else
#include "ILI9341_t3n.h"
extern ILI9341_t3n display;
#endif

// ===========================================================================
//  EXTERNS
// ===========================================================================
extern LCDMenuLib2 LCDML;
extern void       checkWebRemote();

extern sequencer_t seq;
extern config_t    configuration;
extern AudioSynthDexed* MicroDexed[NUM_DEXED];
extern microsynth_t microsynth[NUM_MICROSYNTH];
extern braids_t  braids_osc;
extern AudioSynthEPiano ep;
extern uint8_t selected_instance_id;
extern uint8_t activeSample;
extern uint8_t get_sample_note(uint8_t sample);

extern AudioMixer<REVERB_MIX_CH_MAX> reverb_mixer_r;
extern AudioMixer<REVERB_MIX_CH_MAX> reverb_mixer_l;
extern AudioMixer<2 * NUM_DRUMS + 4>* global_delay_in_mixer[2];
extern AudioMixer<2> dexed_chorus_mixer_r[NUM_DEXED];
extern AudioMixer<2> dexed_chorus_mixer_l[NUM_DEXED];
extern AudioEffectMonoStereo* dexed_mono2stereo[NUM_DEXED];
extern AudioEffectStereoPanorama ep_stereo_panorama;
extern AudioMixer<2> ep_chorus_mixer_r;
extern AudioMixer<2> ep_chorus_mixer_l;
extern AudioMixer<2> microsynth_mixer_reverb;
extern AudioMixer<2> braids_mixer_reverb;

extern int   numTouchPoints;
extern ts_t  ts;
extern uint8_t drum_midi_channel;

extern void  set_dexed_delay_level(uint8_t delay_no, uint8_t instance, uint8_t level);
extern void  update_braids_volume(void);
extern float midi_volume_transform(uint8_t midi_amp);
extern float volume_transform(float amp);
extern char* basename(const char* filename);

extern void handleNoteOn(byte inChannel, byte inNumber, byte inVelocity, byte device);
extern void handleNoteOff(byte inChannel, byte inNumber, byte inVelocity, byte device);
extern void handleStart(void);
extern void handleStop(void);
extern void stop_all_drum_slots(void);
extern void pattern_editor_play_current_step(uint8_t step);
extern void print_empty_spaces(uint8_t spaces, bool clear_background);
extern void getNoteName(char* noteName, uint8_t noteNumber);
extern void check_variable_samples_basespeed(void);
extern float get_sample_p_offset(uint8_t sample);
extern void  set_sample_pitch(uint8_t sample, float playbackspeed);

extern multisample_t msp[NUM_MULTISAMPLES];

#if NUM_DRUMS > 0
extern drum_config_t drum_config[NUM_DRUMSET_CONFIG];
#endif

#ifdef GRANULAR
#include "granular.h"
extern granular_params_t granular_params;
#endif

// ===========================================================================
//  LAYOUT
// ===========================================================================
#define MDT_HDR_H       10
#define MDT_SEP1_Y      10
#define MDT_COLHDR_Y    11
#define MDT_SEP2_Y      19
#define MDT_DATA_Y      21
#define MDT_ROW_H       10
#define MDT_ROWS        16
#define MDT_STATUS_Y    181
#define MDT_STATUS_H    24

#define MDT_VK_VISIBLE_ROWS 8

#define MDT_VK_ROWS_BOTTOM (MDT_DATA_Y + MDT_VK_VISIBLE_ROWS * MDT_ROW_H)
#define MDT_VK_TOP         ((uint16_t)GRID.Y[3])
#define MDT_VK_ADV_X       202
#define MDT_VK_ADV_Y       78
#define MDT_VK_ADV_W       104
#define MDT_VK_ADV_H       20
#define MDT_VK_VELBAR_Y    (MDT_VK_ROWS_BOTTOM + 8)
#define MDT_VK_VELBAR_H    12
#define MDT_VK_VELBAR_LX   2
#define MDT_VK_VELBAR_X    26
#define MDT_VK_VELBAR_W    128
#define MDT_VK_VELBAR_VX   (MDT_VK_VELBAR_X + MDT_VK_VELBAR_W + 5)
#define MDT_VK_VELBAR_TY0  (MDT_VK_ROWS_BOTTOM + 2)
#define MDT_VK_VELBAR_TY1  MDT_VK_TOP

#define MDT_ST_X        2
#define MDT_ST_W        2
#define MDT_NOTE_X      18
#define MDT_NOTE_W      4
#define MDT_VEL_X       46
#define MDT_INS_X       68
#define MDT_CMD_X       90
#define MDT_PAR_X       112
#define MDT_INFO_X      134
#define MDT_INFO_W      11

#define MDT_PREV_X      202
#define MDT_PREV_CAP_Y  76
#define MDT_PREV_CAP_H  12
#define MDT_PREV_Y      (MDT_PREV_CAP_Y + MDT_PREV_CAP_H)
#define MDT_PREV_H      93
#define MDT_PREV_W      117

#define MDT_PREV_IX0    (MDT_PREV_X + 2)
#define MDT_PREV_IX1    (MDT_PREV_X + MDT_PREV_W - 2)

#define MDT_PREV_STEP_W  7
#define MDT_PREV_STEP_WK 6

#define MDT_PREV_KB_X    (MDT_PREV_X + 2)
#define MDT_PREV_KB_W    12

#define MDT_PREV_STEP_X(i)  (MDT_PREV_X + 2 + (i) * MDT_PREV_STEP_W)
#define MDT_PREV_STEP_XK(i) (MDT_PREV_KB_X + MDT_PREV_KB_W + 3 + (i) * MDT_PREV_STEP_WK)

#define MDT_PREV_DR_RIGHT (MDT_PREV_STEP_X(15) + MDT_PREV_STEP_W - 1)
#define MDT_PREV_PR_RIGHT (MDT_PREV_STEP_XK(15) + MDT_PREV_STEP_WK - 1)

#define MDT_CELL_W      3
#define MDT_CELL_PAD    2
#define MDT_CELL_H      10
#define MDT_CELL_TOP(y) ((y) - 1)

#define MDT_PR_ROWH     3

#define MDT_LOOP_INTERVAL_MS 20
#define MDT_MENU_BTN_X   202
#define MDT_MENU_BTN_Y   0
#define MDT_VK_BTN_X     256
#define MDT_VK_BTN_Y     0
#define MDT_STEP_BTN_X   202
#define MDT_STEP_BTN_Y   39
#define MDT_PLAY_BTN_X   256
#define MDT_PLAY_BTN_Y   39

#define MDT_HDR_PAT_X     2
#define MDT_TYPE_BOX_X    22
#define MDT_TYPE_BOX_Y    0
#define MDT_TYPE_BOX_W    50
#define MDT_TYPE_BOX_H    MDT_HDR_H
#define MDT_HDR_TYPE_TX   24
#define MDT_HDR_STATS_X   76
#define MDT_HDR_MODE_X    130
#define MDT_HDR_BPM_X     160

#define MDT_MENU_X0      202
#define MDT_MENU_Y0      0
#define MDT_MENU_W       117
#define MDT_MENU_H       181
#define MDT_MENU_ROWS_Y0 (MDT_MENU_Y0 + 12)
#define MDT_MENU_ROW_H   10
#define MDT_MENU_VALUE_Y (MDT_MENU_Y0 + 20)
#define MDT_MENU_HINT1_Y 150
#define MDT_MENU_HINT2_Y 162
#define MDT_MENU_VALUE_H  84
#define MDT_MENU_TB_X1    (MDT_MENU_X0 + 4)
#define MDT_MENU_TB_X2    (MDT_MENU_X0 + 4 + TouchButton::BUTTON_SIZE_X + 6)
#define MDT_MENU_TB_ADJ_Y 100
#define MDT_MENU_TB_OK_Y  142

#define MDT_EMPTY_SLOT_IDX ((uint8_t)(NUM_DRUMSET_CONFIG - 1))
#define MDT_MAX_SAMPLE_IDX MDT_EMPTY_SLOT_IDX

enum {
  MDT_COL_NOTE = 0,
  MDT_COL_VEL,
  MDT_COL_INS,
  MDT_COL_CMD,
  MDT_COL_PAR,
  MDT_COL_PATTERN,
  MDT_COL_COUNT
};

// ===========================================================================
//  STATE
// ===========================================================================
static uint8_t mdt_cursor_row = 0;
static uint8_t mdt_cursor_col = MDT_COL_NOTE;
static uint8_t mdt_prev_play_row = 0xFF;
static uint8_t mdt_last_pattern = 0xFF;
static uint8_t mdt_last_track = 0xFF;
static bool    mdt_edit_state = false;
static bool    mdt_last_running = false;

static uint8_t mdt_view_offset = 0;
static bool    mdt_view_changed = false;

static bool    mdt_full_redraw = true;
static bool    mdt_light_redraw = false;
static bool    mdt_preview_dirty = true;
static bool    mdt_force_preview = false;
static bool    mdt_cursor_moved = false;
static uint8_t mdt_prev_cursor_row = 0xFF;
static uint8_t mdt_prev_cursor_col = 0xFF;
static bool    mdt_prev_edit_state = false;

static bool    mdt_status_force_full = true;

static bool    mdt_step_recording = false;

static uint8_t mdt_pitch_buffer = 72;
static uint8_t mdt_buf_note = 60;
static uint8_t mdt_buf_vel = 100;
static uint8_t mdt_buf_drum_vel = 120;

static uint8_t mdt_nopv_row = 0xFF;
static uint8_t mdt_nopv_pat = 0xFF;

static bool    mdt_drum_preview_valid = false;
static bool    mdt_note_preview_valid = false;

static uint8_t mdt_preview_active_notes[16];
static uint8_t mdt_preview_active_count = 0;
static constexpr uint16_t MDT_PREVIEW_HOLD_MS = 500;
static elapsedMillis     mdt_preview_hold_timer = 0;

static char mdt_last_oled_l[20] = { 0 };
static char mdt_last_oled_r[20] = { 0 };

enum mdt_menu_state_e {
  MDT_MENU_OFF = 0,
  MDT_MENU_MAIN,
  MDT_MENU_COPY,
  MDT_MENU_SWAP,
  MDT_MENU_FILL_SAMPLE,
  MDT_MENU_FILL_DIV,
  MDT_MENU_TRANSPOSE,
  MDT_MENU_CLEAR_PAT_CONFIRM,
  MDT_MENU_CLEAR_ALL_CONFIRM,
  MDT_MENU_CLEAR_COLS,
  MDT_MENU_QUANTIZE,
  MDT_MENU_GENERATE
};

enum mdt_menu_item_e {
  MDT_MI_CLOSE = 0,
  MDT_MI_STEP_REC,
  MDT_MI_PLAY_MODE,
  MDT_MI_CLEAR_PAT,
  MDT_MI_CLEAR_ALL,
  MDT_MI_COPY,
  MDT_MI_SWAP,
  MDT_MI_FILL,
  MDT_MI_TRANSPOSE,
  MDT_MI_CLEAR_COLS,
  MDT_MI_QUANTIZE,
  MDT_MI_GENERATE,
  MDT_MI_COUNT
};

static uint8_t mdt_menu_state = MDT_MENU_OFF;
static uint8_t mdt_menu_highlight = 0;
static int16_t mdt_menu_value = 0;
static int16_t mdt_menu_value2 = 0;

static uint8_t mdt_menu_backup_notes[16];
static uint8_t mdt_menu_backup_vel[16];
static bool    mdt_menu_has_backup = false;
static uint8_t mdt_menu_backup_fx_inst[16];
static uint8_t mdt_menu_backup_fx_cmd[16];
static uint8_t mdt_menu_backup_fx_val[16];

static uint8_t mdt_form_field = 0;
static bool    mdt_form_edit = false;

static uint8_t mdt_pr_last_pattern = 0xFF;
static uint8_t mdt_pr_last_ct = 0xFF;
static int16_t mdt_pr_last_lo = -1;
static int16_t mdt_pr_last_hi = -1;
static uint8_t mdt_pr_last_h = 0;
static uint8_t mdt_pr_last_cursor = 0xFF;

static uint8_t mdt_dr_last_pattern = 0xFF;
static uint8_t mdt_dr_last_cursor = 0xFF;
static uint8_t mdt_dr_last_play = 0xFF;
static uint8_t mdt_dr_count = 0;
static uint8_t mdt_dr_uniq[16];
static uint8_t mdt_dr_uniq_class[16];
static uint8_t mdt_dr_row_h = 0;
static uint8_t mdt_dr_yt = 0;
static uint8_t mdt_dr_yb = 0;

// ===========================================================================
//  VOICE / BANK / POOL OVERRIDE STATE
// ===========================================================================
uint8_t mdt_manual_voice[NUM_DEXED];
uint8_t mdt_manual_bank [NUM_DEXED];
uint8_t mdt_manual_pool [NUM_DEXED];
bool    mdt_manual_active[NUM_DEXED];

static char    mdt_vn_cur_buf[VOICE_NAME_LEN + 1] = { 0 };
static char    mdt_vn_tgt_buf[VOICE_NAME_LEN + 1] = { 0 };
static uint8_t mdt_vn_cur_pool  = 0xFF;
static uint8_t mdt_vn_cur_bank  = 0xFF;
static uint8_t mdt_vn_cur_voice = 0xFF;
static uint8_t mdt_vn_tgt_pool  = 0xFF;
static uint8_t mdt_vn_tgt_bank  = 0xFF;
static uint8_t mdt_vn_tgt_voice = 0xFF;

static char    mdt_bn_cur_buf[BANK_NAME_LEN + 1] = { 0 };
static char    mdt_bn_tgt_buf[BANK_NAME_LEN + 1] = { 0 };
static uint8_t mdt_bn_cur_pool = 0xFF;
static uint8_t mdt_bn_cur_bank = 0xFF;
static uint8_t mdt_bn_tgt_pool = 0xFF;
static uint8_t mdt_bn_tgt_bank = 0xFF;

static const char* _mdt_voice_name_cur(uint8_t pool, uint8_t bank, uint8_t voice)
{
  if (mdt_vn_cur_pool  != pool ||
      mdt_vn_cur_bank  != bank ||
      mdt_vn_cur_voice != voice) {
    mdt_vn_cur_pool  = pool;
    mdt_vn_cur_bank  = bank;
    mdt_vn_cur_voice = voice;
    mdt_vn_cur_buf[0] = '\0';
    get_voice_name(pool, bank, voice, mdt_vn_cur_buf);
  }
  return mdt_vn_cur_buf;
}

static const char* _mdt_voice_name_tgt(uint8_t pool, uint8_t bank, uint8_t voice)
{
  if (mdt_vn_tgt_pool  != pool ||
      mdt_vn_tgt_bank  != bank ||
      mdt_vn_tgt_voice != voice) {
    mdt_vn_tgt_pool  = pool;
    mdt_vn_tgt_bank  = bank;
    mdt_vn_tgt_voice = voice;
    mdt_vn_tgt_buf[0] = '\0';
    get_voice_name(pool, bank, voice, mdt_vn_tgt_buf);
  }
  return mdt_vn_tgt_buf;
}

static const char* _mdt_bank_name_cur(uint8_t pool, uint8_t bank)
{
  if (mdt_bn_cur_pool != pool || mdt_bn_cur_bank != bank) {
    mdt_bn_cur_pool = pool;
    mdt_bn_cur_bank = bank;
    mdt_bn_cur_buf[0] = '\0';
    get_bank_name(pool, bank, mdt_bn_cur_buf);
  }
  return mdt_bn_cur_buf;
}

static const char* _mdt_bank_name_tgt(uint8_t pool, uint8_t bank)
{
  if (mdt_bn_tgt_pool != pool || mdt_bn_tgt_bank != bank) {
    mdt_bn_tgt_pool = pool;
    mdt_bn_tgt_bank = bank;
    mdt_bn_tgt_buf[0] = '\0';
    get_bank_name(pool, bank, mdt_bn_tgt_buf);
  }
  return mdt_bn_tgt_buf;
}

FLASHMEM uint8_t mdt_get_current_voice(uint8_t dx_idx) {
  if (dx_idx >= NUM_DEXED) return 0;
  return mdt_manual_active[dx_idx] ? mdt_manual_voice[dx_idx]
                                   : configuration.dexed[dx_idx].voice;
}
FLASHMEM uint8_t mdt_get_current_bank(uint8_t dx_idx) {
  if (dx_idx >= NUM_DEXED) return 0;
  return mdt_manual_active[dx_idx] ? mdt_manual_bank[dx_idx]
                                   : configuration.dexed[dx_idx].bank;
}
FLASHMEM uint8_t mdt_get_current_pool(uint8_t dx_idx) {
  if (dx_idx >= NUM_DEXED) return 0;
  return mdt_manual_active[dx_idx] ? mdt_manual_pool[dx_idx]
                                   : configuration.dexed[dx_idx].pool;
}
FLASHMEM bool mdt_voice_override_active(uint8_t dx_idx) {
  return (dx_idx < NUM_DEXED) && mdt_manual_active[dx_idx];
}

static FLASHMEM void _mdt_load_manual_voice_now(uint8_t dx_idx)
{
  if (dx_idx >= NUM_DEXED) return;
  const uint8_t sp = configuration.dexed[dx_idx].pool;
  const uint8_t sb = configuration.dexed[dx_idx].bank;
  const uint8_t sv = configuration.dexed[dx_idx].voice;

  load_sd_voice(mdt_manual_pool[dx_idx], mdt_manual_bank[dx_idx],
                mdt_manual_voice[dx_idx], dx_idx);

  configuration.dexed[dx_idx].pool  = sp;
  configuration.dexed[dx_idx].bank  = sb;
  configuration.dexed[dx_idx].voice = sv;
}

static FLASHMEM void _mdt_apply_voice_fx(uint8_t dx_idx, uint8_t cmd, uint8_t val)
{
  if (dx_idx >= NUM_DEXED) return;

  if (cmd == MDT_FX_VOICE_CLR) {
    const bool was_active = mdt_manual_active[dx_idx];
    mdt_manual_active[dx_idx] = false;
    if (was_active) {
      load_sd_voice(configuration.dexed[dx_idx].pool,
                    configuration.dexed[dx_idx].bank,
                    configuration.dexed[dx_idx].voice, dx_idx);
    }
    return;
  }

  if (!mdt_manual_active[dx_idx]) {
    mdt_manual_pool [dx_idx] = configuration.dexed[dx_idx].pool;
    mdt_manual_bank [dx_idx] = configuration.dexed[dx_idx].bank;
    mdt_manual_voice[dx_idx] = configuration.dexed[dx_idx].voice;
    mdt_manual_active[dx_idx] = true;
  }

  switch (cmd) {
    case MDT_FX_VOICE_UP:
      mdt_manual_voice[dx_idx] = (mdt_manual_voice[dx_idx] + 1) % MAX_VOICES;
      break;
    case MDT_FX_VOICE_DN:
      mdt_manual_voice[dx_idx] = (mdt_manual_voice[dx_idx] == 0)
                                 ? (MAX_VOICES - 1)
                                 : (mdt_manual_voice[dx_idx] - 1);
      break;
    case MDT_FX_VOICE_SEL:
      mdt_manual_voice[dx_idx] = constrain(val, 0, MAX_VOICES - 1);
      break;
    case MDT_FX_BANK_SEL:
      mdt_manual_bank[dx_idx] = constrain(val, 0, MAX_BANKS - 1);
      break;
    case MDT_FX_POOL_SEL:
      mdt_manual_pool[dx_idx] = constrain(val, 0, DEXED_POOLS - 1);
      break;
    default: return;
  }

  _mdt_load_manual_voice_now(dx_idx);
}

FLASHMEM void mdt_reset_all_voice_overrides(void)
{
  for (uint8_t i = 0; i < NUM_DEXED; i++) {
    if (mdt_manual_active[i]) {
      mdt_manual_active[i] = false;
      load_sd_voice(configuration.dexed[i].pool,
                    configuration.dexed[i].bank,
                    configuration.dexed[i].voice, i);
    }
  }
}

static FLASHMEM uint8_t _mdt_voice_fx_target_instance(uint8_t track,
                                                      uint8_t pattern,
                                                      uint8_t step)
{
  const uint8_t inst = seq_effective_instrument(track, pattern, step);
  if (inst < 2)   return inst;
  if (inst == 70) return 2;
  if (inst == 71) return 3;
  return 0xFF;
}

// ===========================================================================
//  FORWARD DECLARATIONS
// ===========================================================================
static void _mdt_draw_header(void);
static FLASHMEM void mdt_apply_fx_to_instrument(uint8_t inst, uint8_t cmd, uint8_t val);
static void _mdt_draw_col_captions(void);
static void _mdt_draw_all_rows(uint8_t cursor_row, uint8_t cursor_col, bool edit_state);
static void _mdt_redraw_left_rows(void);
static void _mdt_status_cache_reset(void);
static void _mdt_draw_status(uint8_t cursor_row, uint8_t cursor_col, bool edit_state);
static void _mdt_draw_pattern_preview(uint8_t cursor_row);
static void _mdt_draw_row(uint8_t row, uint8_t cursor_row, uint8_t cursor_col, bool edit_state);
static void _mdt_draw_single_cell(uint8_t row, uint8_t col,
  uint8_t cursor_row, uint8_t cursor_col,
  bool edit_state, bool is_playhead);
static void _mdt_draw_st_cell(uint8_t row, bool is_playhead, bool is_cursor_row);
static void _mdt_draw_info_cell(uint8_t row, bool is_playhead, bool is_cursor_row);
static void _mdt_full_redraw_body(void);
static void _mdt_light_redraw_body(void);
static void _mdt_preview_invalidate(void);
static void _mdt_draw_touch_buttons(void);
static void _mdt_draw_vk_step_buttons(void);
static void _mdt_draw_type_button(void);
static void _mdt_draw_vk_latch_button(void);
static void _mdt_touch_handler(void);
static void _mdt_update_playhead(void);
static void _mdt_open_menu(void);
static void _mdt_close_menu(void);
static void _mdt_menu_full_redraw(void);
static void _mdt_menu_draw_row(uint8_t i, bool selected);
static void _mdt_menu_pattern_preview(uint16_t x, uint16_t y, uint8_t pat, const char* label);
static void _mdt_menu_draw_value_line(void);
static void _mdt_form_draw_all(void);
static void _mdt_menu_execute(void);
static void _mdt_periodic(void);
static void _mdt_menu_adjust(int8_t dir);
static void _mdt_menu_back(void);
static bool _mdt_handle_step_recording(void);
static void _mdt_step_recording_insert_latch(void);
static void _mdt_step_recording_insert_rest(void);
static void _mdt_open_virtual_keyboard(void);
static void _mdt_close_virtual_keyboard(void);
static void _mdt_cycle_pattern_type(void);
static void _mdt_backup_pattern(void);
static void _mdt_restore_pattern(void);
static void _mdt_clear_backup(void);
static bool _mdt_edit_value(uint8_t row, uint8_t col, int16_t step);
static bool _mdt_scroll_to_pattern(int8_t delta);
static void _mdt_set_step_recording(bool on);
static void _mdt_preview_active_sample(void);
static void _mdt_preview_step(uint8_t row);
static uint8_t _mdt_drum_index_for_step(uint8_t note, uint8_t vel);
static void _mdt_step_rec_store(uint8_t kind);
static void _mdt_vk_redraw_top(void);
static void _mdt_draw_vk_adv_strip(void);
static void _mdt_draw_vk_velocity_bar(void);

// ===========================================================================
//  UTIL
// ===========================================================================
static inline bool mdt_is_vk_mode(void) {
  return seq.cycle_touch_element == 1;
}

static inline bool mdt_is_drum_pattern(void) {
  return (seq.content_type[seq.active_pattern] == 0);
}

static inline uint8_t mdt_row_display_y(uint8_t row) {
  if (row >= MDT_ROWS) return 0xFF;
  if (mdt_is_vk_mode()) {
    if (row < mdt_view_offset) return 0xFF;
    if (row >= (uint8_t)(mdt_view_offset + MDT_VK_VISIBLE_ROWS)) return 0xFF;
    return MDT_DATA_Y + (uint8_t)(row - mdt_view_offset) * MDT_ROW_H;
  }
  return MDT_DATA_Y + row * MDT_ROW_H;
}

static bool mdt_ensure_cursor_visible(void) {
  uint8_t nv = 0;
  if (mdt_is_vk_mode()) {
    nv = (uint8_t)((mdt_cursor_row / MDT_VK_VISIBLE_ROWS) * MDT_VK_VISIBLE_ROWS);
  }
  if (nv != mdt_view_offset) {
    mdt_view_offset = nv;
    mdt_view_changed = true;
    return true;
  }
  return false;
}

static inline bool _mdt_step_is_empty(uint8_t ct, uint8_t note, uint8_t vel) {
  (void)ct; (void)vel;
  return (note == 0);
}

static inline void _mdt_reset_previews(void) {
  mdt_drum_preview_valid = false;
  mdt_note_preview_valid = false;
}

static FLASHMEM void _mdt_fmt_note(char* out, uint8_t note);

static inline uint8_t _mdt_vk_velocity_or(uint8_t fallback)
{
  if (seq.cycle_touch_element == 1) {
    uint8_t v = ts.virtual_keyboard_velocity;
    if (v < 1)   v = 1;
    if (v > 127) v = 127;
    return v;
  }
  return fallback;
}

static FLASHMEM bool _mdt_insert_value(uint8_t* out_note, uint8_t* out_vel)
{
  const uint8_t pat = seq.active_pattern;
  const uint8_t ct = seq.content_type[pat];
#if NUM_DRUMS > 0
  if (ct == 0) {
    if (!mdt_drum_preview_valid || activeSample >= MDT_EMPTY_SLOT_IDX) return false;
    const uint8_t sel = drum_config[activeSample].midinote;
    if (sel == 0) return false;
    if (sel > 209) { *out_note = mdt_pitch_buffer; *out_vel = sel; }
    else { *out_note = sel;              *out_vel = _mdt_vk_velocity_or(mdt_buf_drum_vel); }
    return true;
  }
#else
  if (ct == 0) return false;
#endif
  if (!mdt_note_preview_valid || mdt_buf_note == 0) return false;
  uint8_t v = mdt_buf_vel;
  if (ct == 2) { if (v < 200 || v > 205) v = 200; }
  else if (ct == 1) v = _mdt_vk_velocity_or(v > 127 ? 100 : v);
  *out_note = mdt_buf_note;
  *out_vel = v;
  return true;
}

static FLASHMEM bool _mdt_row_insert_preview(uint8_t row, uint8_t* out_note, uint8_t* out_vel)
{
  const uint8_t pat = seq.active_pattern;
  if (mdt_nopv_row != 0xFF && (mdt_cursor_row != mdt_nopv_row || pat != mdt_nopv_pat))
    mdt_nopv_row = 0xFF;
  if (row != mdt_cursor_row || mdt_cursor_col == MDT_COL_PATTERN) return false;
  if (row == mdt_nopv_row) return false;
  if (!_mdt_step_is_empty(seq.content_type[pat], seq.note_data[pat][row], seq.vel[pat][row]))
    return false;
  return _mdt_insert_value(out_note, out_vel);
}

static FLASHMEM bool _mdt_insert_buffer_into_cursor_step(void)
{
  mdt_nopv_row = 0xFF;
  mdt_nopv_pat = 0xFF;

  if (mdt_cursor_col != MDT_COL_NOTE) return false;

  uint8_t in_note = 0, in_vel = 0;
  if (!_mdt_row_insert_preview(mdt_cursor_row, &in_note, &in_vel)) return false;

  const uint8_t pat = seq.active_pattern;
  const uint8_t row = mdt_cursor_row;
  seq.note_data[pat][row] = in_note;
  seq.vel[pat][row] = in_vel;

  if (seq.content_type[pat] == 0) {
    mdt_drum_preview_valid = true;
    _mdt_preview_active_sample();
  }
  else {
    mdt_buf_note = in_note;
    mdt_buf_vel = in_vel;
    mdt_note_preview_valid = true;
    if (!seq.running) _mdt_preview_step(row);
  }
  _mdt_preview_invalidate();
  mdt_preview_dirty = true;
  _mdt_status_cache_reset();
  return true;
}

static void _mdt_sync_activeSample_to_step(void);
static FLASHMEM void _mdt_load_buffer_from_cursor_step(void)
{
  const uint8_t pat = seq.active_pattern;
  const uint8_t row = mdt_cursor_row;
  if (row >= MDT_ROWS) return;
  const uint8_t note = seq.note_data[pat][row];
  const uint8_t vel = seq.vel[pat][row];
  if (note == 0 || note == 130) return;

  if (seq.content_type[pat] == 0) {
#if NUM_DRUMS > 0
    uint8_t idx;
    if (vel > 209) {
      idx = (uint8_t)(vel - 210);
      if (note >= 12 && note <= 108) mdt_pitch_buffer = note;
    }
    else if (activeSample < MDT_EMPTY_SLOT_IDX &&
      drum_config[activeSample].midinote == note) {
      idx = activeSample;
    }
    else {
      idx = _mdt_drum_index_for_step(note, vel);
    }
    if (idx < MDT_EMPTY_SLOT_IDX) {
      activeSample = idx;
      if (vel > 0 && vel <= 127) mdt_buf_drum_vel = vel;
      mdt_drum_preview_valid = true;
    }
#endif
  }
  else {
    mdt_buf_note = note;
    mdt_buf_vel = vel;
    mdt_note_preview_valid = true;
  }
}

static FLASHMEM void _mdt_fmt_note(char* out, uint8_t note)
{
  if (note == 0) { out[0] = '-'; out[1] = '-'; out[2] = '-'; out[3] = 0; return; }
  if (note == 130) { out[0] = 'L'; out[1] = 'A'; out[2] = 'T'; out[3] = 0; return; }
  const char* n = noteNames[note % 12];
  out[0] = n[0];
  out[1] = (n[1] != '\0') ? n[1] : '-';
  int8_t oct = (int8_t)(note / 12) - 1;
  if (oct < 0) oct = 0;
  if (oct > 9) oct = 9;
  out[2] = (char)('0' + oct);
  out[3] = 0;
}

static FLASHMEM void _mdt_chord_name3(char* out, uint8_t chord_type)
{
  static const char* names3[6] = { "MAJ", "MIN", "SEV", "AUG", "DIM", "MJ7" };
  if (chord_type > 5) chord_type = 0;
  memcpy(out, names3[chord_type], 3);
  out[3] = 0;
}

static FLASHMEM const char* _mdt_chord_full_name(uint8_t chord_type)
{
  static const char* names[6] = { "MAJOR", "MINOR", "SEVEN", "AUGMENTED",
                                  "DIMINISHED", "MAJOR 7" };
  if (chord_type > 5) chord_type = 0;
  return names[chord_type];
}

static FLASHMEM uint16_t _mdt_chord_color(uint8_t chord_type)
{
  static const uint16_t cols[6] = { GREEN, COLOR_INSTR, COLOR_CHORDS,
                                    YELLOW, RED, PINK };
  if (chord_type > 5) chord_type = 0;
  return cols[chord_type];
}

static FLASHMEM uint16_t _mdt_type_color(void)
{
  switch (seq.content_type[seq.active_pattern]) {
  case 0:  return COLOR_DRUMS;
  case 1:  return COLOR_INSTR;
  case 2:  return COLOR_CHORDS;
  case 3:  return COLOR_ARP;
  default: return COLOR_SYSTEXT;
  }
}

static FLASHMEM const char* _mdt_type_label(void)
{
  switch (seq.content_type[seq.active_pattern]) {
  case 0: return "DRM";
  case 1: return "INS";
  case 2: return "CHD";
  case 3: return "SLP";
  default: return "---";
  }
}

static FLASHMEM uint16_t _mdt_shade_by_velocity(uint16_t color, uint8_t vel)
{
  if (vel > 127) vel = 127;
  const uint8_t pct = (uint8_t)(30 + (uint32_t)vel * 70 / 127);
  uint16_t r = ((color >> 11) & 0x1F) * pct / 100;
  uint16_t g = ((color >> 5) & 0x3F) * pct / 100;
  uint16_t b = (color & 0x1F) * pct / 100;
  if (r > 0x1F) r = 0x1F;
  if (g > 0x3F) g = 0x3F;
  if (b > 0x1F) b = 0x1F;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static FLASHMEM void _mdt_ins_label(char* out, size_t n, uint8_t id)
{
  if (id == 0xFF) { snprintf(out, n, "--- TRACK DEFAULT"); return; }
  switch (id) {
  case 0:  snprintf(out, n, "DX1  DEXED #1");      return;
  case 1:  snprintf(out, n, "DX2  DEXED #2");      return;
  case 70: snprintf(out, n, "DX3  DEXED #3");      return;
  case 71: snprintf(out, n, "DX4  DEXED #4");      return;
  case 72: snprintf(out, n, "GRA  GRANULAR");      return;
  case 2:  snprintf(out, n, "EP   EPIANO");        return;
  case 3:  snprintf(out, n, "MS1  MICROSYNTH #1"); return;
  case 4:  snprintf(out, n, "MS2  MICROSYNTH #2"); return;
  case 5:  snprintf(out, n, "BRD  BRAIDS");        return;
  case 64: snprintf(out, n, "CV1  CV-OUT #1");     return;
  default: break;
  }
  if (id >= 6 && id < (uint8_t)(6 + NUM_MULTISAMPLES)) {
    snprintf(out, n, "MSP#%02u MULTISAMPLE", (unsigned)(id - 6)); return;
  }
  if (id >= 16 && id < 32) { snprintf(out, n, "USB#%02u  USB MIDI", (unsigned)(id - 15)); return; }
  if (id >= 32 && id < 48) { snprintf(out, n, "DIN#%02u  DIN MIDI", (unsigned)(id - 31)); return; }
  if (id >= 48 && id < 64) { snprintf(out, n, "INT#%02u  INT. MIDI", (unsigned)(id - 47)); return; }
  snprintf(out, n, "???  UNKNOWN %u", (unsigned)id);
}

static FLASHMEM const uint8_t* _mdt_ins_order(uint8_t* count_out)
{
  static uint8_t order[1 + 9 + NUM_MULTISAMPLES + 16 + 16 + 16 + 1];
  static uint8_t count = 0;
  if (count == 0) {
    uint8_t n = 0;
    order[n++] = 0xFF;
    order[n++] = 0;   order[n++] = 1;  order[n++] = 70;  order[n++] = 71;
    order[n++] = 72;  order[n++] = 2;  order[n++] = 3;   order[n++] = 4;
    order[n++] = 5;
    for (uint8_t i = 0; i < NUM_MULTISAMPLES; i++) order[n++] = 6 + i;
    for (uint8_t i = 16; i < 32; i++)              order[n++] = i;
    for (uint8_t i = 32; i < 48; i++)              order[n++] = i;
    for (uint8_t i = 48; i < 64; i++)              order[n++] = i;
    order[n++] = 64;
    count = n;
  }
  if (count_out) *count_out = count;
  return order;
}

static FLASHMEM int16_t _mdt_ins_find(uint8_t value)
{
  uint8_t count = 0;
  const uint8_t* order = _mdt_ins_order(&count);
  for (uint8_t i = 0; i < count; i++) if (order[i] == value) return (int16_t)i;
  return -1;
}

static FLASHMEM uint8_t _mdt_drum_index_for_step(uint8_t note, uint8_t vel)
{
#if NUM_DRUMS > 0
  if (vel > 209) {
    const uint8_t idx = vel - 210;
    return (idx < NUM_DRUMSET_CONFIG - 1) ? idx : 0xFF;
  }
  if (note == 0 || note == 130) return 0xFF;
  for (uint8_t n = 0; n < NUM_DRUMSET_CONFIG - 1; n++) {
    if (drum_config[n].midinote == note && drum_config[n].midinote != 0) return n;
  }
#else
  (void)note; (void)vel;
#endif
  return 0xFF;
}

static FLASHMEM uint8_t _mdt_cycle_activeSample(uint8_t current, int8_t dir)
{
  const int16_t maxIdx = (int16_t)MDT_MAX_SAMPLE_IDX;
  if (dir == 0) return current;
  int16_t idx = (current > maxIdx) ? 0 : (int16_t)current;
  for (int16_t tries = 0; tries <= maxIdx; tries++) {
    idx += (dir > 0) ? 1 : -1;
    if (idx < 0)      idx = maxIdx;
    if (idx > maxIdx) idx = 0;
#if NUM_DRUMS > 0
    if (idx == maxIdx) break;
    if (drum_config[idx].midinote != 0) break;
#else
    break;
#endif
  }
  return (uint8_t)idx;
}

static FLASHMEM void _mdt_sync_activeSample_to_step(void)
{
#if NUM_DRUMS > 0
  if (!mdt_is_drum_pattern()) return;
  if (mdt_cursor_col != MDT_COL_NOTE) return;
  if (mdt_cursor_row >= MDT_ROWS) return;

  const uint8_t cur_note = seq.note_data[seq.active_pattern][mdt_cursor_row];
  const uint8_t cur_vel = seq.vel[seq.active_pattern][mdt_cursor_row];

  if (cur_note == 0) return;

  uint8_t step_sample = 0xFF;
  if (cur_vel > 209) {
    step_sample = (uint8_t)(cur_vel - 210);
    if (cur_note >= 12 && cur_note <= 108) mdt_pitch_buffer = cur_note;
  }
  else if (cur_note != 0) {
    if (activeSample < MDT_EMPTY_SLOT_IDX &&
      drum_config[activeSample].midinote == cur_note &&
      drum_config[activeSample].midinote < 210) {
      step_sample = activeSample;
    }
    else {
      step_sample = _mdt_drum_index_for_step(cur_note, cur_vel);
    }
  }

  if (step_sample < MDT_EMPTY_SLOT_IDX) activeSample = step_sample;
#endif
}

static FLASHMEM void _mdt_preview_active_sample(void)
{
#if NUM_DRUMS > 0
  if (seq.running) return;
  if (activeSample >= MDT_EMPTY_SLOT_IDX) return;

  const uint8_t sel_midi = drum_config[activeSample].midinote;
  if (sel_midi == 0) return;

  if (sel_midi > 209) {
    set_sample_pitch(activeSample,
      (float)pow(2.0, (mdt_pitch_buffer - 72) / 12.0) * get_sample_p_offset(activeSample));
    handleNoteOn(drum_midi_channel, sel_midi, 90, 0);
  }
  else {
    handleNoteOn(drum_midi_channel, sel_midi, 90, 0);
  }
#endif
}

static FLASHMEM void _mdt_preview_release_all(void)
{
  if (mdt_preview_active_count == 0) return;

  const uint8_t track = seq.selected_track;
  const uint8_t inst = seq.instrument[track];

  for (uint8_t i = 0; i < mdt_preview_active_count; i++) {
    const uint8_t n = mdt_preview_active_notes[i];

    if (inst < 2 || inst == 70 || inst == 71) {
      const uint8_t ii = (inst < 2) ? inst : (uint8_t)(inst - 68);
      if (ii < NUM_DEXED)
        handleNoteOff(configuration.dexed[ii].midi_channel, n, 0, 0);
    }
#ifdef GRANULAR
    else if (inst == 72) {
      handleNoteOff(granular_params.midi_channel, n, 0, 0);
    }
#endif
    else if (inst == 2) {
      handleNoteOff(configuration.epiano.midi_channel, n, 0, 0);
    }
    else if (inst == 5) {
      handleNoteOff(braids_osc.midi_channel, n, 0, 4);
    }
    else if (inst > 5 && inst < 16) {
      handleNoteOff(msp[inst - 6].midi_channel, n, 0, 0);
    }
#ifdef MIDI_DEVICE_USB_HOST
    else if (inst > 15 && inst < 32) {
      handleNoteOff(inst - 15, n, 0, 1);
    }
#endif
#ifdef MIDI_DEVICE_DIN
    else if (inst > 31 && inst < 48) {
      handleNoteOff(inst - 31, n, 0, 2);
    }
#endif
    else if (inst > 47 && inst < 64) {
      handleNoteOff(inst - 47, n, 0, 3);
    }
    else if (inst == 64) {
      handleNoteOff(21, n, 0, 0);
    }
    else {
      handleNoteOff(configuration.epiano.midi_channel, n, 0, 0);
    }
  }

  mdt_preview_active_count = 0;
}

static FLASHMEM void _mdt_preview_step(uint8_t row)
{
  if (seq.running) return;
  if (row >= MDT_ROWS) return;

  _mdt_preview_release_all();

  const uint8_t pat = seq.active_pattern;
  const uint8_t note = seq.note_data[pat][row];
  const uint8_t vel = seq.vel[pat][row];
  const uint8_t ct = seq.content_type[pat];
  const uint8_t fxc = seq.fx_cmd[pat][row];
  const uint8_t fxv = seq.fx_val[pat][row];
  const uint8_t track = seq.selected_track;

  const uint8_t eff_inst = seq_effective_instrument(track, pat, row);

  if (fxc != MDT_FX_NONE) {
    const bool is_global_cmd =
      mdt_cmd_is_arp(fxc) ||
      fxc == MDT_FX_TRN ||
      fxc == MDT_FX_GAT ||
      fxc == MDT_FX_HLD;

    if (is_global_cmd) {
      seq_apply_step_fx(track, pat, row);
    }
    else if (mdt_cmd_is_voice(fxc)) {
      // Voice overrides are applied globally when the sequencer fires
      // the step. Preview does not touch the DX voice so that stepping
      // through the pattern with the cursor does not blow away the
      // persistent configuration.dexed[] state on every cursor move.
    }
    else {
      switch (ct) {
      case 0:
        if (note > 0 && mdt_cmd_is_drum_audio(fxc)) {
          g_drum_fx_override_cmd = fxc;
          g_drum_fx_override_val = fxv;
        }
        break;

      case 1:
      case 2:
        mdt_apply_fx_to_instrument(eff_inst, fxc, fxv);
        break;

      case 3:
      default:
        break;
      }
    }
  }

  if (ct == 0 || ct == 3) {
    pattern_editor_play_current_step(row);
    return;
  }

  if (note == 0 || note == 130) return;

  const uint8_t pvel = (vel > 0 && vel < 128) ? vel : 100;

  auto trigger_note = [&](uint8_t in_note, uint8_t in_vel) {
    if (eff_inst < 2 || eff_inst == 70 || eff_inst == 71) {
      const uint8_t i = (eff_inst < 2) ? eff_inst : (uint8_t)(eff_inst - 68);
      if (i < NUM_DEXED)
        handleNoteOn(configuration.dexed[i].midi_channel, in_note, in_vel, 0);
    }
#ifdef GRANULAR
    else if (eff_inst == 72) {
      handleNoteOn(granular_params.midi_channel, in_note, in_vel, 0);
    }
#endif
    else if (eff_inst == 2) {
      handleNoteOn(configuration.epiano.midi_channel, in_note, in_vel, 0);
    }
    else if (eff_inst == 5) {
      handleNoteOn(braids_osc.midi_channel, in_note, in_vel, 4);
    }
    else if (eff_inst > 5 && eff_inst < 16) {
      handleNoteOn(msp[eff_inst - 6].midi_channel, in_note, in_vel, 0);
    }
#ifdef MIDI_DEVICE_USB_HOST
    else if (eff_inst > 15 && eff_inst < 32) {
      handleNoteOn(eff_inst - 15, in_note, in_vel, 1);
    }
#endif
#ifdef MIDI_DEVICE_DIN
    else if (eff_inst > 31 && eff_inst < 48) {
      handleNoteOn(eff_inst - 31, in_note, in_vel, 2);
    }
#endif
    else if (eff_inst > 47 && eff_inst < 64) {
      handleNoteOn(eff_inst - 47, in_note, in_vel, 3);
    }
    else if (eff_inst == 64) {
      handleNoteOn(21, in_note, in_vel, 0);
    }
    else {
      handleNoteOn(configuration.epiano.midi_channel, in_note, in_vel, 0);
    }

    if (mdt_preview_active_count < sizeof(mdt_preview_active_notes)) {
      mdt_preview_active_notes[mdt_preview_active_count++] = in_note;
      mdt_preview_hold_timer = 0;
    }
    };

  if ((ct == 2 || ct == 3) && vel >= 200 && vel <= 205) {
    const uint8_t chord_type = (uint8_t)(vel - 200);
    uint8_t count = seq.chord_key_ammount;
    if (count == 0) count = 4;
    if (count > 6)  count = 6;

    for (uint8_t i = 0; i < count; i++) {
      const int16_t cn = (int16_t)note + seq.arps[chord_type][i];
      if (cn < 0 || cn > 127) continue;
      trigger_note((uint8_t)cn, pvel);
    }
    return;
  }

  trigger_note(note, pvel);
}

static FLASHMEM bool _mdt_commit_sample_to_step(void)
{
#if NUM_DRUMS > 0
  if (!mdt_is_drum_pattern()) return false;
  if (mdt_cursor_col != MDT_COL_NOTE) return false;

  const uint8_t pat = seq.active_pattern;
  const uint8_t row = mdt_cursor_row;

  if (activeSample >= MDT_EMPTY_SLOT_IDX) {
    seq.note_data[pat][row] = 0;
    seq.vel[pat][row] = 0;
  }
  else {
    const uint8_t sel_midi = drum_config[activeSample].midinote;
    const bool    sel_is_pitch = (sel_midi > 209);
    if (sel_is_pitch) {
      seq.vel[pat][row] = sel_midi;
      seq.note_data[pat][row] = mdt_pitch_buffer;
    }
    else {
      seq.note_data[pat][row] = sel_midi;
      seq.vel[pat][row] = mdt_buf_drum_vel;
    }
  }

  mdt_drum_preview_valid = true;
  mdt_dr_last_cursor = 0xFF;
  mdt_preview_dirty = true;
  return true;
#else
  return false;
#endif
}

static FLASHMEM void _mdt_set_oled_help(const char* left_enc_text, const char* right_enc_text)
{
  if (!left_enc_text)  left_enc_text = "";
  if (!right_enc_text) right_enc_text = "";

  const char* left_text = left_enc_text;
  const char* right_text = right_enc_text;

  if (strcmp(left_text, mdt_last_oled_l) != 0) {
    strncpy(mdt_last_oled_l, left_text, sizeof(mdt_last_oled_l) - 1);
    mdt_last_oled_l[sizeof(mdt_last_oled_l) - 1] = '\0';
    helptext_oled_l(left_text);
  }
  if (strcmp(right_text, mdt_last_oled_r) != 0) {
    strncpy(mdt_last_oled_r, right_text, sizeof(mdt_last_oled_r) - 1);
    mdt_last_oled_r[sizeof(mdt_last_oled_r) - 1] = '\0';
    helptext_oled_r(right_text);
  }
}

static FLASHMEM void _mdt_update_oled_help(void)
{
  if (mdt_menu_state != MDT_MENU_OFF) {
    if (mdt_menu_state == MDT_MENU_MAIN) {
      _mdt_set_oled_help("R:TURN   P:OK", "L:EXIT PAGE");
    }
    else if (mdt_menu_state == MDT_MENU_QUANTIZE ||
      mdt_menu_state == MDT_MENU_GENERATE) {
      _mdt_set_oled_help(mdt_form_edit ? "R:VALUE  P:DONE" : "R:SELECT P:EDIT",
        "L:EXIT PAGE");
    }
    else {
      _mdt_set_oled_help("R:ADJUST P:OK", "L:EXIT PAGE");
    }
    return;
  }

  if (mdt_cursor_col == MDT_COL_PATTERN) {
    _mdt_set_oled_help("< > COLUMN", "< > CHANGEPATTERN");
    return;
  }

  {
    uint8_t pn, pv;
    if (!mdt_edit_state && _mdt_row_insert_preview(mdt_cursor_row, &pn, &pv)) {
      _mdt_set_oled_help("< > COLUMN", "< > ROW   PUSH: EDIT");
      return;
    }
  }

  if (mdt_is_drum_pattern() && mdt_cursor_col == MDT_COL_NOTE) {
    if (mdt_edit_state) {
      _mdt_set_oled_help("< > COLUMN", "< > SAMPLEP: CONFIRM");
    }
    else {
      _mdt_set_oled_help("< > COLUMN", "< > ROW   PUSH: EDIT");
    }
    return;
  }

  if (mdt_edit_state && mdt_is_drum_pattern() && mdt_cursor_col == MDT_COL_VEL &&
    seq.vel[seq.active_pattern][mdt_cursor_row] > 209) {
    _mdt_set_oled_help("< > COLUMN", "<> PITCHEDSMPL. NOTE");
    return;
  }

  if (mdt_edit_state) {
    switch (mdt_cursor_col) {
    case MDT_COL_NOTE: _mdt_set_oled_help("< > COLUMN", "< > NOTE  P: CONFIRM"); break;
    case MDT_COL_VEL:  _mdt_set_oled_help("< > COLUMN", "<>VELOCITYP: CONFIRM"); break;
    case MDT_COL_INS:  _mdt_set_oled_help("< > COLUMN", "< > INST  P: CONFIRM"); break;
    case MDT_COL_CMD:  _mdt_set_oled_help("< > COLUMN", "< > CMD   P: CONFIRM"); break;
    case MDT_COL_PAR:  _mdt_set_oled_help("< > COLUMN", "< > PARAM P: CONFIRM"); break;
    default:           _mdt_set_oled_help("< > COLUMN", "x:xxx     P: CONFIRM"); break;
    }
  }
  else {
    _mdt_set_oled_help("< > COLUMN", "< > ROW   PUSH: EDIT");
  }
}

// ===========================================================================
//  ACTIONS
// ===========================================================================
static FLASHMEM void _mdt_action_clear_pattern(void)
{
  const uint8_t pat = seq.active_pattern;

  memset(seq.note_data[pat], 0, sizeof(seq.note_data[pat]));
  memset(seq.vel[pat], 0, sizeof(seq.vel[pat]));

  // FX columns: INS = 0xFF (none), CMD = MDT_FX_NONE, PAR = 0
  memset(seq.fx_inst[pat], 0xFF, sizeof(seq.fx_inst[pat]));
  memset(seq.fx_cmd[pat], MDT_FX_NONE, sizeof(seq.fx_cmd[pat]));
  memset(seq.fx_val[pat], 0, sizeof(seq.fx_val[pat]));
}

static FLASHMEM void _mdt_action_clear_step(void)
{
  const uint8_t pat = seq.active_pattern;
  const uint8_t row = mdt_cursor_row;
  if (row >= MDT_ROWS) return;

  seq.note_data[pat][row] = 0;
  seq.vel[pat][row] = 0;
  seq.fx_inst[pat][row] = 0xFF;
  seq.fx_cmd[pat][row] = MDT_FX_NONE;
  seq.fx_val[pat][row] = 0;

  mdt_nopv_row = row;
  mdt_nopv_pat = pat;

  _mdt_draw_row(row, mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  _mdt_status_cache_reset();
  _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  _mdt_preview_invalidate();
  _mdt_draw_pattern_preview(mdt_cursor_row);
  mdt_preview_dirty = false;
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_action_clear_all_patterns(void)
{
  for (uint8_t i = 0; i < NUM_SEQ_PATTERN; i++) {
    memset(seq.note_data[i], 0, sizeof(seq.note_data[i]));
    memset(seq.vel[i], 0, sizeof(seq.vel[i]));
    memset(seq.fx_inst[i], 0xFF, sizeof(seq.fx_inst[i]));
    memset(seq.fx_cmd[i], MDT_FX_NONE, sizeof(seq.fx_cmd[i]));
    memset(seq.fx_val[i], 0, sizeof(seq.fx_val[i]));
    seq.content_type[i] = 0;
  }
}

static FLASHMEM void _mdt_action_copy_pattern(uint8_t dst)
{
  if (dst >= NUM_SEQ_PATTERN || dst == seq.active_pattern) return;
  memcpy(seq.note_data[dst], seq.note_data[seq.active_pattern], sizeof(seq.note_data[0]));
  memcpy(seq.vel[dst], seq.vel[seq.active_pattern], sizeof(seq.vel[0]));
  seq.content_type[dst] = seq.content_type[seq.active_pattern];
  memcpy(seq.fx_inst[dst], seq.fx_inst[seq.active_pattern], sizeof(seq.fx_inst[0]));
  memcpy(seq.fx_cmd[dst], seq.fx_cmd[seq.active_pattern], sizeof(seq.fx_cmd[0]));
  memcpy(seq.fx_val[dst], seq.fx_val[seq.active_pattern], sizeof(seq.fx_val[0]));
}

static FLASHMEM void _mdt_action_swap_pattern(uint8_t dst)
{
  if (dst >= NUM_SEQ_PATTERN || dst == seq.active_pattern) return;
  uint8_t tmp_notes[16], tmp_vel[16], tmp_fx_i[16], tmp_fx_c[16], tmp_fx_v[16];
  uint8_t tmp_ct;

  memcpy(tmp_notes, seq.note_data[dst], 16);
  memcpy(tmp_vel, seq.vel[dst], 16);
  memcpy(tmp_fx_i, seq.fx_inst[dst], 16);
  memcpy(tmp_fx_c, seq.fx_cmd[dst], 16);
  memcpy(tmp_fx_v, seq.fx_val[dst], 16);
  tmp_ct = seq.content_type[dst];

  memcpy(seq.note_data[dst], seq.note_data[seq.active_pattern], 16);
  memcpy(seq.vel[dst], seq.vel[seq.active_pattern], 16);
  memcpy(seq.fx_inst[dst], seq.fx_inst[seq.active_pattern], 16);
  memcpy(seq.fx_cmd[dst], seq.fx_cmd[seq.active_pattern], 16);
  memcpy(seq.fx_val[dst], seq.fx_val[seq.active_pattern], 16);
  seq.content_type[dst] = seq.content_type[seq.active_pattern];

  memcpy(seq.note_data[seq.active_pattern], tmp_notes, 16);
  memcpy(seq.vel[seq.active_pattern], tmp_vel, 16);
  memcpy(seq.fx_inst[seq.active_pattern], tmp_fx_i, 16);
  memcpy(seq.fx_cmd[seq.active_pattern], tmp_fx_c, 16);
  memcpy(seq.fx_val[seq.active_pattern], tmp_fx_v, 16);
  seq.content_type[seq.active_pattern] = tmp_ct;
}

static FLASHMEM void _mdt_action_fill_pattern(uint8_t sample, uint8_t step_div)
{
  uint8_t step = (step_div == 0) ? 4 : (step_div == 1) ? 2 : 1;
  const bool is_drum = (seq.content_type[seq.active_pattern] == 0 ||
    seq.content_type[seq.active_pattern] == 3);
  for (uint8_t i = 0; i < 16; i += step) {
    if (is_drum && sample < NUM_DRUMSET_CONFIG) {
      seq.note_data[seq.active_pattern][i] = drum_config[sample].midinote;
      seq.vel[seq.active_pattern][i] = 120;
    }
    else if (!is_drum) {
      seq.note_data[seq.active_pattern][i] = sample;
      seq.vel[seq.active_pattern][i] = 120;
    }
  }
}

static FLASHMEM void _mdt_action_transpose_pattern(int8_t amount)
{
  uint8_t tmp[16];
  memcpy(tmp, seq.note_data[seq.active_pattern], 16);
  const bool is_drum = (seq.content_type[seq.active_pattern] == 0 ||
    seq.content_type[seq.active_pattern] == 3);
  for (uint8_t i = 0; i < 16; i++) {
    if (tmp[i] == 0 || tmp[i] == 130) continue;
    if (is_drum) {
      if (seq.vel[seq.active_pattern][i] >= 210) {
        int16_t n = (int16_t)tmp[i] + amount;
        if (n >= 0 && n < 254) seq.note_data[seq.active_pattern][i] = (uint8_t)n;
      }
    }
    else {
      int16_t n = (int16_t)tmp[i] + amount;
      if (n > 0 && n < 254) seq.note_data[seq.active_pattern][i] = (uint8_t)n;
    }
  }
}

static FLASHMEM void _mdt_backup_pattern(void)
{
  memcpy(mdt_menu_backup_notes, seq.note_data[seq.active_pattern], 16);
  memcpy(mdt_menu_backup_vel, seq.vel[seq.active_pattern], 16);
  memcpy(mdt_menu_backup_fx_inst, seq.fx_inst[seq.active_pattern], 16);
  memcpy(mdt_menu_backup_fx_cmd, seq.fx_cmd[seq.active_pattern], 16);
  memcpy(mdt_menu_backup_fx_val, seq.fx_val[seq.active_pattern], 16);
  mdt_menu_has_backup = true;
}

static FLASHMEM void _mdt_restore_pattern(void)
{
  if (!mdt_menu_has_backup) return;
  memcpy(seq.note_data[seq.active_pattern], mdt_menu_backup_notes, 16);
  memcpy(seq.vel[seq.active_pattern], mdt_menu_backup_vel, 16);
  memcpy(seq.fx_inst[seq.active_pattern], mdt_menu_backup_fx_inst, 16);
  memcpy(seq.fx_cmd[seq.active_pattern], mdt_menu_backup_fx_cmd, 16);
  memcpy(seq.fx_val[seq.active_pattern], mdt_menu_backup_fx_val, 16);
}

static FLASHMEM void _mdt_clear_backup(void)
{
  mdt_menu_has_backup = false;
}

// ===========================================================================
//  MUSIC THEORY TABLES
// ===========================================================================
struct MdtScaleDef { const char* name; uint16_t mask; };
static const MdtScaleDef mdt_scales[] = {
  { "MAJOR",    0xAB5 },
  { "MINOR",    0x5AD },
  { "DORIAN",   0x6AD },
  { "PHRYGIAN", 0x5AB },
  { "LYDIAN",   0xAD5 },
  { "MIXOLYD",  0x6B5 },
  { "LOCRIAN",  0x56B },
  { "HARM MIN", 0x9AD },
  { "MEL MIN",  0xAAD },
  { "PENT MAJ", 0x295 },
  { "PENT MIN", 0x4A9 },
  { "BLUES",    0x4E9 },
};
#define MDT_NUM_SCALES ((uint8_t)(sizeof(mdt_scales) / sizeof(mdt_scales[0])))

struct MdtChordDef { const char* name; uint8_t n; uint8_t iv[5]; uint16_t scale; };
static const MdtChordDef mdt_chords[] = {
  { "MAJ",  3, { 0, 4, 7 },         0xAB5 },
  { "MIN",  3, { 0, 3, 7 },         0x5AD },
  { "7",    4, { 0, 4, 7, 10 },     0x6B5 },
  { "MAJ7", 4, { 0, 4, 7, 11 },     0xAB5 },
  { "MIN7", 4, { 0, 3, 7, 10 },     0x6AD },
  { "MIN9", 5, { 0, 3, 7, 10, 14 }, 0x6AD },
  { "MAJ9", 5, { 0, 4, 7, 11, 14 }, 0xAB5 },
  { "SUS2", 3, { 0, 2, 7 },         0x6A5 },
  { "SUS4", 3, { 0, 5, 7 },         0x6A5 },
  { "DIM",  3, { 0, 3, 6 },         0xB6D },
  { "M7B5", 4, { 0, 3, 6, 10 },     0x56B },
  { "AUG",  3, { 0, 4, 8 },         0x555 },
};
#define MDT_NUM_CHORDS ((uint8_t)(sizeof(mdt_chords) / sizeof(mdt_chords[0])))

enum { MDT_GEN_BASS = 0, MDT_GEN_MELODY, MDT_GEN_ARP, MDT_GEN_STYLES };
static const char* const mdt_gen_style_names[MDT_GEN_STYLES] = { "BASS", "MELODY", "ARP" };

static const uint8_t mdt_metric_w[16] = {
  100, 10, 35, 15,  70, 10, 40, 20,  85, 10, 35, 15,  70, 15, 45, 25
};

static FLASHMEM bool _mdt_pc_in(uint16_t mask, uint8_t root, uint8_t note)
{
  return (mask >> ((note % 12 + 12 - root) % 12)) & 1;
}

static uint32_t mdt_rng_state = 1;
static FLASHMEM uint32_t _mdt_rng(void)
{
  uint32_t x = mdt_rng_state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return mdt_rng_state = x;
}
static FLASHMEM void _mdt_rng_seed(uint32_t s)
{
  s = s * 2654435761u + 0x9E3779B9u;
  mdt_rng_state = s ? s : 1;
  for (uint8_t i = 0; i < 4; i++) _mdt_rng();
}
static FLASHMEM uint8_t _mdt_rnd(uint8_t n) { return n ? (uint8_t)(_mdt_rng() % n) : 0; }
static FLASHMEM bool    _mdt_pct(uint8_t p) { return _mdt_rnd(100) < p; }

static FLASHMEM uint8_t _mdt_build_pool(uint8_t* out, uint8_t max, uint8_t lo, uint8_t hi,
  uint8_t root, uint16_t mask)
{
  uint8_t k = 0;
  for (uint16_t n = lo; n <= hi && k < max; n++)
    if (_mdt_pc_in(mask, root, (uint8_t)n)) out[k++] = (uint8_t)n;
  return k;
}

static FLASHMEM uint8_t _mdt_pool_nearest(const uint8_t* pool, uint8_t np, uint8_t note)
{
  uint8_t best = 0, bd = 255;
  for (uint8_t i = 0; i < np; i++) {
    const uint8_t d = (pool[i] > note) ? pool[i] - note : note - pool[i];
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

static FLASHMEM uint8_t _mdt_pattern_len(void)
{
  const uint8_t len = 16 - seq.pattern_len_dec;
  return (len >= 1 && len <= 16) ? len : 16;
}

// ===========================================================================
//  ACTION: CLEAR COLUMNS
// ===========================================================================
static FLASHMEM void _mdt_action_clear_cols(uint8_t mode)
{
  const uint8_t pat = seq.active_pattern;
  for (uint8_t s = 0; s < 16; s++) {
    if (mode == 0) {
      seq.fx_inst[pat][s] = 0xFF;
      seq.fx_cmd[pat][s] = MDT_FX_NONE;
      seq.fx_val[pat][s] = 0;
    }
    else {
      seq.note_data[pat][s] = 0;
      seq.vel[pat][s] = 0;
    }
  }
}

// ===========================================================================
//  ACTION: SCALE QUANTIZE
// ===========================================================================
static FLASHMEM bool _mdt_quantize_allowed(void)
{
  const uint8_t ct = seq.content_type[seq.active_pattern];
  return ct == 1 || ct == 2;
}

static FLASHMEM void _mdt_action_quantize(uint8_t root, uint8_t scale)
{
  if (scale >= MDT_NUM_SCALES) return;
  const uint16_t mask = mdt_scales[scale].mask;
  const uint8_t pat = seq.active_pattern;
  for (uint8_t s = 0; s < 16; s++) {
    const uint8_t n = seq.note_data[pat][s];
    if (n == 0 || n > 127) continue;
    for (uint8_t d = 0; d <= 6; d++) {
      if (n > d && _mdt_pc_in(mask, root, n - d)) { seq.note_data[pat][s] = n - d; break; }
      if (n + d <= 127 && _mdt_pc_in(mask, root, n + d)) { seq.note_data[pat][s] = n + d; break; }
    }
  }
}

// ===========================================================================
//  ACTION: GENERATOR
// ===========================================================================
static FLASHMEM bool _mdt_generate_allowed(void)
{
  return seq.content_type[seq.active_pattern] == 1;
}

static FLASHMEM void _mdt_gen_rhythm(bool* on, uint8_t len, uint8_t style, uint8_t dens)
{
  memset(on, 0, 16);
  if (dens < 1) dens = 1;
  if (dens > 5) dens = 5;

  if (style == MDT_GEN_ARP) {
    const uint8_t stepw = (dens == 1) ? 4 : (dens <= 3) ? 2 : 1;
    for (uint8_t s = 0; s < len; s++) {
      if (s % stepw == 0)                    on[s] = !(dens == 4 && (s & 1) && _mdt_pct(15));
      else if (dens == 3 && _mdt_pct(30))    on[s] = true;
    }
    on[0] = true;
    return;
  }

  static const uint8_t target16[2][5] = {
    { 2, 4, 6, 9, 12 },
    { 3, 5, 7, 9, 12 },
  };
  const uint8_t half = (len >= 8) ? len / 2 : len;
  uint8_t want = (uint8_t)((target16[style == MDT_GEN_MELODY][dens - 1] * half + 8) / 16);
  if (want < 1)    want = 1;
  if (want > half) want = half;

  uint8_t score[16];
  for (uint8_t s = 0; s < half; s++) {
    uint16_t sc = mdt_metric_w[s] + _mdt_rnd(45);
    if (style == MDT_GEN_MELODY && (s & 1)) sc += 10;
    score[s] = (sc > 254) ? 254 : (uint8_t)sc;
  }
  if (style == MDT_GEN_BASS) score[0] = 255;
  for (uint8_t k = 0; k < want; k++) {
    int8_t best = -1;
    for (uint8_t s = 0; s < half; s++)
      if (!on[s] && (best < 0 || score[s] > score[best])) best = s;
    if (best >= 0) on[best] = true;
  }

  for (uint8_t s = 4; s < half; s += 4)
    if (on[s] && !on[s - 1] && _mdt_pct(style == MDT_GEN_MELODY ? 25 : 15)) {
      on[s] = false; on[s - 1] = true;
    }

  if (half < len) {
    for (uint8_t s = half; s < len; s++) on[s] = on[s - half];
    const uint8_t nvar = 1 + _mdt_rnd(2);
    for (uint8_t v = 0; v < nvar; v++) {
      const uint8_t t = half + _mdt_rnd(len - half);
      if (!on[t] || (style == MDT_GEN_BASS && t == half)) continue;
      const int8_t  dir = _mdt_pct(50) ? 1 : -1;
      const int16_t u = (int16_t)t + dir;
      if (u > (int16_t)half && u < (int16_t)len && !on[u]) { on[t] = false; on[u] = true; }
    }

    if (len >= 12) {
      uint8_t c = 0;
      for (uint8_t s = len - 3; s < len; s++) { c += on[s]; on[s] = false; }
      if (dens >= 4 && c < 3) c++;
      for (uint8_t k = 0; k < c; k++) {
        int8_t best = -1; uint16_t bsc = 0;
        for (uint8_t s = len - 3; s < len; s++) {
          if (on[s]) continue;
          const uint16_t sc = mdt_metric_w[s] + _mdt_rnd(45);
          if (best < 0 || sc > bsc) { best = s; bsc = sc; }
        }
        if (best >= 0) on[best] = true;
      }
    }
    if (style == MDT_GEN_BASS && (half % 4) == 0) on[half] = true;
  }

  uint8_t cnt = 0;
  for (uint8_t s = 0; s < len; s++) cnt += on[s];
  if (cnt < 2 && len > 1) { on[0] = true; on[(half < len) ? half : len - 1] = true; }
}

static FLASHMEM void _mdt_gen_bass(const bool* on, uint8_t len, uint8_t* notes,
  uint8_t base, const MdtChordDef& cd,
  const uint8_t* sp, uint8_t np)
{
  const uint8_t fifth = base + cd.iv[(cd.n > 2) ? 2 : cd.n - 1];
  const uint8_t third = base + cd.iv[1];
  const uint8_t seventh = base + ((cd.n > 3) ? cd.iv[3] : 12);
  const uint8_t octave = base + 12;
  const uint8_t half = (len >= 8) ? len / 2 : len;

  for (uint8_t s = 0; s < len; s += 4) {
    if (!on[s]) continue;
    if (s == 0)            notes[s] = base;
    else if (s == half)    notes[s] = _mdt_pct(70) ? base : fifth;
    else {
      const uint8_t r = _mdt_rnd(100);
      notes[s] = (r < 50) ? base : (r < 78) ? fifth : (r < 90) ? octave : third;
    }
  }

  uint8_t prev = base;
  for (uint8_t s = 0; s < len; s++) {
    if (!on[s]) continue;
    if ((s % 4) == 0 && notes[s]) { prev = notes[s]; continue; }

    uint8_t next = 0, tn = 0;
    for (uint8_t t = s + 1; t < len; t++)
      if (on[t] && (t % 4) == 0 && notes[t]) { next = notes[t]; tn = t; break; }

    uint8_t n;
    if (next && tn <= s + 2 && _mdt_pct(45)) {
      if (_mdt_pct(55))       n = next - 1;
      else if (_mdt_pct(50))  n = next + 1;
      else {
        const uint8_t i = _mdt_pool_nearest(sp, np, next);
        n = (i > 0) ? sp[i - 1] : next - 2;
      }
    }
    else if (prev == base && _mdt_pct(30))   n = octave;
    else if (prev == octave && _mdt_pct(40)) n = base;
    else {
      const uint8_t r = _mdt_rnd(100);
      if (r < 35) n = base;
      else if (r < 60) n = fifth;
      else if (r < 75) n = third;
      else if (r < 87) n = seventh;
      else {
        const uint8_t i = _mdt_pool_nearest(sp, np, prev);
        n = (_mdt_pct(50) && i + 1 < np) ? sp[i + 1] : (i > 0 ? sp[i - 1] : sp[i]);
      }
    }
    if (n < 1) n = 1;
    if (n > 127) n = 127;
    notes[s] = n;
    prev = n;
  }
}

static FLASHMEM void _mdt_gen_melody(const bool* on, uint8_t len, uint8_t* notes,
  uint8_t root, const uint8_t* sp, uint8_t np,
  const uint8_t* cp, uint8_t nc)
{
  const uint8_t half = (len >= 8) ? len / 2 : len;

  int8_t first = -1, last = -1, last_call = -1;
  for (uint8_t s = 0; s < len; s++) if (on[s]) {
    if (first < 0) first = s;
    last = s;
    if (s < half) last_call = s;
  }
  if (first < 0) return;

  bool skel[16] = { false };
  for (uint8_t s = 0; s < len; s++)
    skel[s] = on[s] && ((s % 4) == 0 || s == first || s == last || s == last_call);

  uint8_t si = nc / 3 + _mdt_rnd(nc / 3 + 1);
  if (si >= nc) si = nc - 1;
  uint8_t prev = cp[si];
  for (uint8_t s = 0; s < len; s++) {
    if (!skel[s]) continue;
    uint8_t pick;
    if (s == last) {
      uint8_t best = cp[0], bd = 255;
      for (uint8_t i = 0; i < nc; i++) {
        const uint8_t iv = (cp[i] % 12 + 12 - root) % 12;
        if (iv != 0 && iv != 3 && iv != 4) continue;
        const uint8_t d = (cp[i] > prev) ? cp[i] - prev : prev - cp[i];
        if (d < bd) { bd = d; best = cp[i]; }
      }
      pick = best;
    }
    else {
      const uint8_t i0 = _mdt_pool_nearest(cp, nc, prev);
      const uint8_t r = _mdt_rnd(100);
      int8_t idx;
      if (r < 30)      idx = i0;
      else if (r < 60) idx = i0 + 1;
      else if (r < 88) idx = i0 - 1;
      else             idx = _mdt_rnd(nc);
      if (idx < 0)   idx = 1;
      if (idx >= nc) idx = nc - 2;
      if (idx < 0)   idx = 0;
      pick = cp[idx];
      if (s == last_call && (pick % 12) == root % 12 && nc > 1)
        pick = cp[(idx + 1 < nc) ? idx + 1 : idx - 1];
    }
    notes[s] = pick;
    prev = pick;
  }

  uint8_t last_note = notes[first];
  for (uint8_t s = 0; s < len; s++) {
    if (!on[s]) continue;
    if (skel[s]) { last_note = notes[s]; continue; }

    uint8_t next = 0;
    for (uint8_t t = s + 1; t < len; t++) if (skel[t]) { next = notes[t]; break; }

    const int16_t pidx = _mdt_pool_nearest(sp, np, last_note);
    int16_t idx;
    if (!next) {
      idx = pidx + (_mdt_pct(50) ? 1 : -1);
    }
    else {
      const int16_t ni = _mdt_pool_nearest(sp, np, next);
      const int16_t d = ni - pidx;
      if (d >= 2 || d <= -2)  idx = pidx + (d > 0 ? 1 : -1);
      else if (d == 0)        idx = _mdt_pct(70) ? pidx + (_mdt_pct(50) ? 1 : -1) : pidx;
      else                    idx = _mdt_pct(50) ? ni : pidx - d;
    }
    if (idx < 0)   idx = 1;
    if (idx >= np) idx = np - 2;
    if (idx < 0)   idx = 0;
    notes[s] = sp[idx];
    last_note = notes[s];
  }
}

static FLASHMEM void _mdt_gen_arp(const bool* on, uint8_t len, uint8_t* notes,
  uint8_t hi, const uint8_t* cp, uint8_t nc)
{
  static const uint8_t broken[4] = { 0, 2, 1, 3 };
  const uint8_t shape = _mdt_rnd(5);
  const uint8_t half = (len >= 8) ? len / 2 : len;
  const bool    lift = _mdt_pct(40);
  uint8_t j = 0;
  for (uint8_t s = 0; s < len; s++) {
    if (!on[s]) continue;
    uint8_t i;
    switch (shape) {
    case 0:  i = j % nc; break;
    case 1:  i = nc - 1 - (j % nc); break;
    case 2: { const uint8_t per = (nc > 1) ? 2 * nc - 2 : 1;
      const uint8_t p = j % per; i = (p < nc) ? p : per - p; break; }
    case 3:  i = (j & 1) ? 1 + ((j / 2) % (nc > 1 ? nc - 1 : 1)) : 0; break;
    default: i = ((j / 4) + broken[j % 4]) % nc; break;
    }
    if (i >= nc) i = nc - 1;
    uint8_t n = cp[i];
    if (lift && s >= half && n + 12 <= hi) n += 12;
    notes[s] = n;
    j++;
  }
}

static FLASHMEM void _mdt_action_generate(uint8_t root, uint8_t chord, uint8_t oct,
  uint8_t style, uint8_t dens, uint8_t seed)
{
  const uint8_t pat = seq.active_pattern;
  const uint8_t len = _mdt_pattern_len();
  const MdtChordDef& cd = mdt_chords[(chord < MDT_NUM_CHORDS) ? chord : 0];

  uint16_t cmask = 0;
  for (uint8_t i = 0; i < cd.n; i++) cmask |= 1u << (cd.iv[i] % 12);
  const uint16_t smask = cd.scale | cmask;

  const uint8_t base = (oct + 1) * 12 + root;
  uint8_t hi = base + ((style == MDT_GEN_BASS) ? 14 : (style == MDT_GEN_MELODY) ? 19 : 24);
  if (hi > 127) hi = 127;

  uint8_t sp[40], cp[24];
  const uint8_t np = _mdt_build_pool(sp, sizeof(sp), base, hi, root, smask);
  const uint8_t nc = _mdt_build_pool(cp, sizeof(cp), base, hi, root, cmask);
  if (np < 2 || nc < 2) return;

  bool on[16];
  _mdt_rng_seed((uint32_t)seed * 7919u + style * 131u + dens * 17u);
  _mdt_gen_rhythm(on, len, style, dens);

  bool ctrl[16];
  for (uint8_t s = 0; s < 16; s++) {
    ctrl[s] = mdt_step_is_control(mdt_menu_backup_notes[s], seq.fx_cmd[pat][s]);
    if (ctrl[s]) on[s] = false;
  }

  uint8_t notes[16] = { 0 };
  _mdt_rng_seed((uint32_t)seed * 104729u + style);
  if (style == MDT_GEN_BASS)        _mdt_gen_bass(on, len, notes, base, cd, sp, np);
  else if (style == MDT_GEN_MELODY) _mdt_gen_melody(on, len, notes, root, sp, np, cp, nc);
  else                              _mdt_gen_arp(on, len, notes, hi, cp, nc);

  const uint8_t half = (len >= 8) ? len / 2 : len;
  if (style != MDT_GEN_ARP && half < len && _mdt_pct(60))
    for (uint8_t s = half + 1; s + 4 < len; s++)
      if (on[s] && on[s - half] && notes[s - half]) notes[s] = notes[s - half];

  bool    can_tie = false;
  uint8_t tie_p = 0;
  for (uint8_t s = 0; s < len; s++) {
    if (ctrl[s]) { can_tie = false; continue; }
    if (on[s] && notes[s]) {
      const uint8_t w = mdt_metric_w[s];
      int16_t v;
      if (style == MDT_GEN_BASS)        v = 72 + w * 45 / 100 - ((s & 1) ? 12 : 0);
      else if (style == MDT_GEN_MELODY) v = 78 + w * 35 / 100;
      else                              v = 70 + w * 40 / 100;
      v += (int16_t)_mdt_rnd(11) - 5;
      if (v < 20)  v = 20;
      if (v > 127) v = 127;
      seq.note_data[pat][s] = notes[s];
      seq.vel[pat][s] = (uint8_t)v;
      can_tie = true;
      static const uint8_t tie_mel[5] = { 55, 40, 28, 15, 5 };
      static const uint8_t tie_bass[5] = { 60, 35, 10, 0, 0 };
      tie_p = (style == MDT_GEN_MELODY) ? tie_mel[dens - 1] + ((w >= 70) ? 10 : 0)
        : (style == MDT_GEN_BASS) ? tie_bass[dens - 1] : 0;
    }
    else if (can_tie && _mdt_pct(tie_p)) {
      seq.note_data[pat][s] = 130;
      seq.vel[pat][s] = 0;
    }
    else {
      seq.note_data[pat][s] = 0;
      seq.vel[pat][s] = 0;
      can_tie = false;
    }
  }
}

static FLASHMEM void _mdt_cycle_pattern_type(void)
{
  seq.content_type[seq.active_pattern] =
    (uint8_t)((seq.content_type[seq.active_pattern] + 1) & 0x03);

  virtual_keyboard_smart_preselect_mode();

  _mdt_preview_invalidate();
  _mdt_status_cache_reset();

  if (mdt_is_vk_mode()) {
    _mdt_draw_header();
    _mdt_draw_all_rows(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
    _mdt_draw_vk_latch_button();
  }
  else {
    checkWebRemote();
    display.fillRect(MDT_PREV_X, MDT_PREV_CAP_Y,
      DISPLAY_WIDTH - MDT_PREV_X, MDT_STATUS_Y - MDT_PREV_CAP_Y,
      COLOR_BACKGROUND);
    display.console = false;
    _mdt_draw_col_captions();
    _mdt_light_redraw_body();
    _mdt_draw_type_button();
    _mdt_draw_pattern_preview(mdt_cursor_row);
  }
  mdt_full_redraw = false;
  mdt_light_redraw = false;
  mdt_preview_dirty = false;
  _mdt_update_oled_help();
}

// ===========================================================================
//  STEP RECORDING
// ===========================================================================
static FLASHMEM void _mdt_set_step_recording(bool on)
{
  if (on && seq.running) return;
  mdt_step_recording = on;
  seq.step_recording = on;
  seq.note_in = 0;
  seq.note_in_velocity = 0;

  _mdt_draw_header();
  if (mdt_menu_state == MDT_MENU_OFF) _mdt_draw_touch_buttons();
  _mdt_update_oled_help();
}

static FLASHMEM bool _mdt_step_advance_cursor(void)
{
  if (seq.auto_advance_step == 0) return false;

  if (mdt_cursor_row < MDT_ROWS - 1) {
    mdt_cursor_row++;
  }
  else if (seq.auto_advance_step == 1) {
    mdt_cursor_row = 0;
  }
  else {
    return true;
  }
  mdt_ensure_cursor_visible();
  return false;
}

static FLASHMEM void _mdt_step_rec_store(uint8_t kind)
{
  if (!mdt_step_recording || seq.running || seq.recording) return;

  const uint8_t pat = seq.active_pattern;
  const uint8_t row = mdt_cursor_row;
  const uint8_t ct = seq.content_type[pat];
  if (row >= MDT_ROWS) return;

  if (kind == 1) {
    seq.note_data[pat][row] = 0;
    seq.vel[pat][row] = 0;
  }
  else if (kind == 2) {
    if (ct == 0) return;
    seq.note_data[pat][row] = 130;
    seq.vel[pat][row] = 0;
  }
  else {
    const uint8_t note = seq.note_in;
    const uint8_t velo = _mdt_vk_velocity_or(seq.note_in_velocity);
    seq.note_data[pat][row] = note;

    if (ct == 0 && ts.virtual_keyboard_instrument != VK_DRUMS &&
      get_sample_note(activeSample) > 209) {
      seq.vel[pat][row] = get_sample_note(activeSample);
      mdt_pitch_buffer = note;
      mdt_drum_preview_valid = true;
    }
    else if (ct == 2) {
      if (seq.vel[pat][row] < 200) seq.vel[pat][row] = 200;
      mdt_buf_note = note;
      mdt_buf_vel = seq.vel[pat][row];
      mdt_note_preview_valid = true;
    }
    else {
      seq.vel[pat][row] = velo;
      if (ct == 0) {
        const uint8_t idx = _mdt_drum_index_for_step(note, velo);
        if (idx < MDT_EMPTY_SLOT_IDX) { activeSample = idx; mdt_drum_preview_valid = true; }
        if (velo <= 127) mdt_buf_drum_vel = velo;
      }
      else {
        mdt_buf_note = note; mdt_buf_vel = velo; mdt_note_preview_valid = true;
      }
    }
  }

  seq.note_in = 0;
  seq.note_in_velocity = 0;

  const bool stop = _mdt_step_advance_cursor();

  if (mdt_view_changed) {
    _mdt_draw_header();
    _mdt_draw_all_rows(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
    mdt_view_changed = false;
  }
  else {
    _mdt_draw_row(row, mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
    if (mdt_cursor_row != row)
      _mdt_draw_row(mdt_cursor_row, mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  }
  mdt_prev_cursor_row = mdt_cursor_row;
  mdt_prev_cursor_col = mdt_cursor_col;
  mdt_prev_edit_state = mdt_edit_state;
  mdt_cursor_moved = false;

  _mdt_status_cache_reset();
  _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  _mdt_preview_invalidate();
  _mdt_draw_pattern_preview(mdt_cursor_row);
  mdt_preview_dirty = false;

  if (stop) _mdt_set_step_recording(false);
}

static bool _mdt_handle_step_recording(void)
{
  if (!mdt_step_recording) return false;
  if (seq.running || seq.recording) return false;
  if (seq.note_in == 0 || seq.note_in_velocity == 0) return false;
  _mdt_step_rec_store(0);
  return true;
}

static FLASHMEM void _mdt_step_recording_insert_latch(void) { _mdt_step_rec_store(2); }
static FLASHMEM void _mdt_step_recording_insert_rest(void) { _mdt_step_rec_store(1); }

FLASHMEM bool mdtracker_step_recording_active(void)
{
  return mdt_step_recording && LCDML.FUNC_getID() ==
    LCDML.OTHER_getIDFromFunction(UI_func_mdtracker_pattern_editor);
}

FLASHMEM bool mdtracker_get_ring_feedback(int16_t* value,
  int16_t* minVal,
  int16_t* maxVal)
{
  if (!mdt_edit_state || mdt_menu_state != MDT_MENU_OFF) return false;
  if (mdt_cursor_col == MDT_COL_PATTERN || mdt_cursor_row >= MDT_ROWS) return false;

  const uint8_t pat = seq.active_pattern, row = mdt_cursor_row;
  const uint8_t ct = seq.content_type[pat];

  static const uint8_t colMax[MDT_COL_COUNT] = {
    127, 127, 72, MDT_FX_END - 1, 255, 0
  };

  const uint8_t* cell[MDT_COL_COUNT] = {
    &seq.note_data[pat][row], &seq.vel[pat][row], &seq.fx_inst[pat][row],
    &seq.fx_cmd[pat][row],    &seq.fx_val[pat][row], nullptr
  };
  const uint8_t* p = cell[mdt_cursor_col];
  if (!p) return false;

  *minVal = 0;
  *maxVal = colMax[mdt_cursor_col];
  *value = *p;

  if (mdt_cursor_col == MDT_COL_NOTE && ct == 0) {
    *value = activeSample;   *maxVal = MDT_MAX_SAMPLE_IDX;
  }
  else if (mdt_cursor_col == MDT_COL_VEL && (ct == 2 || ct == 3) && *value > 199) {
    *value -= 200;           *maxVal = 5;
  }
  else if (mdt_cursor_col == MDT_COL_PAR) {
    *maxVal = mdt_fx_param_max[seq.fx_cmd[pat][row]];
    if (*value > *maxVal) *value = *maxVal;
  }
  else if (mdt_cursor_col == MDT_COL_CMD) {
    // Ring shows the position inside the commands allowed for this pattern type
    const uint8_t cnt = mdt_cmd_allowed_count(ct);
    *maxVal = (cnt > 0) ? (cnt - 1) : 0;
    *value = mdt_cmd_allowed_index(ct, seq.fx_cmd[pat][row]);
    if (*value > *maxVal) *value = *maxVal;
  }
  return true;
}

// ===========================================================================
//  VIRTUAL KEYBOARD OPEN / CLOSE
// ===========================================================================
static FLASHMEM void _mdt_vk_redraw_top(void)
{
  checkWebRemote();
  display.fillRect(MDT_PREV_X, 0, DISPLAY_WIDTH - MDT_PREV_X, MDT_VK_TOP,
    COLOR_BACKGROUND);
  display.console = false;

  _mdt_draw_header();
  _mdt_draw_col_captions();
  _mdt_draw_all_rows(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  _mdt_draw_vk_step_buttons();
  _mdt_draw_vk_velocity_bar();

  mdt_view_changed = false;
  mdt_prev_cursor_row = mdt_cursor_row;
  mdt_prev_cursor_col = mdt_cursor_col;
  mdt_prev_edit_state = mdt_edit_state;
  mdt_cursor_moved = false;
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_open_virtual_keyboard(void)
{
  seq.cycle_touch_element = 1;
  virtual_keyboard_smart_preselect_mode();
  mdt_ensure_cursor_visible();

  checkWebRemote();
  display.fillRect(0, MDT_DATA_Y, DISPLAY_WIDTH, DISPLAY_HEIGHT - MDT_DATA_Y,
    COLOR_BACKGROUND);
  display.console = false;

  _mdt_vk_redraw_top();
  drawVirtualKeyboard();

  _mdt_status_cache_reset();
  mdt_full_redraw = false;
  mdt_light_redraw = false;
  mdt_preview_dirty = false;
}

static FLASHMEM void _mdt_close_virtual_keyboard(void)
{
  seq.cycle_touch_element = 0;
  mdt_view_offset = 0;
  mdt_view_changed = false;

  checkWebRemote();
  display.fillRect(0, MDT_DATA_Y, DISPLAY_WIDTH,
    DISPLAY_HEIGHT - MDT_DATA_Y, COLOR_BACKGROUND);
  display.console = false;

  _mdt_preview_invalidate();
  _mdt_status_cache_reset();

  _mdt_full_redraw_body();
  _mdt_draw_pattern_preview(mdt_cursor_row);
  mdt_full_redraw = false;
  mdt_light_redraw = false;
  mdt_preview_dirty = false;

  _mdt_update_oled_help();
}

// ===========================================================================
//  MENU SYSTEM
// ===========================================================================
static const char* const _mdt_menu_items[MDT_MI_COUNT] = {
  "CLOSE",
  "STEP REC",
  "MODE",
  "CLEAR PAT >",
  "CLEAR ALL >",
  "COPY PAT >",
  "SWAP PAT <>",
  "FILL PAT >",
  "TRANSPOSE >",
  "CLEAR COL >",
  "QUANTIZE >",
  "GENERATE >"
};

static FLASHMEM void _mdt_open_menu(void)
{
  if (mdt_is_vk_mode()) _mdt_close_virtual_keyboard();

  mdt_menu_state = MDT_MENU_MAIN;
  mdt_menu_highlight = 0;
  mdt_menu_value = 0;
  mdt_menu_value2 = 0;
  _mdt_clear_backup();

  _mdt_menu_full_redraw();

  mdt_full_redraw = false;
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_close_menu(void)
{
  _mdt_clear_backup();
  mdt_menu_state = MDT_MENU_OFF;
  mdt_full_redraw = true;
  mdt_light_redraw = false;
  mdt_preview_dirty = true;
  _mdt_status_cache_reset();
  _mdt_preview_invalidate();
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_menu_draw_row(uint8_t i, bool selected)
{
  if (i >= MDT_MI_COUNT) return;

  const uint16_t y = MDT_MENU_ROWS_Y0 + i * MDT_MENU_ROW_H;
  const uint16_t bg = selected ? DX_DARKCYAN : COLOR_BACKGROUND;

  checkWebRemote();
  display.fillRect(MDT_MENU_X0 + 3, y - 1,
    MDT_MENU_W - 6, MDT_MENU_ROW_H - 1, bg);

  display.setTextSize(1);
  display.setTextColor(selected ? COLOR_SYSTEXT : GREY1, bg);
  display.setCursor(MDT_MENU_X0 + 5, y);
  display.print(_mdt_menu_items[i]);

  display.setCursor(MDT_MENU_X0 + MDT_MENU_W - 28, y);
  switch (i) {
  case MDT_MI_STEP_REC:
    if (seq.running) display.print("--");
    else             display.print(mdt_step_recording ? "ON" : "OF");
    break;
  case MDT_MI_PLAY_MODE:
    display.print(seq.play_mode ? "P" : "S");
    break;
  case MDT_MI_QUANTIZE:
    if (!_mdt_quantize_allowed()) display.print("--");
    break;
  case MDT_MI_GENERATE:
    if (!_mdt_generate_allowed()) display.print("--");
    break;
  default: break;
  }
}

static FLASHMEM void _mdt_menu_pattern_preview(uint16_t x, uint16_t y,
  uint8_t pat, const char* label)
{
  display.setTextSize(1);
  display.setTextColor(GREY1, COLOR_BACKGROUND);
  display.setCursor(x, y);
  display.print(label);

  const uint8_t ct = seq.content_type[pat];
  const bool is_chordy = (ct == 2 || ct == 3);

  for (uint8_t r = 0; r < 16; r++) {
    char c = '-';
    const uint8_t n = seq.note_data[pat][r];
    const uint8_t v = seq.vel[pat][r];
    const uint8_t fx = seq.fx_cmd[pat][r];
    uint16_t col = GREY3;

    if (n == 130) { c = 'L'; col = GREEN; }
    else if (n > 0) {
      c = noteNames[n % 12][0];
      if (ct == 0 && v > 209)         col = COLOR_PITCHSMP;
      else if (is_chordy && v > 199)  col = _mdt_chord_color((uint8_t)(v - 200));
      else                            col = _mdt_shade_by_velocity(_mdt_type_color(), v);
    }
    else if (fx != MDT_FX_NONE) {
      // FX-only control step - same color as the ">FX" marker in the grid
      c = 'x';
      col = COLOR_ARP;
    }
    display.setTextColor(col, COLOR_BACKGROUND);
    display.setCursor(x + r * 6, y + 10);
    display.print(c);
  }
}

static FLASHMEM void _mdt_menu_draw_value_line(void)
{
  checkWebRemote();
  display.fillRect(MDT_MENU_X0 + 3, MDT_MENU_VALUE_Y - 4,
    MDT_MENU_W - 6, MDT_MENU_VALUE_H, COLOR_BACKGROUND);

  display.setTextSize(1);
  display.setTextColor(COLOR_PITCHSMP, COLOR_BACKGROUND);
  display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y);

  switch (mdt_menu_state) {
  case MDT_MENU_COPY:
  case MDT_MENU_SWAP:
    display.printf("DEST: %02d", (int)mdt_menu_value);
    _mdt_menu_pattern_preview(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 16,
      seq.active_pattern, "SRC:");
    _mdt_menu_pattern_preview(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 44,
      (uint8_t)mdt_menu_value, "DST:");
    break;

  case MDT_MENU_FILL_SAMPLE:
    if (seq.content_type[seq.active_pattern] == 0 ||
      seq.content_type[seq.active_pattern] == 3) {
      display.printf("SAMPLE %02d", (int)mdt_menu_value);
      if (mdt_menu_value >= 0 && mdt_menu_value < NUM_DRUMSET_CONFIG) {
        display.setTextColor(GREY1, COLOR_BACKGROUND);
        display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 14);
        char tmp[20];
        snprintf(tmp, sizeof(tmp), "%s", basename(drum_config[mdt_menu_value].name));
        display.print(tmp);
      }
    }
    else {
      display.printf("NOTE: %d", (int)mdt_menu_value);
    }
    _mdt_menu_pattern_preview(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 44,
      seq.active_pattern, "PRV:");
    break;

  case MDT_MENU_FILL_DIV:
    if (mdt_menu_value == 0) display.print(F("STEP: 1/4"));
    else if (mdt_menu_value == 1) display.print(F("STEP: 1/8"));
    else                          display.print(F("STEP: 1/16"));
    _mdt_menu_pattern_preview(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 44,
      seq.active_pattern, "PRV:");
    break;

  case MDT_MENU_CLEAR_COLS:
    if (mdt_menu_value == 0) display.print(F("CLEAR: FX ONLY"));
    else                     display.print(F("CLEAR: NOTES ONLY"));
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 14);
    if (mdt_menu_value == 0) display.print(F("NOTES ARE KEPT"));
    else                     display.print(F("FX ARE KEPT"));
    _mdt_menu_pattern_preview(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 44,
      seq.active_pattern, "PRV:");
    break;

  case MDT_MENU_QUANTIZE:
  case MDT_MENU_GENERATE:
    _mdt_form_draw_all();
    break;

  case MDT_MENU_TRANSPOSE:
    display.printf("%+d SEMITONES", (int)mdt_menu_value);
    _mdt_menu_pattern_preview(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 44,
      seq.active_pattern, "PRV:");
    break;

  case MDT_MENU_CLEAR_PAT_CONFIRM:
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.printf("CLEAR PATTERN %02d?", seq.active_pattern);
    display.setTextColor(COLOR_PITCHSMP, COLOR_BACKGROUND);
    _mdt_menu_pattern_preview(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 20,
      seq.active_pattern, "WILL:");
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 60);
    display.print(F("L:CANCEL R:OK"));
    break;

  case MDT_MENU_CLEAR_ALL_CONFIRM: {
    uint8_t used = 0;
    for (uint8_t p = 0; p < NUM_SEQ_PATTERN; p++) {
      bool has = false;
      for (uint8_t s = 0; s < 16 && !has; s++)
        if (seq.note_data[p][s] != 0 || seq.fx_cmd[p][s] != MDT_FX_NONE) has = true;
      if (has) used++;
    }
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.print(F("REALLY CLEAR ALL?"));
    display.setTextColor(COLOR_PITCHSMP, COLOR_BACKGROUND);
    display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 20);
    display.printf("%d / %d HAVE DATA", used, NUM_SEQ_PATTERN);
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_VALUE_Y + 60);
    display.print(F("L:CANCEL R:OK"));
    break;
  }

  default: break;
  }
}

static FLASHMEM void _mdt_menu_full_redraw(void)
{
  if (mdt_menu_state == MDT_MENU_OFF) return;

  checkWebRemote();
  display.fillRect(MDT_MENU_X0, MDT_MENU_Y0, MDT_MENU_W, MDT_MENU_H, COLOR_BACKGROUND);
  display.drawRect(MDT_MENU_X0, MDT_MENU_Y0, MDT_MENU_W - 1, MDT_MENU_H - 1, COLOR_SYSTEXT);
  display.drawRect(MDT_MENU_X0 + 1, MDT_MENU_Y0 + 1, MDT_MENU_W - 3, MDT_MENU_H - 3, GREY3);

  display.setTextSize(1);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_Y0 + 3);

  if (mdt_menu_state == MDT_MENU_MAIN) {
    display.print(F("MENU P:"));
    display.print(seq.active_pattern);
    display.print(F(" T:"));
    display.print(seq.selected_track + 1);
    for (uint8_t i = 0; i < MDT_MI_COUNT; i++)
      _mdt_menu_draw_row(i, i == mdt_menu_highlight);
  }
  else {
    switch (mdt_menu_state) {
    case MDT_MENU_COPY:              display.print(F("COPY PATTERN"));       break;
    case MDT_MENU_SWAP:              display.print(F("SWAP PATTERN"));       break;
    case MDT_MENU_FILL_SAMPLE:       display.print(F("FILL PATTERN"));       break;
    case MDT_MENU_FILL_DIV:          display.print(F("FILL STEP SIZE"));     break;
    case MDT_MENU_TRANSPOSE:         display.print(F("TRANSPOSE"));          break;
    case MDT_MENU_CLEAR_PAT_CONFIRM: display.print(F("CLEAR PATTERN"));      break;
    case MDT_MENU_CLEAR_ALL_CONFIRM: display.print(F("CLEAR ALL"));          break;
    case MDT_MENU_CLEAR_COLS:        display.print(F("CLEAR COLUMNS"));      break;
    case MDT_MENU_QUANTIZE:          display.print(F("SCALE QUANTIZE"));     break;
    case MDT_MENU_GENERATE:          display.print(F("GENERATOR"));          break;
    default: break;
    }
    _mdt_menu_draw_value_line();
  }

  if (mdt_menu_state == MDT_MENU_MAIN) {
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_HINT1_Y);
    display.print(F("R:TURN  P/TAP:OK"));
    display.setCursor(MDT_MENU_X0 + 5, MDT_MENU_HINT2_Y);
    display.print(F("TAP OUTSIDE:BACK"));
  }
  else {
    const bool confirm_only = (mdt_menu_state == MDT_MENU_CLEAR_PAT_CONFIRM ||
      mdt_menu_state == MDT_MENU_CLEAR_ALL_CONFIRM);
    if (!confirm_only) {
      TouchButton::drawButton(MDT_MENU_TB_X1, MDT_MENU_TB_ADJ_Y, "VALUE", "-",
        TouchButton::BUTTON_NORMAL);
      TouchButton::drawButton(MDT_MENU_TB_X2, MDT_MENU_TB_ADJ_Y, "VALUE", "+",
        TouchButton::BUTTON_NORMAL);
    }
    TouchButton::drawButton(MDT_MENU_TB_X2, MDT_MENU_TB_OK_Y, "CONFIRM", "OK",
      confirm_only ? TouchButton::BUTTON_RED
      : TouchButton::BUTTON_ACTIVE);
    TouchButton::drawButton(MDT_MENU_TB_X1, MDT_MENU_TB_OK_Y, "GO", "BACK",
      TouchButton::BUTTON_NORMAL);
  }

  display.console = false;
}

// ===========================================================================
//  FORM PAGES
// ===========================================================================
#define MDT_FORM_ROW_H 10

enum { MDT_QZ_ROOT = 0, MDT_QZ_SCALE, MDT_QZ_COUNT };
enum {
  MDT_GF_ROOT = 0, MDT_GF_CHORD, MDT_GF_OCT, MDT_GF_STYLE,
  MDT_GF_DENS, MDT_GF_SEED, MDT_GF_COUNT
};

static uint8_t mdt_qz_val[MDT_QZ_COUNT] = { 0, 0 };
static uint8_t mdt_gen_val[MDT_GF_COUNT] = { 0, 1, 2, MDT_GEN_BASS, 3, 1 };

static FLASHMEM bool _mdt_is_form_state(void)
{
  return mdt_menu_state == MDT_MENU_QUANTIZE || mdt_menu_state == MDT_MENU_GENERATE;
}
static FLASHMEM uint8_t _mdt_form_nfields(void)
{
  return (mdt_menu_state == MDT_MENU_QUANTIZE) ? (uint8_t)MDT_QZ_COUNT : (uint8_t)MDT_GF_COUNT;
}
static FLASHMEM uint8_t* _mdt_form_vals(void)
{
  return (mdt_menu_state == MDT_MENU_QUANTIZE) ? mdt_qz_val : mdt_gen_val;
}
static FLASHMEM void _mdt_form_range(uint8_t f, uint8_t* lo, uint8_t* hi)
{
  *lo = 0;
  if (mdt_menu_state == MDT_MENU_QUANTIZE) {
    *hi = (f == MDT_QZ_ROOT) ? 11 : MDT_NUM_SCALES - 1;
    return;
  }
  switch (f) {
  case MDT_GF_ROOT:  *hi = 11; break;
  case MDT_GF_CHORD: *hi = MDT_NUM_CHORDS - 1; break;
  case MDT_GF_OCT:   *lo = 1; *hi = 6; break;
  case MDT_GF_STYLE: *hi = MDT_GEN_STYLES - 1; break;
  case MDT_GF_DENS:  *lo = 1; *hi = 5; break;
  default:           *hi = 255; break;
  }
}

static FLASHMEM void _mdt_form_draw_row(uint8_t i)
{
  const uint8_t  nf = _mdt_form_nfields();
  const uint16_t y = MDT_MENU_VALUE_Y + i * MDT_FORM_ROW_H;
  const bool     sel = (i == mdt_form_field);
  const uint16_t bg = sel ? ((mdt_form_edit && i < nf) ? RED : DX_DARKCYAN) : COLOR_BACKGROUND;

  checkWebRemote();
  display.fillRect(MDT_MENU_X0 + 3, y - 1, MDT_MENU_W - 6, MDT_FORM_ROW_H - 1, bg);
  display.setTextSize(1);

  if (i >= nf) {
    display.setTextColor(sel ? COLOR_SYSTEXT : GREEN, bg);
    display.setCursor(MDT_MENU_X0 + 5, y);
    display.print(F(">> APPLY"));
    return;
  }

  static const char* const qz_labels[MDT_QZ_COUNT] = { "ROOT", "SCALE" };
  static const char* const gen_labels[MDT_GF_COUNT] = { "ROOT", "CHORD", "OCTAVE",
                                                        "STYLE", "DENSITY", "VARIANT" };
  const bool qz = (mdt_menu_state == MDT_MENU_QUANTIZE);
  const uint8_t v = _mdt_form_vals()[i];

  display.setTextColor(sel ? COLOR_SYSTEXT : GREY1, bg);
  display.setCursor(MDT_MENU_X0 + 5, y);
  display.print(qz ? qz_labels[i] : gen_labels[i]);

  display.setTextColor(sel ? COLOR_SYSTEXT : COLOR_PITCHSMP, bg);
  display.setCursor(MDT_MENU_X0 + 58, y);
  if (i == 0)                                   display.print(noteNames[v % 12]);
  else if (qz)                                  display.print(mdt_scales[v].name);
  else if (i == MDT_GF_CHORD)                   display.print(mdt_chords[v].name);
  else if (i == MDT_GF_OCT)                     display.printf("%d", v);
  else if (i == MDT_GF_STYLE)                   display.print(mdt_gen_style_names[v]);
  else if (i == MDT_GF_DENS)                    display.printf("%d/5", v);
  else                                          display.printf("%03d", v);
}

static FLASHMEM void _mdt_form_draw_all(void)
{
  for (uint8_t i = 0; i <= _mdt_form_nfields(); i++) _mdt_form_draw_row(i);
}

static FLASHMEM void _mdt_form_preview(void)
{
  _mdt_restore_pattern();
  if (mdt_menu_state == MDT_MENU_QUANTIZE) {
    _mdt_action_quantize(mdt_qz_val[MDT_QZ_ROOT], mdt_qz_val[MDT_QZ_SCALE]);
  }
  else {
    _mdt_action_generate(mdt_gen_val[MDT_GF_ROOT], mdt_gen_val[MDT_GF_CHORD],
      mdt_gen_val[MDT_GF_OCT], mdt_gen_val[MDT_GF_STYLE],
      mdt_gen_val[MDT_GF_DENS], mdt_gen_val[MDT_GF_SEED]);
  }
  _mdt_redraw_left_rows();
}

static FLASHMEM void _mdt_form_open(uint8_t state)
{
  _mdt_backup_pattern();
  mdt_menu_state = state;
  mdt_form_field = 0;
  mdt_form_edit = false;
  _mdt_form_preview();
  _mdt_menu_full_redraw();
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_form_commit(void)
{
  mdt_form_edit = false;
  _mdt_clear_backup();
  _mdt_close_menu();
}

static FLASHMEM void _mdt_form_change_value(int8_t dir)
{
  const uint8_t f = mdt_form_field;
  if (f >= _mdt_form_nfields()) return;
  uint8_t lo, hi;
  _mdt_form_range(f, &lo, &hi);
  uint8_t* v = &_mdt_form_vals()[f];
  int16_t nv = (int16_t)*v + dir;
  if (nv < lo) nv = hi;
  if (nv > hi) nv = lo;
  *v = (uint8_t)nv;
  _mdt_form_preview();
  _mdt_form_draw_row(f);
}

static FLASHMEM void _mdt_form_select(uint8_t f)
{
  if (f == mdt_form_field) return;
  const uint8_t old = mdt_form_field;
  mdt_form_field = f;
  mdt_form_edit = false;
  _mdt_form_draw_row(old);
  _mdt_form_draw_row(f);
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_form_adjust(int8_t dir)
{
  if (mdt_form_edit) { _mdt_form_change_value(dir); return; }
  const int16_t n = _mdt_form_nfields() + 1;
  int16_t f = (int16_t)mdt_form_field + dir;
  if (f < 0)  f = n - 1;
  if (f >= n) f = 0;
  _mdt_form_select((uint8_t)f);
}

static FLASHMEM void _mdt_form_press(void)
{
  if (mdt_form_field >= _mdt_form_nfields()) { _mdt_form_commit(); return; }
  mdt_form_edit = !mdt_form_edit;
  _mdt_form_draw_row(mdt_form_field);
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_form_tap(uint8_t row)
{
  if (row > _mdt_form_nfields()) return;
  if (row == _mdt_form_nfields()) { _mdt_form_commit(); return; }
  if (row == mdt_form_field) _mdt_form_press();
  else                       _mdt_form_select(row);
}

static FLASHMEM void _mdt_menu_execute(void)
{
  if (mdt_menu_state == MDT_MENU_MAIN) {
    switch (mdt_menu_highlight) {
    case MDT_MI_CLOSE:      _mdt_close_menu(); return;
    case MDT_MI_STEP_REC:
      if (seq.running) return;
      _mdt_set_step_recording(!mdt_step_recording);
      _mdt_menu_draw_row(MDT_MI_STEP_REC, true);
      return;
    case MDT_MI_PLAY_MODE:
      seq.play_mode = !seq.play_mode;
      if (!seq.play_mode) seq.hunt_pattern = false;
      _mdt_menu_draw_row(MDT_MI_PLAY_MODE, true);
      _mdt_draw_touch_buttons();
      return;
    case MDT_MI_CLEAR_PAT:
      mdt_menu_state = MDT_MENU_CLEAR_PAT_CONFIRM;
      _mdt_menu_full_redraw(); return;
    case MDT_MI_CLEAR_ALL:
      mdt_menu_state = MDT_MENU_CLEAR_ALL_CONFIRM;
      _mdt_menu_full_redraw(); return;
    case MDT_MI_COPY:
      mdt_menu_state = MDT_MENU_COPY;
      mdt_menu_value = (seq.active_pattern + 1) % NUM_SEQ_PATTERN;
      _mdt_menu_full_redraw(); return;
    case MDT_MI_SWAP:
      mdt_menu_state = MDT_MENU_SWAP;
      mdt_menu_value = (seq.active_pattern + 1) % NUM_SEQ_PATTERN;
      _mdt_menu_full_redraw(); return;
    case MDT_MI_FILL:
      _mdt_backup_pattern();
      mdt_menu_state = MDT_MENU_FILL_SAMPLE;
      mdt_menu_value = 0;
      mdt_menu_value2 = 0;
      _mdt_restore_pattern();
      _mdt_action_fill_pattern(0, 0);
      _mdt_redraw_left_rows();
      _mdt_menu_full_redraw(); return;
    case MDT_MI_TRANSPOSE:
      _mdt_backup_pattern();
      mdt_menu_state = MDT_MENU_TRANSPOSE;
      mdt_menu_value = 0;
      _mdt_menu_full_redraw(); return;
    case MDT_MI_CLEAR_COLS:
      _mdt_backup_pattern();
      mdt_menu_state = MDT_MENU_CLEAR_COLS;
      mdt_menu_value = 0;
      _mdt_action_clear_cols(0);
      _mdt_redraw_left_rows();
      _mdt_menu_full_redraw(); return;
    case MDT_MI_QUANTIZE:
      if (_mdt_quantize_allowed()) _mdt_form_open(MDT_MENU_QUANTIZE);
      return;
    case MDT_MI_GENERATE:
      if (_mdt_generate_allowed()) _mdt_form_open(MDT_MENU_GENERATE);
      return;
    }
  }
  else if (mdt_menu_state == MDT_MENU_COPY) {
    _mdt_action_copy_pattern((uint8_t)mdt_menu_value);
    _mdt_close_menu();
  }
  else if (mdt_menu_state == MDT_MENU_SWAP) {
    _mdt_action_swap_pattern((uint8_t)mdt_menu_value);
    _mdt_close_menu();
  }
  else if (mdt_menu_state == MDT_MENU_FILL_SAMPLE) {
    mdt_menu_state = MDT_MENU_FILL_DIV;
    mdt_menu_value2 = mdt_menu_value;
    mdt_menu_value = 0;
    _mdt_restore_pattern();
    _mdt_action_fill_pattern((uint8_t)mdt_menu_value2, (uint8_t)mdt_menu_value);
    _mdt_redraw_left_rows();
    _mdt_menu_full_redraw();
  }
  else if (mdt_menu_state == MDT_MENU_FILL_DIV) {
    _mdt_clear_backup(); _mdt_close_menu();
  }
  else if (mdt_menu_state == MDT_MENU_TRANSPOSE ||
    mdt_menu_state == MDT_MENU_CLEAR_COLS) {
    _mdt_clear_backup(); _mdt_close_menu();
  }
  else if (_mdt_is_form_state()) {
    _mdt_form_press();
  }
  else if (mdt_menu_state == MDT_MENU_CLEAR_PAT_CONFIRM) {
    _mdt_action_clear_pattern(); _mdt_close_menu();
  }
  else if (mdt_menu_state == MDT_MENU_CLEAR_ALL_CONFIRM) {
    _mdt_action_clear_all_patterns(); _mdt_close_menu();
  }
}

static FLASHMEM void _mdt_menu_adjust(int8_t dir)
{
  if (_mdt_is_form_state()) { _mdt_form_adjust(dir); return; }
  if (mdt_menu_state == MDT_MENU_CLEAR_COLS) {
    mdt_menu_value = mdt_menu_value ? 0 : 1;
    _mdt_restore_pattern();
    _mdt_action_clear_cols((uint8_t)mdt_menu_value);
    _mdt_redraw_left_rows();
    _mdt_menu_draw_value_line();
    return;
  }
  if (mdt_menu_state == MDT_MENU_MAIN) {
    int16_t h = (int16_t)mdt_menu_highlight + dir;
    if (h < 0)             h = MDT_MI_COUNT - 1;
    if (h >= MDT_MI_COUNT) h = 0;
    if (h != mdt_menu_highlight) {
      uint8_t old = mdt_menu_highlight;
      mdt_menu_highlight = (uint8_t)h;
      _mdt_menu_draw_row(old, false);
      _mdt_menu_draw_row(mdt_menu_highlight, true);
    }
  }
  else if (mdt_menu_state == MDT_MENU_COPY || mdt_menu_state == MDT_MENU_SWAP) {
    int16_t v = mdt_menu_value + dir;
    if (v < 0)                            v = NUM_SEQ_PATTERN - 1;
    if (v >= NUM_SEQ_PATTERN)             v = 0;
    if (v == (int16_t)seq.active_pattern) v += dir;
    if (v < 0)                            v = NUM_SEQ_PATTERN - 1;
    if (v >= NUM_SEQ_PATTERN)             v = 0;
    mdt_menu_value = v;
    _mdt_menu_draw_value_line();
  }
  else if (mdt_menu_state == MDT_MENU_FILL_SAMPLE) {
    int16_t v = mdt_menu_value + dir;
    if (seq.content_type[seq.active_pattern] == 0 ||
      seq.content_type[seq.active_pattern] == 3) {
      if (v < 0)                   v = NUM_DRUMSET_CONFIG - 1;
      if (v >= NUM_DRUMSET_CONFIG) v = 0;
    }
    else {
      if (v < 0)   v = 108;
      if (v > 108) v = 0;
    }
    mdt_menu_value = v;
    _mdt_restore_pattern();
    _mdt_action_fill_pattern((uint8_t)mdt_menu_value, 0);
    _mdt_redraw_left_rows();
    _mdt_menu_draw_value_line();
  }
  else if (mdt_menu_state == MDT_MENU_FILL_DIV) {
    int16_t v = mdt_menu_value + dir;
    if (v < 0) v = 2;
    if (v > 2) v = 0;
    mdt_menu_value = v;
    _mdt_restore_pattern();
    _mdt_action_fill_pattern((uint8_t)mdt_menu_value2, (uint8_t)mdt_menu_value);
    _mdt_redraw_left_rows();
    _mdt_menu_draw_value_line();
  }
  else if (mdt_menu_state == MDT_MENU_TRANSPOSE) {
    int16_t v = mdt_menu_value + dir;
    if (v < -36) v = 36;
    if (v > 36) v = -36;
    mdt_menu_value = v;
    _mdt_restore_pattern();
    _mdt_action_transpose_pattern((int8_t)mdt_menu_value);
    _mdt_redraw_left_rows();
    _mdt_menu_draw_value_line();
  }
}

static FLASHMEM void _mdt_menu_back(void)
{
  if (mdt_menu_state == MDT_MENU_MAIN) {
    _mdt_close_menu();
    return;
  }
  if (mdt_menu_has_backup) {
    _mdt_restore_pattern();
    _mdt_redraw_left_rows();
    _mdt_clear_backup();
  }
  mdt_form_edit = false;
  mdt_menu_state = MDT_MENU_MAIN;
  _mdt_menu_full_redraw();
  _mdt_update_oled_help();
}

static FLASHMEM void _mdt_periodic(void)
{
  if (mdt_preview_active_count > 0 &&
    mdt_preview_hold_timer >= MDT_PREVIEW_HOLD_MS) {
    _mdt_preview_release_all();
  }
  _mdt_update_playhead();
  _mdt_handle_step_recording();
}

// ===========================================================================
//  PREVIEW helpers
// ===========================================================================
static FLASHMEM void _mdt_preview_invalidate(void)
{
  mdt_pr_last_pattern = 0xFF;
  mdt_pr_last_ct = 0xFF;
  mdt_pr_last_lo = -1;
  mdt_pr_last_hi = -1;
  mdt_pr_last_h = 0;
  mdt_pr_last_cursor = 0xFF;

  mdt_dr_last_pattern = 0xFF;
  mdt_dr_last_cursor = 0xFF;
  mdt_dr_last_play = 0xFF;
}

#if NUM_DRUMS > 0
static FLASHMEM uint8_t _mdt_drum_count_unique(uint8_t* out_idx, uint8_t* out_class, uint8_t max)
{
  uint8_t count = 0;
  for (uint8_t s = 0; s < MDT_ROWS && count < max; s++) {
    const uint8_t note = seq.note_data[seq.active_pattern][s];
    const uint8_t vel = seq.vel[seq.active_pattern][s];
    const uint8_t idx = _mdt_drum_index_for_step(note, vel);
    if (idx == 0xFF) continue;
    bool found = false;
    for (uint8_t i = 0; i < count; i++) {
      if (out_idx[i] == idx) { found = true; break; }
    }
    if (!found) {
      out_idx[count] = idx;
      out_class[count] = drum_config[idx].drum_class ? drum_config[idx].drum_class : 11;
      count++;
    }
  }
  return count;
}

static FLASHMEM uint8_t _mdt_drum_row_h(uint8_t count)
{
  if (count == 0) return 6;
  uint8_t rh = (MDT_PREV_H - 4) / count;
  if (rh > 12) rh = 12;
  if (rh < 6)  rh = 6;
  return rh;
}

static FLASHMEM uint16_t _mdt_drum_class_color(uint8_t cls)
{
  switch (cls) {
  case 1:  return RED;
  case 2:  return COLOR_DRUMS;
  case 3:  return COLOR_ARP;
  case 4:  return PINK;
  case 5:  return COLOR_CHORDS;
  case 6:  return COLOR_CHORDS;
  case 7:  return YELLOW;
  case 8:  return YELLOW;
  case 9:  return YELLOW;
  case 10: return MIDDLEGREEN;
  case 11: return COLOR_PITCHSMP;
  default: return COLOR_SYSTEXT;
  }
}

static FLASHMEM void _mdt_dr_redraw_col(uint8_t step)
{
  const uint16_t x0 = MDT_PREV_STEP_X(step);
  const uint16_t x1 = x0 + MDT_PREV_STEP_W - 1;
  const bool is_cursor = (step == mdt_cursor_row);
  checkWebRemote();
  display.fillRect(x0, mdt_dr_yt, MDT_PREV_STEP_W,
    mdt_dr_yb - mdt_dr_yt + 1,
    is_cursor ? DX_DARKCYAN : COLOR_BACKGROUND);

  if ((step + 1) % 4 == 0)
    display.drawLine(x1, mdt_dr_yt, x1, mdt_dr_yb, GREY3);

  for (uint8_t i = 0; i <= mdt_dr_count; i++) {
    uint8_t gy = mdt_dr_yt + i * mdt_dr_row_h;
    if (gy > mdt_dr_yb) break;
    display.drawLine(x0, gy, x1, gy, GREY3);
  }

  display.drawLine(x0, mdt_dr_yt, x1, mdt_dr_yt, GREY3);
  display.drawLine(x0, mdt_dr_yb, x1, mdt_dr_yb, GREY3);

  if (step == 0) display.drawLine(x0, mdt_dr_yt, x0, mdt_dr_yb, GREY3);

  const uint8_t note = seq.note_data[seq.active_pattern][step];
  const uint8_t vel = seq.vel[seq.active_pattern][step];
  const uint8_t idx = _mdt_drum_index_for_step(note, vel);
  if (idx == 0xFF) return;

  int8_t row = -1;
  for (uint8_t i = 0; i < mdt_dr_count; i++)
    if (mdt_dr_uniq[i] == idx) { row = i; break; }
  if (row < 0) return;

  const bool beyond = (int16_t)step > (15 - seq.pattern_len_dec);
  const uint16_t col = beyond ? GREY3 : _mdt_drum_class_color(mdt_dr_uniq_class[row]);

  char c = drum_config[idx].shortname[0];
  if (c == '\0') c = 'x';

  const uint8_t y = mdt_dr_yt + row * mdt_dr_row_h + (mdt_dr_row_h - 7) / 2;
  if (y + 7 > mdt_dr_yb) return;
  const uint16_t tbg = is_cursor ? DX_DARKCYAN : COLOR_BACKGROUND;
  display.setTextSize(1);
  display.setTextColor(col, tbg);
  display.setCursor(x0 + 1, y);
  display.print(c);
}

static FLASHMEM void _mdt_dr_draw_empty_label(uint8_t pane_h)
{
  if (pane_h > MDT_PREV_H) pane_h = MDT_PREV_H;
  display.setTextSize(1);
  display.setTextColor(GREY3);
  display.setCursor(MDT_PREV_X + (MDT_PREV_W - 30) / 2,
    MDT_PREV_Y + pane_h / 2 - 4);
  display.print(F("EMPTY"));
}

static FLASHMEM void _mdt_dr_full_redraw(uint8_t cursor_row, uint8_t pane_h)
{
  if (pane_h > MDT_PREV_H) pane_h = MDT_PREV_H;

  mdt_dr_count = _mdt_drum_count_unique(mdt_dr_uniq, mdt_dr_uniq_class, 16);
  mdt_dr_row_h = _mdt_drum_row_h(mdt_dr_count);
  mdt_dr_yt = MDT_PREV_Y + 2;
  mdt_dr_yb = MDT_PREV_Y + pane_h - 3;

  {
    uint8_t max_rows = (mdt_dr_yb - mdt_dr_yt) / mdt_dr_row_h;
    if (mdt_dr_count > max_rows) {
      mdt_dr_count = max_rows;
      mdt_dr_yb = mdt_dr_yt + mdt_dr_count * mdt_dr_row_h;
    }
  }

  if (mdt_dr_count > 0) {
    for (uint8_t i = 1; i < mdt_dr_count; i++) {
      const uint8_t k = mdt_dr_uniq[i];
      const uint8_t c = mdt_dr_uniq_class[i];
      int8_t j = (int8_t)i - 1;
      while (j >= 0 && (mdt_dr_uniq_class[j] > c ||
        (mdt_dr_uniq_class[j] == c && mdt_dr_uniq[j] > k))) {
        mdt_dr_uniq[j + 1] = mdt_dr_uniq[j];
        mdt_dr_uniq_class[j + 1] = mdt_dr_uniq_class[j];
        j--;
      }
      mdt_dr_uniq[j + 1] = k;
      mdt_dr_uniq_class[j + 1] = c;
    }
  }
  checkWebRemote();
  display.fillRect(MDT_PREV_X, MDT_PREV_Y, MDT_PREV_W, MDT_PREV_H, COLOR_BACKGROUND);

  if (mdt_dr_count == 0) {
    display.drawLine(MDT_PREV_STEP_X(0), mdt_dr_yt, MDT_PREV_DR_RIGHT, mdt_dr_yt, GREY3);
    display.drawLine(MDT_PREV_STEP_X(0), mdt_dr_yb, MDT_PREV_DR_RIGHT, mdt_dr_yb, GREY3);
    for (uint8_t s = 0; s <= 16; s += 4) {
      uint16_t gx = (s == 0) ? MDT_PREV_STEP_X(0)
        : (s == 16) ? MDT_PREV_DR_RIGHT
        : MDT_PREV_STEP_X(s) - 1;
      display.drawLine(gx, mdt_dr_yt, gx, mdt_dr_yb, GREY3);
    }
    mdt_dr_last_cursor = cursor_row;
    mdt_dr_last_play = seq.running ? seq.step : 0xFF;
    if (mdt_dr_last_cursor < MDT_ROWS) _mdt_dr_redraw_col(mdt_dr_last_cursor);
    _mdt_dr_draw_empty_label(pane_h);
    return;
  }

  for (uint8_t s = 0; s < MDT_ROWS; s++)
    _mdt_dr_redraw_col(s);

  mdt_dr_last_cursor = cursor_row;
  mdt_dr_last_play = seq.running ? seq.step : 0xFF;
}

static FLASHMEM void _mdt_dr_update(uint8_t cursor_row, uint8_t pane_h)
{
  uint8_t new_uniq[16], new_class[16];
  const uint8_t new_count = _mdt_drum_count_unique(new_uniq, new_class, 16);

  bool list_changed = (mdt_dr_last_pattern != seq.active_pattern);

  if (!list_changed) {
    if (new_count != mdt_dr_count) {
      list_changed = true;
    }
    else {
      for (uint8_t i = 0; i < new_count && !list_changed; i++) {
        bool found = false;
        for (uint8_t j = 0; j < mdt_dr_count; j++) {
          if (mdt_dr_uniq[j] == new_uniq[i]) { found = true; break; }
        }
        if (!found) list_changed = true;
      }
    }
  }

  if (list_changed) {
    _mdt_dr_full_redraw(cursor_row, pane_h);
    mdt_dr_last_pattern = seq.active_pattern;
    return;
  }

  const uint8_t cur_play = seq.running ? seq.step : 0xFF;

  bool repainted = false;

  if (mdt_dr_last_cursor != cursor_row) {
    if (mdt_dr_last_cursor < MDT_ROWS) _mdt_dr_redraw_col(mdt_dr_last_cursor);
    if (cursor_row < MDT_ROWS) _mdt_dr_redraw_col(cursor_row);
    mdt_dr_last_cursor = cursor_row;
    repainted = true;
  }

  if (mdt_dr_last_play != cur_play) {
    if (mdt_dr_last_play < MDT_ROWS) _mdt_dr_redraw_col(mdt_dr_last_play);
    if (cur_play < MDT_ROWS) _mdt_dr_redraw_col(cur_play);
    mdt_dr_last_play = cur_play;
    repainted = true;
  }

  if (repainted && mdt_dr_count == 0) _mdt_dr_draw_empty_label(pane_h);
}
#endif

// ===========================================================================
//  Piano roll preview
// ===========================================================================
static FLASHMEM inline int16_t _mdt_pr_pitch_y(int16_t note, int16_t disp_lo, uint8_t y_bot)
{
  return (int16_t)y_bot - (int16_t)(note - disp_lo) * MDT_PR_ROWH - MDT_PR_ROWH + 1;
}

static FLASHMEM void _mdt_pr_get_range(uint8_t pane_h, int16_t* lo_out, int16_t* hi_out)
{
  const uint8_t y_top = MDT_PREV_Y + 2;
  const uint8_t y_bot = MDT_PREV_Y + pane_h - 3;
  const uint8_t inner_h = y_bot - y_top + 1;

  int16_t pmin = 128, pmax = -1;
  const bool chord_mode = (seq.content_type[seq.active_pattern] == 2);

  for (uint8_t i = 0; i < MDT_ROWS; i++) {
    const uint8_t n = seq.note_data[seq.active_pattern][i];
    if (n == 0 || n == 130 || n >= 128) continue;

    if (n < pmin) pmin = n;
    if (n > pmax) pmax = n;

    if (chord_mode) {
      const uint8_t vel = seq.vel[seq.active_pattern][i];
      if (vel >= 200 && vel <= 205) {
        const uint8_t chord_type = (uint8_t)(vel - 200);
        uint8_t count = seq.chord_key_ammount;
        if (count == 0) count = 4;
        if (count > 6)  count = 6;
        for (uint8_t k = 1; k < count; k++) {
          int16_t cn = (int16_t)n + seq.arps[chord_type][k];
          if (cn < 0 || cn > 127) continue;
          if (cn < pmin) pmin = (int16_t)cn;
          if (cn > pmax) pmax = (int16_t)cn;
        }
      }
    }
  }
  if (pmax < 0) { pmin = 60; pmax = 72; }

  const uint8_t visible_semis = inner_h / MDT_PR_ROWH;
  const int16_t centre = (pmin + pmax) / 2;
  int16_t disp_lo = centre - (int16_t)visible_semis / 2;
  int16_t disp_hi = disp_lo + (int16_t)visible_semis - 1;
  if (disp_lo < 0) { disp_lo = 0;   disp_hi = (int16_t)visible_semis - 1; }
  if (disp_hi > 127) { disp_hi = 127; disp_lo = disp_hi - (int16_t)visible_semis + 1; }
  if (disp_lo < 0) { disp_lo = 0;   disp_hi = (int16_t)visible_semis - 1; }

  *lo_out = disp_lo;
  *hi_out = disp_hi;
}

static FLASHMEM void _mdt_pr_draw_keys(int16_t lo, int16_t hi, uint8_t y_top, uint8_t y_bot)
{
  const uint8_t inner_h = y_bot - y_top + 1;
  const uint16_t kb_x = MDT_PREV_KB_X;
  const uint8_t  kb_w = MDT_PREV_KB_W;
  const uint8_t  black_w = (kb_w * 2) / 3;
  checkWebRemote();
  display.fillRect(kb_x, y_top, kb_w, inner_h, COLOR_SYSTEXT);

  for (int16_t n = lo; n <= hi; n++) {
    int16_t yt = _mdt_pr_pitch_y(n, lo, y_bot);
    int16_t yb = yt + MDT_PR_ROWH - 1;
    if (yb < (int16_t)y_top || yt >(int16_t)y_bot) continue;
    if (yt < (int16_t)y_top) yt = y_top;
    if (yb > (int16_t)y_bot) yb = y_bot;
    if (yb < yt) continue;

    const int16_t pc = ((n % 12) + 12) % 12;
    const bool is_black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
    if (is_black) {
      checkWebRemote();
      display.fillRect(kb_x, (uint8_t)yt, black_w, (uint8_t)(yb - yt + 1), COLOR_BACKGROUND);
      const int16_t ym = _mdt_pr_pitch_y(n, lo, y_bot) + MDT_PR_ROWH / 2;
      if (ym >= (int16_t)y_top && ym <= (int16_t)y_bot)
        display.drawLine(kb_x + black_w, (uint8_t)ym, kb_x + kb_w - 1, (uint8_t)ym, GREY2);
    }
    else if ((pc == 0 || pc == 5) && n > lo) {
      const int16_t yl = _mdt_pr_pitch_y(n, lo, y_bot) + MDT_PR_ROWH - 1;
      if (yl >= (int16_t)y_top && yl <= (int16_t)y_bot)
        display.drawLine(kb_x, (uint8_t)yl, kb_x + kb_w - 1, (uint8_t)yl, GREY2);
    }
  }

  display.drawLine(kb_x + kb_w, y_top, kb_x + kb_w, y_bot, GREY3);
  display.drawLine(MDT_PREV_IX0, y_top, kb_x + kb_w, y_top, GREY3);
  display.drawLine(MDT_PREV_IX0, y_bot, kb_x + kb_w, y_bot, GREY3);
}

static FLASHMEM void _mdt_pr_redraw_col(bool chord_mode, uint8_t step,
  int16_t lo, int16_t hi,
  uint8_t y_top, uint8_t y_bot)
{
  const uint16_t x0 = MDT_PREV_STEP_XK(step);
  const uint16_t x1 = x0 + MDT_PREV_STEP_WK - 1;
  const bool is_cursor = (step == mdt_cursor_row);
  checkWebRemote();
  display.fillRect(x0, y_top, MDT_PREV_STEP_WK, y_bot - y_top + 1,
    is_cursor ? DX_DARKCYAN : COLOR_BACKGROUND);

  if ((step + 1) % 4 == 0) display.drawLine(x1, y_top, x1, y_bot, GREY3);

  for (int16_t n = lo; n <= hi; n++) {
    if (((n % 12) + 12) % 12 != 0 || n == lo) continue;
    const int16_t yl = _mdt_pr_pitch_y(n, lo, y_bot) + MDT_PR_ROWH - 1;
    if (yl < (int16_t)y_top || yl >(int16_t)y_bot) continue;
    display.drawLine(x0, (uint8_t)yl, x1, (uint8_t)yl, GREY3);
  }

  display.drawLine(x0, y_top, x1, y_top, GREY3);
  display.drawLine(x0, y_bot, x1, y_bot, GREY3);

  const uint8_t note = seq.note_data[seq.active_pattern][step];
  if (note == 0) return;

  int16_t last_pitch = -1;
  for (uint8_t s = 0; s <= step; s++) {
    const uint8_t n = seq.note_data[seq.active_pattern][s];
    if (n > 0 && n != 130) last_pitch = (int16_t)n;
  }

  int16_t pitched_note;
  bool is_latch = false;
  if (note == 130) {
    if (last_pitch < 0) return;
    pitched_note = last_pitch;
    is_latch = true;
  }
  else {
    pitched_note = (int16_t)note;
  }

  const uint8_t vel = seq.vel[seq.active_pattern][step];
  const bool beyond = (int16_t)step > (15 - seq.pattern_len_dec);

  uint16_t col;
  uint8_t  chord_type = 0;
  bool     chord_ok = false;
  if (is_latch) col = GREEN;
  else if (beyond) col = GREY3;
  else if (chord_mode) {
    if (vel >= 200 && vel <= 205) {
      chord_type = (uint8_t)(vel - 200);
      chord_ok = true;
      col = _mdt_chord_color(chord_type);
    }
    else {
      col = GREY1;
    }
  }
  else {
    col = _mdt_shade_by_velocity(_mdt_type_color(), vel);
  }

  if (pitched_note >= lo && pitched_note <= hi) {
    int16_t yt = _mdt_pr_pitch_y(pitched_note, lo, y_bot);
    int16_t fill_h = MDT_PR_ROWH - 1;
    if (yt + fill_h > (int16_t)y_bot) fill_h = (int16_t)y_bot - yt + 1;
    if (fill_h > 0 && yt >= (int16_t)y_top) {
      checkWebRemote();
      display.fillRect(x0 + 1, (uint8_t)yt, MDT_PREV_STEP_WK - 2,
        (uint8_t)fill_h, col);
    }
  }

  if (chord_mode && chord_ok && !is_latch) {
    uint8_t count = seq.chord_key_ammount;
    if (count == 0) count = 4;
    if (count > 6)  count = 6;
    for (uint8_t i = 1; i < count; i++) {
      const uint8_t offset = seq.arps[chord_type][i];
      int16_t cn = pitched_note + offset;
      if (cn < 0)   cn = 0;
      if (cn > 127) cn = 127;
      if (cn < lo || cn > hi) continue;

      int16_t cyt = _mdt_pr_pitch_y((uint8_t)cn, lo, y_bot);
      int16_t fill_h = MDT_PR_ROWH - 1;
      if (cyt + fill_h > (int16_t)y_bot) fill_h = (int16_t)y_bot - cyt + 1;
      if (fill_h <= 0 || cyt < (int16_t)y_top) continue;

      checkWebRemote();
      display.fillRect(x0 + 1, (uint8_t)cyt, MDT_PREV_STEP_WK - 2,
        (uint8_t)fill_h, col);
    }
  }
}

static FLASHMEM void _mdt_pr_draw_step_grid(bool chord_mode, int16_t lo, int16_t hi,
  uint8_t y_top, uint8_t y_bot)
{
  const uint16_t step0_x = MDT_PREV_STEP_XK(0) - 2;

  for (uint8_t s = 4; s < 16; s += 4) {
    const uint16_t gx = MDT_PREV_STEP_XK(s) - 1;
    display.drawLine(gx, y_top, gx, y_bot, GREY3);
  }

  for (int16_t n = lo; n <= hi; n++) {
    if (((n % 12) + 12) % 12 != 0 || n == lo) continue;
    const int16_t yl = _mdt_pr_pitch_y(n, lo, y_bot) + MDT_PR_ROWH - 1;
    if (yl < (int16_t)y_top || yl >(int16_t)y_bot) continue;
    display.drawLine(step0_x, (uint8_t)yl, MDT_PREV_PR_RIGHT, (uint8_t)yl, GREY3);
  }

  display.drawLine(step0_x, y_top, MDT_PREV_PR_RIGHT, y_top, GREY3);
  display.drawLine(step0_x, y_bot, MDT_PREV_PR_RIGHT, y_bot, GREY3);
  display.drawLine(step0_x, y_top, step0_x, y_bot, GREY3);

  (void)chord_mode;
}

static FLASHMEM void _mdt_pr_full_redraw(bool chord_mode, uint8_t cursor_row,
  int16_t lo, int16_t hi, uint8_t pane_h)
{
  const uint8_t y_top = MDT_PREV_Y + 2;
  const uint8_t y_bot = MDT_PREV_Y + pane_h - 3;
  checkWebRemote();
  display.fillRect(MDT_PREV_X, MDT_PREV_Y, MDT_PREV_W, MDT_PREV_H, COLOR_BACKGROUND);

  _mdt_pr_draw_keys(lo, hi, y_top, y_bot);
  _mdt_pr_draw_step_grid(chord_mode, lo, hi, y_top, y_bot);

  for (uint8_t s = 0; s < MDT_ROWS; s++)
    _mdt_pr_redraw_col(chord_mode, s, lo, hi, y_top, y_bot);

  mdt_pr_last_cursor = cursor_row;
}

static FLASHMEM void _mdt_pr_update(bool chord_mode, uint8_t cursor_row, uint8_t pane_h)
{
  const uint8_t ct = seq.content_type[seq.active_pattern];
  int16_t lo, hi;
  _mdt_pr_get_range(pane_h, &lo, &hi);

  const bool layout_changed =
    (mdt_pr_last_pattern != seq.active_pattern) ||
    (mdt_pr_last_ct != ct) ||
    (mdt_pr_last_lo != lo) ||
    (mdt_pr_last_hi != hi) ||
    (mdt_pr_last_h != pane_h);

  if (layout_changed) {
    _mdt_pr_full_redraw(chord_mode, cursor_row, lo, hi, pane_h);
    mdt_pr_last_pattern = seq.active_pattern;
    mdt_pr_last_ct = ct;
    mdt_pr_last_lo = lo;
    mdt_pr_last_hi = hi;
    mdt_pr_last_h = pane_h;
    return;
  }

  const uint8_t y_top = MDT_PREV_Y + 2;
  const uint8_t y_bot = MDT_PREV_Y + pane_h - 3;

  if (mdt_pr_last_cursor != cursor_row) {
    if (mdt_pr_last_cursor < MDT_ROWS)
      _mdt_pr_redraw_col(chord_mode, mdt_pr_last_cursor, lo, hi, y_top, y_bot);
    if (cursor_row < MDT_ROWS)
      _mdt_pr_redraw_col(chord_mode, cursor_row, lo, hi, y_top, y_bot);
    mdt_pr_last_cursor = cursor_row;
  }
}

static FLASHMEM void _mdt_preview_slice(uint8_t cursor_row, uint8_t pane_h)
{
  const uint8_t y_top = MDT_PREV_Y + 2;
  const uint8_t y_bot = MDT_PREV_Y + pane_h - 3;
  checkWebRemote();
  display.fillRect(MDT_PREV_X, MDT_PREV_Y, MDT_PREV_W, MDT_PREV_H, COLOR_BACKGROUND);

  for (uint8_t s = 0; s <= 16; s += 4) {
    uint16_t gx = (s == 0) ? MDT_PREV_IX0
      : (s == 16) ? MDT_PREV_IX1
      : MDT_PREV_STEP_X(s) - 1;
    if (gx < MDT_PREV_IX0) gx = MDT_PREV_IX0;
    if (gx > MDT_PREV_IX1) gx = MDT_PREV_IX1;
    display.drawLine(gx, y_top, gx, y_bot, GREY3);
  }
  display.drawLine(MDT_PREV_IX0, y_top, MDT_PREV_IX1, y_top, GREY3);
  display.drawLine(MDT_PREV_IX0, y_bot, MDT_PREV_IX1, y_bot, GREY3);

  if (cursor_row < MDT_ROWS) {
    uint16_t x = MDT_PREV_STEP_X(cursor_row);
    checkWebRemote();
    display.fillRect(x, y_top, MDT_PREV_STEP_W, y_bot - y_top + 1, DX_DARKCYAN);
  }

  static const uint16_t palette[] = {
    COLOR_ARP, COLOR_CHORDS, COLOR_PITCHSMP, MIDDLEGREEN,
    YELLOW, PINK, COLOR_DRUMS, COLOR_INSTR
  };
  const uint8_t palN = sizeof(palette) / sizeof(palette[0]);
  for (uint8_t s = 0; s < MDT_ROWS; s++) {
    const uint8_t note = seq.note_data[seq.active_pattern][s];
    if (note == 0) continue;
    const bool beyond = (int16_t)s > (15 - seq.pattern_len_dec);
    const uint16_t col = beyond ? GREY3 : palette[s % palN];
    checkWebRemote();
    display.fillRect(MDT_PREV_STEP_X(s) + 1, y_bot - 4, MDT_PREV_STEP_W - 3, 3, col);
  }
}

static FLASHMEM uint8_t _mdt_preview_compute_height(void)
{
  uint8_t h = MDT_PREV_H;
  switch (seq.content_type[seq.active_pattern]) {
  case 0: {
#if NUM_DRUMS > 0
    uint8_t uniq[16], cls[16];
    const uint8_t count = _mdt_drum_count_unique(uniq, cls, 16);
    if (count == 0) h = 30;
    else h = 6 + count * _mdt_drum_row_h(count);
#endif
    break;
  }
  case 1:
  case 2: h = MDT_PREV_H; break;
  case 3: h = 40; break;
  default: h = 30; break;
  }
  if (h > MDT_PREV_H) h = MDT_PREV_H;
  return h;
}

static FLASHMEM void _mdt_draw_pattern_preview(uint8_t cursor_row)
{
  if (mdt_is_vk_mode()) return;
  if (mdt_menu_state != MDT_MENU_OFF) return;
  const uint8_t ct = seq.content_type[seq.active_pattern];
  const uint8_t pane_h = _mdt_preview_compute_height();

  checkWebRemote();

  if (ct == 1 || ct == 2) { _mdt_pr_update(ct == 2, cursor_row, pane_h); return; }
  if (ct == 0) {
#if NUM_DRUMS > 0
    _mdt_dr_update(cursor_row, pane_h);
#endif
    return;
  }
  if (ct == 3) {
    _mdt_preview_slice(cursor_row, pane_h);
    mdt_pr_last_pattern = 0xFF;
    mdt_dr_last_pattern = 0xFF;
  }
}

// ===========================================================================
//  HEADER / CAPTIONS / CELLS / ROWS
// ===========================================================================
static FLASHMEM uint8_t _mdt_count_filled_steps(void)
{
  const uint8_t* notes = seq.note_data[seq.active_pattern];
  uint8_t filled = 0;
  for (uint8_t i = 0; i < MDT_ROWS; i++)
    if (notes[i] != 0) filled++;
  return filled;
}

static FLASHMEM void _mdt_draw_header(void)
{
  checkWebRemote();
  display.fillRect(0, 0, MDT_PREV_X, MDT_HDR_H, DX_DARKCYAN);
  display.setTextSize(1);

  if (mdt_cursor_col == MDT_COL_PATTERN) {
    checkWebRemote();
    display.fillRect(MDT_HDR_PAT_X - 1, 0, 3 * 6 + 2, MDT_HDR_H - 1, RED);
    display.setTextColor(COLOR_SYSTEXT, RED);
  }
  else {
    display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
  }
  display.setCursor(MDT_HDR_PAT_X, 1);
  display.print(F("P"));
  print_formatted_number(seq.active_pattern, 2);

  display.setTextColor(_mdt_type_color(), DX_DARKCYAN);
  display.setCursor(MDT_HDR_TYPE_TX, 1);
  display.print(F("TYPE:"));
  display.print(_mdt_type_label());

  {
    const uint8_t filled = _mdt_count_filled_steps();
    const uint8_t total = (uint8_t)(16 - seq.pattern_len_dec);
    char stat[12];
    snprintf(stat, sizeof(stat), "EV:%02u/%02u",
      (unsigned)filled, (unsigned)total);
    display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
    display.setCursor(MDT_HDR_STATS_X, 1);
    display.print(stat);
  }

  display.setCursor(MDT_HDR_MODE_X, 1);
  if (mdt_step_recording) {
    display.setTextColor(RED, DX_DARKCYAN);
    display.print(F("REC "));
  }
  else if (mdt_edit_state) {
    display.setTextColor(RED, DX_DARKCYAN);
    display.print(F("EDIT"));
  }
  else {
    display.setTextColor(GREY2, DX_DARKCYAN);
    display.print(F("VIEW"));
  }

  display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
  display.setCursor(MDT_HDR_BPM_X, 1);
  print_formatted_number(seq.bpm, 3);
  display.setTextColor(GREY1, DX_DARKCYAN);
  display.print(F(" BPM"));

  display.drawLine(0, MDT_SEP1_Y, MDT_PREV_X - 1, MDT_SEP1_Y, GREY3);
}

static FLASHMEM void _mdt_draw_col_captions(void)
{
  display.setTextSize(1);
  display.setTextColor(GREY2, COLOR_BACKGROUND);
  display.setCursor(MDT_ST_X, MDT_COLHDR_Y); display.print(F("ST"));
  display.setCursor(MDT_NOTE_X, MDT_COLHDR_Y); display.print(F("NOTE"));
  display.setCursor(MDT_VEL_X, MDT_COLHDR_Y); display.print(F("VEL"));
  display.setCursor(MDT_INS_X, MDT_COLHDR_Y); display.print(F("INS"));
  display.setCursor(MDT_CMD_X, MDT_COLHDR_Y); display.print(F("CMD"));
  display.setCursor(MDT_PAR_X, MDT_COLHDR_Y); display.print(F("PAR"));
  display.setCursor(MDT_INFO_X, MDT_COLHDR_Y); display.print(F("INFO"));

  if (!mdt_is_vk_mode()) {
    display.setCursor(MDT_PREV_X + 3, MDT_PREV_CAP_Y + 2);
    display.print(F("PREVIEW:"));
  }

  display.drawLine(0, MDT_SEP2_Y, MDT_PREV_X - 1, MDT_SEP2_Y, GREY3);
}

static void _mdt_draw_cell(uint16_t x, uint8_t y,
  const char* text,
  uint16_t content_fg,
  uint8_t  col_idx,
  bool     is_playhead,
  bool     is_cursor,
  uint8_t  cursor_col,
  bool     edit_state,
  bool     beyond_len,
  uint8_t  cell_w_chars,
  bool     is_preview = false)
{
  (void)is_playhead;

  uint16_t fg, bg;
  if (is_preview) {
    const bool on_cursor = is_cursor && col_idx != 0xFF && cursor_col == col_idx;
    bg = on_cursor ? (edit_state ? RED : DX_DARKCYAN) : COLOR_BACKGROUND;
    fg = on_cursor ? GREY1 : GREY2;
  }
  else if (is_cursor && col_idx != 0xFF && cursor_col == col_idx && edit_state) {
    fg = COLOR_SYSTEXT; bg = RED;
  }
  else if (is_cursor && col_idx != 0xFF && cursor_col == col_idx) {
    fg = COLOR_BACKGROUND; bg = DX_DARKCYAN;
  }
  else if (beyond_len) {
    fg = GREY3; bg = COLOR_BACKGROUND;
  }
  else {
    fg = content_fg; bg = COLOR_BACKGROUND;
  }
  checkWebRemote();
  display.fillRect(x - MDT_CELL_PAD, MDT_CELL_TOP(y),
    cell_w_chars * 6 + MDT_CELL_PAD * 2, MDT_CELL_H, bg);
  display.setTextSize(1);
  display.setTextColor(fg, bg);
  display.setCursor(x, y);
  display.print(text);
}

static FLASHMEM void _mdt_draw_st_cell(uint8_t row, bool is_playhead, bool is_cursor_row)
{
  const uint8_t y = mdt_row_display_y(row);
  if (y == 0xFF) return;
  const bool beyond_len = (int16_t)row > (15 - seq.pattern_len_dec);
  const bool row_is_empty = _mdt_step_is_empty(seq.content_type[seq.active_pattern],
    seq.note_data[seq.active_pattern][row],
    seq.vel[seq.active_pattern][row]);

  char sbuf[4];
  snprintf(sbuf, sizeof(sbuf), "%02d", (int)row + 1);

  uint16_t fg, bg;
  if (is_playhead) { fg = COLOR_BACKGROUND; bg = RED; }
  else if (is_cursor_row) { fg = COLOR_BACKGROUND; bg = DX_DARKCYAN; }
  else if (beyond_len) { fg = GREY3; bg = COLOR_BACKGROUND; }
  else if (row_is_empty) { fg = GREY3; bg = COLOR_BACKGROUND; }
  else if ((row & 3) == 0) { fg = GREY1; bg = COLOR_BACKGROUND; }
  else { fg = GREY2; bg = COLOR_BACKGROUND; }

  checkWebRemote();
  display.fillRect(MDT_ST_X - MDT_CELL_PAD, MDT_CELL_TOP(y),
    MDT_ST_W * 6 + MDT_CELL_PAD * 2, MDT_CELL_H, bg);
  display.setTextSize(1);
  display.setTextColor(fg, bg);
  display.setCursor(MDT_ST_X, y);
  display.print(sbuf);
}

static FLASHMEM void _mdt_draw_info_cell(uint8_t row, bool is_playhead, bool is_cursor_row)
{
  const uint8_t y = mdt_row_display_y(row);
  if (y == 0xFF) return;
  const bool beyond_len = (int16_t)row > (15 - seq.pattern_len_dec);
  const uint8_t note = seq.note_data[seq.active_pattern][row];
  const uint8_t vel = seq.vel[seq.active_pattern][row];
  const uint8_t ct = seq.content_type[seq.active_pattern];
  const uint8_t fxc = seq.fx_cmd[seq.active_pattern][row];

  const char* base_info = "";
  if (ct == 0) {
#if NUM_DRUMS > 0
    if (vel > 209)                    base_info = basename(drum_config[vel - 210].name);
    else if (note > 0 && note != 130) base_info = get_drum_name_from_note(note);
#endif
  }
  else if (ct == 3) {
    base_info = (note > 0 && vel > 199) ? _mdt_chord_full_name((uint8_t)(vel - 200)) : (note > 0 ? "ARP" : "");
  }
  else if (ct == 2) {
    base_info = (note > 0 && vel > 199) ? _mdt_chord_full_name((uint8_t)(vel - 200)) : (note > 0 ? "CHORD" : "");
  }
  else if (note == 130) {
    base_info = "LATCH";
  }

  char info_work[MDT_INFO_W + 1];
  memset(info_work, ' ', MDT_INFO_W);
  info_work[MDT_INFO_W] = 0;

  if (fxc != MDT_FX_NONE) {
    const uint8_t fxp = seq.fx_val[seq.active_pattern][row];
    const uint8_t fxm = mdt_fx_param_max[fxc];
    const char* fxn = mdt_fx_names[fxc];

    char tmp[16];

    if (mdt_cmd_is_voice(fxc)) {
      const uint8_t dx_idx = _mdt_voice_fx_target_instance(
          seq.selected_track, seq.active_pattern, row);
      if (dx_idx < NUM_DEXED) {
        const uint8_t cur_v = mdt_get_current_voice(dx_idx);
        const uint8_t cur_b = mdt_get_current_bank (dx_idx);
        const uint8_t cur_p = mdt_get_current_pool (dx_idx);
        switch (fxc) {
          case MDT_FX_VOICE_UP: {
            const uint8_t tgt = (cur_v + 1) % MAX_VOICES;
            snprintf(tmp, sizeof(tmp), "%s%02u>%02u", fxn,
                     (unsigned)(cur_v + 1), (unsigned)(tgt + 1));
            break;
          }
          case MDT_FX_VOICE_DN: {
            const uint8_t tgt = (cur_v == 0) ? (MAX_VOICES - 1) : (cur_v - 1);
            snprintf(tmp, sizeof(tmp), "%s%02u>%02u", fxn,
                     (unsigned)(cur_v + 1), (unsigned)(tgt + 1));
            break;
          }
          case MDT_FX_VOICE_SEL: {
            const uint8_t tgt = constrain(fxp, 0, MAX_VOICES - 1);
            snprintf(tmp, sizeof(tmp), "%s%02u>%02u", fxn,
                     (unsigned)(cur_v + 1), (unsigned)(tgt + 1));
            break;
          }
          case MDT_FX_BANK_SEL:
            snprintf(tmp, sizeof(tmp), "%s%02u>%02u", fxn,
                     (unsigned)cur_b, (unsigned)fxp);
            break;
          case MDT_FX_POOL_SEL:
            snprintf(tmp, sizeof(tmp), "%s%02u>%02u", fxn,
                     (unsigned)cur_p, (unsigned)fxp);
            break;
          case MDT_FX_VOICE_CLR:
            snprintf(tmp, sizeof(tmp), "%s%02u", fxn, (unsigned)(cur_v + 1));
            break;
        }
      } else {
        snprintf(tmp, sizeof(tmp), "%s?", fxn);
      }
    }
    else if (fxm == 0) {
      snprintf(tmp, sizeof(tmp), "%s", fxn);
    }
    else if (fxc == MDT_FX_PAN) {
      if (fxp < 64)      snprintf(tmp, sizeof(tmp), "%s L%02u", fxn, (unsigned)(64u - fxp));
      else if (fxp > 64) snprintf(tmp, sizeof(tmp), "%s R%02u", fxn, (unsigned)(fxp - 64u));
      else               snprintf(tmp, sizeof(tmp), "%s CNTR", fxn);
    }
    else {
      snprintf(tmp, sizeof(tmp), "%s %u", fxn, (unsigned)fxp);
    }

    size_t n = strlen(tmp);
    if (n > MDT_INFO_W) n = MDT_INFO_W;
    memcpy(info_work, tmp, n);
  }
  else {
    size_t n = strlen(base_info);
    if (n > MDT_INFO_W) n = MDT_INFO_W;
    memcpy(info_work, base_info, n);
  }

  uint8_t pv_note = 0, pv_vel = 0;
  const bool row_preview = _mdt_row_insert_preview(row, &pv_note, &pv_vel);
  if (row_preview) {
    const char* pv_info = nullptr;
    if (ct == 0) {
#if NUM_DRUMS > 0
      pv_info = (pv_vel > 209) ? basename(drum_config[pv_vel - 210].name)
        : get_drum_name_from_note(pv_note);
#endif
    }
    else if ((ct == 2 || ct == 3) && pv_vel > 199) {
      pv_info = _mdt_chord_full_name((uint8_t)(pv_vel - 200));
    }
    else {
      pv_info = "INSERT?";
    }
    if (pv_info) {
      memset(info_work, ' ', MDT_INFO_W);
      size_t n = strlen(pv_info);
      if (n > MDT_INFO_W) n = MDT_INFO_W;
      memcpy(info_work, pv_info, n);
    }
  }

  const bool row_is_empty = _mdt_step_is_empty(ct, note, vel);

  uint16_t fg, bg;
  if (is_cursor_row) { fg = COLOR_SYSTEXT; bg = DX_DARKCYAN; }
  else if (beyond_len) { fg = GREY3; bg = COLOR_BACKGROUND; }
  else if (row_is_empty && fxc == MDT_FX_NONE) { fg = GREY3; bg = COLOR_BACKGROUND; }
  else { fg = GREY1; bg = COLOR_BACKGROUND; }
  if (row_preview) fg = GREY1;

  checkWebRemote();
  display.fillRect(MDT_INFO_X - MDT_CELL_PAD, MDT_CELL_TOP(y),
    MDT_INFO_W * 6 + MDT_CELL_PAD * 2, MDT_CELL_H, bg);
  display.setTextSize(1);
  display.setTextColor(fg, bg);
  display.setCursor(MDT_INFO_X, y);
  display.print(info_work);
}

static FLASHMEM void _mdt_draw_single_cell(uint8_t row, uint8_t col,
  uint8_t cursor_row, uint8_t cursor_col,
  bool edit_state, bool is_playhead)
{
  if (col == MDT_COL_PATTERN) return;

  const uint8_t y = mdt_row_display_y(row);
  if (y == 0xFF) return;
  const bool is_cursor = (row == cursor_row);
  const bool beyond_len = (int16_t)row > (15 - seq.pattern_len_dec);

  const uint8_t note = seq.note_data[seq.active_pattern][row];
  const uint8_t vel = seq.vel[seq.active_pattern][row];
  const uint8_t fx_ins = seq.fx_inst[seq.active_pattern][row];
  const uint8_t fx_cmd = seq.fx_cmd[seq.active_pattern][row];
  const uint8_t fx_val = seq.fx_val[seq.active_pattern][row];

  const uint8_t ct = seq.content_type[seq.active_pattern];
  const bool is_chordy = (ct == 2 || ct == 3);

  const bool row_is_empty = _mdt_step_is_empty(ct, note, vel);

  char buf[4];
  uint16_t x = 0;
  uint8_t cw = MDT_CELL_W;
  uint16_t content_fg = COLOR_SYSTEXT;

  uint8_t pv_note = 0, pv_vel = 0;
  const bool row_preview = _mdt_row_insert_preview(row, &pv_note, &pv_vel);
  bool cell_preview = false;

  switch (col) {
  case MDT_COL_NOTE: {
    if (row_preview) {
      _mdt_fmt_note(buf, pv_note);
      content_fg = GREY2;
      cell_preview = true;
    }
    else if (mdt_step_is_control(note, fx_cmd)) {
      buf[0] = '>'; buf[1] = 'F'; buf[2] = 'X'; buf[3] = 0;
      content_fg = COLOR_ARP;
    }
    else {
      _mdt_fmt_note(buf, note);
      if (note == 0)                  content_fg = GREY3;
      else if (note == 130)           content_fg = GREEN;
      else if (ct == 0 && vel > 209)  content_fg = COLOR_PITCHSMP;
      else                            content_fg = _mdt_type_color();
    }
    x = MDT_NOTE_X; cw = MDT_NOTE_W;
    break;
  }

  case MDT_COL_VEL:
    if (row_preview) {
      if (ct == 0 && pv_vel > 209)        snprintf(buf, sizeof(buf), "P%02d", (int)(pv_vel - 210));
      else if (is_chordy && pv_vel > 199) _mdt_chord_name3(buf, (uint8_t)(pv_vel - 200));
      else                                snprintf(buf, sizeof(buf), "%03d", (int)pv_vel);
      content_fg = GREY2;
      cell_preview = true;
    }
    else if (is_chordy && note == 0) {
      buf[0] = '-'; buf[1] = '-'; buf[2] = '-'; buf[3] = 0;
      content_fg = GREY3;
    }
    else if (vel == 0) {
      snprintf(buf, sizeof(buf), "%03d", 0);
      content_fg = GREY3;
    }
    else if (is_chordy && vel > 199) {
      _mdt_chord_name3(buf, (uint8_t)(vel - 200));
      content_fg = _mdt_chord_color((uint8_t)(vel - 200));
    }
    else if (ct == 0 && vel > 209) {
      snprintf(buf, sizeof(buf), "P%02d", (int)(vel - 210));
      content_fg = COLOR_PITCHSMP;
    }
    else if (row_is_empty) {
      snprintf(buf, sizeof(buf), "%03d", (int)vel);
      content_fg = GREY3;
    }
    else {
      snprintf(buf, sizeof(buf), "%03d", (int)vel);
      content_fg = GREEN;
    }
    x = MDT_VEL_X;
    break;

  case MDT_COL_INS:
    if (fx_ins == 0xFF) { buf[0] = '-'; buf[1] = '-'; buf[2] = '-'; buf[3] = 0; }
    else { snprintf(buf, sizeof(buf), "%03d", (int)fx_ins); }
    content_fg = (fx_ins == 0xFF) ? GREY3 : COLOR_ARP;
    x = MDT_INS_X;
    break;

  case MDT_COL_CMD:
    snprintf(buf, sizeof(buf), "%03d", (int)fx_cmd);
    content_fg = (fx_cmd == MDT_FX_NONE) ? GREY3 : COLOR_CHORDS;
    x = MDT_CMD_X;
    break;

  case MDT_COL_PAR:
    if (fx_cmd == MDT_FX_VOICE_SEL)
      snprintf(buf, sizeof(buf), "%03d", (int)fx_val + 1);
    else
      snprintf(buf, sizeof(buf), "%03d", (int)fx_val);
    content_fg = (fx_cmd == MDT_FX_NONE) ? GREY3 : COLOR_PITCHSMP;
    x = MDT_PAR_X;
    break;

  default: return;
  }

  _mdt_draw_cell(x, y, buf, content_fg, col,
    is_playhead, is_cursor, cursor_col, edit_state, beyond_len,
    cw, cell_preview);
}

static void _mdt_draw_row(uint8_t row, uint8_t cursor_row,
  uint8_t cursor_col, bool edit_state)
{
  if (mdt_row_display_y(row) == 0xFF) return;

  const bool is_playhead = seq.running && (row == seq.step);
  const bool is_cursor_row = (row == cursor_row);
  const bool is_cursor_info = (row == cursor_row) && (cursor_col != MDT_COL_PATTERN);

  _mdt_draw_st_cell(row, is_playhead, is_cursor_row);
  _mdt_draw_single_cell(row, MDT_COL_NOTE, cursor_row, cursor_col, edit_state, is_playhead);
  _mdt_draw_single_cell(row, MDT_COL_VEL, cursor_row, cursor_col, edit_state, is_playhead);
  _mdt_draw_single_cell(row, MDT_COL_INS, cursor_row, cursor_col, edit_state, is_playhead);
  _mdt_draw_single_cell(row, MDT_COL_CMD, cursor_row, cursor_col, edit_state, is_playhead);
  _mdt_draw_single_cell(row, MDT_COL_PAR, cursor_row, cursor_col, edit_state, is_playhead);
  _mdt_draw_info_cell(row, is_playhead, is_cursor_info);
}

enum MdtCellId : uint16_t {
  MDT_CELL_ST = 0x8000,
  MDT_CELL_INFO = 0x8001,
};

static FLASHMEM void _mdt_draw_one_cell(uint8_t row, uint16_t cell_id,
  uint8_t cursor_row, uint8_t cursor_col,
  bool edit_state)
{
  if (mdt_row_display_y(row) == 0xFF) return;
  const bool is_playhead = seq.running && (row == seq.step);

  if (cell_id == MDT_CELL_ST) {
    _mdt_draw_st_cell(row, is_playhead, row == cursor_row);
  }
  else if (cell_id == MDT_CELL_INFO) {
    _mdt_draw_info_cell(row, is_playhead,
      (row == cursor_row) && (cursor_col != MDT_COL_PATTERN));
  }
  else {
    _mdt_draw_single_cell(row, (uint8_t)cell_id,
      cursor_row, cursor_col, edit_state, is_playhead);
  }
}

static FLASHMEM void _mdt_draw_all_rows(uint8_t cursor_row, uint8_t cursor_col, bool edit_state)
{
  checkWebRemote();
  for (uint8_t r = 0; r < MDT_ROWS; r++)
    _mdt_draw_row(r, cursor_row, cursor_col, edit_state);
}

static FLASHMEM void _mdt_redraw_left_rows(void)
{
  for (uint8_t r = 0; r < MDT_ROWS; r++) {
    const uint8_t y = mdt_row_display_y(r);
    if (y == 0xFF) continue;
    checkWebRemote();
    display.fillRect(MDT_ST_X - MDT_CELL_PAD, MDT_CELL_TOP(y),
      MDT_INFO_X + MDT_INFO_W * 6 + MDT_CELL_PAD - (MDT_ST_X - MDT_CELL_PAD),
      MDT_CELL_H, COLOR_BACKGROUND);
  }
  _mdt_draw_all_rows(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
}

static FLASHMEM void _mdt_status_cache_reset(void)
{
  mdt_status_force_full = true;
}

// ===========================================================================
//  STATUS BAR
// ===========================================================================
#define MDT_ST_Y1   (MDT_STATUS_Y + 2)
#define MDT_ST_Y2   (MDT_STATUS_Y + 13)

struct MdtStField {
  uint16_t x;
  uint16_t w;
  uint8_t  y;
  bool     right;
  bool     valid;
  uint16_t color;
  char     last[24];
};

enum { MDT_ST_SUM = 0, MDT_ST_KEY, MDT_ST_TRN, MDT_ST_A, MDT_ST_B, MDT_ST_FLAG, MDT_ST_COUNT };

static MdtStField mdt_st[MDT_ST_COUNT] = {
  {   1, 108, MDT_ST_Y1, false, false, 0, "" },
  { 115,  90, MDT_ST_Y1, false, false, 0, "" },
  { 208, 111, MDT_ST_Y1, true,  false, 0, "" },

  {   1, 108, MDT_ST_Y2, false, false, 0, "" },
  { 115, 174, MDT_ST_Y2, false, false, 0, "" },
  { 291,  28, MDT_ST_Y2, true,  false, 0, "" },
};

#define MDT_ST_BAR_X  (115 + 2)
#define MDT_ST_BAR_W  100
static int16_t  mdt_st_bar_last = -1;

static uint32_t mdt_st_sig = 0;
static bool     mdt_st_sig_valid = false;
static char     mdt_st_sum_txt[24];
static char     mdt_st_key_txt[24];
static uint16_t mdt_st_key_col = 0;

static uint8_t  mdt_st_use_pat = 0xFF;
static char     mdt_st_use_a[24];
static char     mdt_st_use_b[24];

// Value renderer mode (voice / bank / pool)
enum mdt_vvm_mode : uint8_t {
  MDT_VVM_VOICE = 0,
  MDT_VVM_BANK  = 1,
  MDT_VVM_POOL  = 2,
};

static uint8_t mdt_vcv_last_pool = 0xFF;
static uint8_t mdt_vcv_last_bank = 0xFF;
static uint8_t mdt_vcv_last_curv = 0xFF;
static uint8_t mdt_vcv_last_tgtv = 0xFF;
static bool    mdt_vcv_last_show = false;
static uint8_t mdt_vcv_last_mode = 0xFF;

// Which renderer last wrote field B?
//   false → _mdt_st_set() (single-colour text) owns it, f.last is valid.
//   true  → _mdt_st_set_voice_value() (chips) owns it, f.last is not usable.
static bool    mdt_st_b_voice_drawn = false;

static FLASHMEM void _mdt_st_invalidate(void)
{
  for (uint8_t i = 0; i < MDT_ST_COUNT; i++) mdt_st[i].valid = false;
  mdt_st_bar_last = -1;
  mdt_st_sig_valid = false;
  mdt_st_use_pat = 0xFF;
}

static FLASHMEM void _mdt_st_set(uint8_t id, const char* text, uint16_t color)
{
  MdtStField& f = mdt_st[id];

  const bool voice_owns_b = (id == MDT_ST_B) && mdt_st_b_voice_drawn;

  // Skip only if the text renderer wrote last AND the text/colour are
  // unchanged. If a voice command drew field B, f.last is stale and the
  // text renderer must always repaint.
  if (f.valid && !voice_owns_b &&
      f.color == color &&
      strncmp(f.last, text, sizeof(f.last)) == 0) return;

  if (id == MDT_ST_B) mdt_st_b_voice_drawn = false;

  strncpy(f.last, text, sizeof(f.last) - 1);
  f.last[sizeof(f.last) - 1] = '\0';
  f.color = color;
  f.valid = true;

  checkWebRemote();
  display.fillRect(f.x, f.y, f.w, 8, COLOR_BACKGROUND);
  display.console = false;
  if (f.last[0] == '\0') return;

  char buf[24];
  const uint8_t maxc = f.w / 6;
  strncpy(buf, f.last, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  if (strlen(buf) > maxc) buf[maxc] = '\0';

  const uint16_t x = f.right ? (uint16_t)(f.x + f.w - strlen(buf) * 6) : f.x;
  display.setTextSize(1);
  display.setTextColor(color, COLOR_BACKGROUND);
  display.setCursor(x, f.y);
  display.print(buf);
}

static FLASHMEM void _mdt_st_set_voice_value(uint8_t id,
                                             uint8_t pool,  uint8_t bank,
                                             uint8_t cur_val, uint8_t tgt_val,
                                             uint8_t mode,
                                             bool    show_target)
{
  MdtStField& f = mdt_st[id];

  const bool voice_owns_b = (id == MDT_ST_B) && mdt_st_b_voice_drawn;

  const bool same = f.valid && voice_owns_b &&
      mdt_vcv_last_pool == pool &&
      mdt_vcv_last_bank == bank &&
      mdt_vcv_last_curv == cur_val &&
      mdt_vcv_last_tgtv == tgt_val &&
      mdt_vcv_last_show == show_target &&
      mdt_vcv_last_mode == mode;

  if (same) return;

  mdt_vcv_last_pool = pool;
  mdt_vcv_last_bank = bank;
  mdt_vcv_last_curv = cur_val;
  mdt_vcv_last_tgtv = tgt_val;
  mdt_vcv_last_show = show_target;
  mdt_vcv_last_mode = mode;

  if (id == MDT_ST_B) mdt_st_b_voice_drawn = true;
  f.valid = true;

  // Poison f.last so the text renderer's strncmp() is guaranteed to
  // mismatch on the next call — even when that call's text is empty.
  f.last[0] = '\x01';
  f.last[1] = '\0';

  const char* cn = nullptr;
  const char* tn = nullptr;
  if (mode == MDT_VVM_VOICE) {
    cn = _mdt_voice_name_cur(pool, bank, cur_val);
    if (show_target) tn = _mdt_voice_name_tgt(pool, bank, tgt_val);
  }
  else if (mode == MDT_VVM_BANK) {
    cn = _mdt_bank_name_cur(pool, cur_val);
    if (show_target) tn = _mdt_bank_name_tgt(pool, tgt_val);
  }

  checkWebRemote();
  display.fillRect(f.x, f.y, f.w, 8, COLOR_BACKGROUND);
  display.console = false;

  display.setTextSize(1);
  const uint16_t y = f.y;
  uint16_t       x = f.x;

  auto displayNum = [&](uint8_t v) -> uint8_t {
    return (mode == MDT_VVM_VOICE) ? (uint8_t)(v + 1) : v;
  };

  auto printNum = [&](uint8_t v) {
    checkWebRemote();
    display.fillRect(x, y, 2 * CHAR_width_small, 8, COLOR_SYSTEXT);
    display.console = false;

    display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
    display.setCursor(x, y);
    char nb[4];
    snprintf(nb, sizeof(nb), "%02u", (unsigned)displayNum(v));
    display.print(nb);
    x += 2 * CHAR_width_small;
  };

  auto printSep = [&](char c) {
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.setCursor(x, y);
    char sb[2] = { c, 0 };
    display.print(sb);
    x += CHAR_width_small;
  };

  auto printName = [&](const char* n) {
    char nb[11];
    if (n) { strncpy(nb, n, 10); nb[10] = '\0'; } else { nb[0] = '\0'; }

    checkWebRemote();
    display.fillRect(x, y, 10 * CHAR_width_small, 8, DX_DARKCYAN);
    display.console = false;

    if (nb[0] != '\0') {
      display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
      display.setCursor(x, y);
      display.print(nb);
    }

    x += 10 * CHAR_width_small;
  };

  printNum(cur_val);
  if (cn) {
    printSep(' ');
    printName(cn);
  }

  if (show_target) {
    printSep(' ');
    printSep('>');
    printSep(' ');
    printNum(tgt_val);
    if (cn) {
      printSep(' ');
      printName(tn);
    }
  }
}

static FLASHMEM void _mdt_st_bar(int16_t value)
{
  if (value == mdt_st_bar_last) return;
  checkWebRemote();
  if (value < 0) {
    if (mdt_st_bar_last >= 0)
      display.fillRect(MDT_ST_BAR_X, MDT_ST_Y2, MDT_ST_BAR_W, 8, COLOR_BACKGROUND);
    mdt_st_bar_last = -1;
    return;
  }
  const uint8_t fill = (uint8_t)((uint16_t)value * MDT_ST_BAR_W / 127);
  display.fillRect(MDT_ST_BAR_X, MDT_ST_Y2 + 1, fill, 6, COLOR_PITCHSMP);
  display.fillRect(MDT_ST_BAR_X + fill, MDT_ST_Y2 + 1, MDT_ST_BAR_W - fill, 6, GREY4);
  mdt_st_bar_last = value;
}

static FLASHMEM const char* _mdt_st_chord_name(uint8_t t)
{
  static const char* const names[6] = { "MAJ", "MIN", "7", "AUG", "DIM", "MAJ7" };
  return (t < 6) ? names[t] : "?";
}

static FLASHMEM void _mdt_st_note_name(char* out, size_t n, uint8_t note)
{
  char nb[4];
  _mdt_fmt_note(nb, note);
  snprintf(out, n, "%s", nb);
}

static FLASHMEM const char* _mdt_fx_long_name(uint8_t cmd)
{
  switch (cmd) {
  case MDT_FX_NONE:      return "NO EFFECT";
  case MDT_FX_REV:       return "REVERB SEND";
  case MDT_FX_DLY1:      return "DELAY 1 SEND";
  case MDT_FX_DLY2:      return "DELAY 2 SEND";
  case MDT_FX_CHR:       return "CHORUS SEND";
  case MDT_FX_PAN:       return "PANORAMA";
  case MDT_FX_VOL:       return "VOLUME";
  case MDT_FX_TRN:       return "TRANSPOSE";
  case MDT_FX_GAT:       return "GATE LENGTH";
  case MDT_FX_HLD:       return "HOLD";
  case MDT_FX_ARP_STOP:  return "ARP RUN/STOP";
  case MDT_FX_ARP_STYLE: return "ARP STYLE";
  case MDT_FX_ARP_SPEED: return "ARP SPEED";
  case MDT_FX_ARP_LEN:   return "ARP LENGTH";
  case MDT_FX_ARP_MAXNT: return "ARP MAX NOTES";
  case MDT_FX_ARP_OCT:   return "ARP OCTAVE";
  case MDT_FX_ARP_VEL:   return "ARP VELOCITY";
  case MDT_FX_ARP_EUCL:  return "ARP EUCLID";
  case MDT_FX_ARP_EUOFF: return "ARP EUCL.OFFSET";
  case MDT_FX_ARP_RESET: return "ARP RESET";
  case MDT_FX_VOICE_UP:  return "VOICE +";
  case MDT_FX_VOICE_DN:  return "VOICE -";
  case MDT_FX_VOICE_SEL: return "SELECT VOICE";
  case MDT_FX_BANK_SEL:  return "SELECT BANK";
  case MDT_FX_POOL_SEL:  return "SELECT POOL";
  case MDT_FX_VOICE_CLR: return "CLEAR OVERRIDE";
  default:               return "UNKNOWN";
  }
}

static FLASHMEM void _mdt_fx_value_text(char* out, size_t n, uint8_t cmd, uint8_t v)
{
  switch (cmd) {
  case MDT_FX_NONE: out[0] = '\0'; return;
  case MDT_FX_REV: case MDT_FX_DLY1: case MDT_FX_DLY2: case MDT_FX_CHR:
  case MDT_FX_VOL:
    snprintf(out, n, "= %u%%", (unsigned)v); return;
  case MDT_FX_PAN:
    if (v == 64)     snprintf(out, n, "= CENTRE");
    else if (v < 64) snprintf(out, n, "= LEFT %u%%", (unsigned)((64 - v) * 100 / 64));
    else             snprintf(out, n, "= RIGHT %u%%", (unsigned)((v - 64) * 100 / 63));
    return;
  case MDT_FX_TRN: {
    const int16_t st = (int16_t)v - 48;
    if (st == 0) snprintf(out, n, "= NO TRANSPOSE");
    else         snprintf(out, n, "= %+d SEMITONES", st);
    return;
  }
  case MDT_FX_GAT:
    snprintf(out, n, "= %u TICKS", (unsigned)v); return;
  case MDT_FX_HLD: case MDT_FX_ARP_EUCL:
    snprintf(out, n, v ? "= ON" : "= OFF"); return;
  case MDT_FX_ARP_STOP:
    snprintf(out, n, v ? "= STOP" : "= RUN"); return;
  case MDT_FX_ARP_STYLE: {
    static const char* const s[4] = { "UP", "DOWN", "UP+DOWN", "RANDOM" };
    snprintf(out, n, "= %s", s[v & 3]); return;
  }
  case MDT_FX_ARP_SPEED: {
    static const char* const s[4] = { "1/16", "1/8", "1/32", "1/64" };
    snprintf(out, n, "= %s", s[v & 3]); return;
  }
  case MDT_FX_ARP_LEN:
    snprintf(out, n, "= %u STEPS", (unsigned)v); return;
  case MDT_FX_ARP_MAXNT:
    snprintf(out, n, "= %u NOTES", (unsigned)v); return;
  case MDT_FX_ARP_OCT: {
    const int8_t o = (int8_t)v - 2;
    if (o == 0) snprintf(out, n, "= NO SHIFT");
    else        snprintf(out, n, "= %+d OCTAVE", o);
    return;
  }
  case MDT_FX_ARP_VEL:
    snprintf(out, n, "= %u", (unsigned)v); return;
  case MDT_FX_ARP_EUOFF:
    snprintf(out, n, "= +%u STEPS", (unsigned)v); return;
  case MDT_FX_ARP_RESET:
    snprintf(out, n, v ? "= RESET HERE" : "= IDLE"); return;
  case MDT_FX_VOICE_UP:  snprintf(out, n, "= next voice");     return;
  case MDT_FX_VOICE_DN:  snprintf(out, n, "= previous voice"); return;
  case MDT_FX_VOICE_SEL: snprintf(out, n, "= voice %u", (unsigned)(v + 1)); return;
  case MDT_FX_BANK_SEL:  snprintf(out, n, "= bank %u",  (unsigned)v); return;
  case MDT_FX_POOL_SEL:  snprintf(out, n, "= pool %u",  (unsigned)v); return;
  case MDT_FX_VOICE_CLR: snprintf(out, n, "= default voice");  return;
  default:
    snprintf(out, n, "= %u", (unsigned)v); return;
  }
}

static FLASHMEM uint32_t _mdt_st_pattern_sig(void)
{
  const uint8_t pat = seq.active_pattern;
  uint32_t h = 2166136261u;
  for (uint8_t s = 0; s < 16; s++) {
    h = (h ^ seq.note_data[pat][s]) * 16777619u;
    h = (h ^ seq.vel[pat][s]) * 16777619u;
    h = (h ^ seq.fx_cmd[pat][s]) * 16777619u;
  }
  h = (h ^ seq.content_type[pat]) * 16777619u;
  h = (h ^ seq.pattern_len_dec) * 16777619u;
  h = (h ^ pat) * 16777619u;
  return h;
}

static FLASHMEM void _mdt_st_compute_pattern_info(void)
{
  const uint8_t pat = seq.active_pattern;
  const uint8_t ct = seq.content_type[pat];
  const uint8_t len = _mdt_pattern_len();

  uint8_t notes = 0, fx = 0;
  for (uint8_t s = 0; s < len; s++) {
    const uint8_t n = seq.note_data[pat][s];
    if (n > 0 && n != 130) notes++;
    if (seq.fx_cmd[pat][s] != MDT_FX_NONE) fx++;
  }
  snprintf(mdt_st_sum_txt, sizeof(mdt_st_sum_txt), "LEN %02u NT %02u FX %02u",
    (unsigned)len, (unsigned)notes, (unsigned)fx);

  mdt_st_key_col = GREY2;
  if (ct == 0) {
#if NUM_DRUMS > 0
    uint8_t u[16], c[16];
    const uint8_t cnt = _mdt_drum_count_unique(u, c, 16);
    if (cnt == 0) snprintf(mdt_st_key_txt, sizeof(mdt_st_key_txt), "NO DRUMS");
    else snprintf(mdt_st_key_txt, sizeof(mdt_st_key_txt), "%u DRUM%s", (unsigned)cnt, cnt == 1 ? "" : "S");
    mdt_st_key_col = COLOR_DRUMS;
#else
    mdt_st_key_txt[0] = '\0';
#endif
    return;
  }
  if (ct == 3) {
    snprintf(mdt_st_key_txt, sizeof(mdt_st_key_txt), "SLICES");
    mdt_st_key_col = COLOR_ARP;
    return;
  }

  uint8_t hist[12] = { 0 }, tonic[12] = { 0 };
  uint8_t total = 0;
  int8_t  first = -1, last = -1;
  for (uint8_t s = 0; s < len; s++) {
    const uint8_t n = seq.note_data[pat][s];
    if (n == 0 || n > 127) continue;
    const uint8_t pc = n % 12;
    hist[pc]++; total++;
    if ((s % 4) == 0) tonic[pc] += 2;
    if (first < 0) first = s;
    last = s;
    if (ct == 2) {
      const uint8_t v = seq.vel[pat][s];
      if (v >= 200 && v <= 205) {
        uint8_t cnt = seq.chord_key_ammount;
        if (cnt == 0) cnt = 4;
        if (cnt > 6)  cnt = 6;
        for (uint8_t k = 1; k < cnt; k++) {
          hist[(n + seq.arps[v - 200][k]) % 12]++; total++;
        }
      }
    }
  }
  if (total == 0) {
    snprintf(mdt_st_key_txt, sizeof(mdt_st_key_txt), "KEY --");
    return;
  }
  tonic[seq.note_data[pat][first] % 12] += 3;
  tonic[seq.note_data[pat][last] % 12] += 3;

  uint8_t best_root = 0, best_scale = 0, best_hits = 0;
  int16_t best_score = -1;
  for (uint8_t sc = 0; sc <= 8; sc++) {
    const uint16_t mask = mdt_scales[sc].mask;
    for (uint8_t r = 0; r < 12; r++) {
      uint8_t hits = 0;
      for (uint8_t pc = 0; pc < 12; pc++)
        if ((mask >> ((pc + 12 - r) % 12)) & 1) hits += hist[pc];
      const int16_t score = (int16_t)hits * 16 + tonic[r];
      if (score > best_score) {
        best_score = score; best_root = r; best_scale = sc; best_hits = hits;
      }
    }
  }
  const uint8_t off = total - best_hits;
  if (off == 0) {
    snprintf(mdt_st_key_txt, sizeof(mdt_st_key_txt), "KEY %s %s",
      noteNames[best_root], mdt_scales[best_scale].name);
    mdt_st_key_col = COLOR_PITCHSMP;
  }
  else {
    snprintf(mdt_st_key_txt, sizeof(mdt_st_key_txt), "%s %s +%u",
      noteNames[best_root], mdt_scales[best_scale].name, (unsigned)off);
    mdt_st_key_col = YELLOW;
  }
}

static FLASHMEM void _mdt_st_compute_usage(void)
{
  const uint8_t pat = seq.active_pattern;
  bool    in_chain[NUM_CHAINS];
  uint8_t chains = 0, first_chain = 0xFF;
  for (uint8_t c = 0; c < NUM_CHAINS; c++) {
    in_chain[c] = false;
    for (uint8_t k = 0; k < 16; k++)
      if (seq.chain[c][k] == pat) { in_chain[c] = true; break; }
    if (in_chain[c]) { chains++; if (first_chain == 0xFF) first_chain = c; }
  }

  if (chains == 0) {
    snprintf(mdt_st_use_a, sizeof(mdt_st_use_a), "NOT IN ANY CHAIN");
    mdt_st_use_b[0] = '\0';
    return;
  }
  if (chains == 1) snprintf(mdt_st_use_a, sizeof(mdt_st_use_a), "IN CHAIN %02u", (unsigned)first_chain);
  else             snprintf(mdt_st_use_a, sizeof(mdt_st_use_a), "IN CHAIN %02u +%u MORE",
    (unsigned)first_chain, (unsigned)(chains - 1));

  for (uint8_t s = 0; s < SONG_LENGTH; s++)
    for (uint8_t t = 0; t < NUM_SEQ_TRACKS; t++) {
      const uint8_t c = seq.song[t][s];
      if (c < NUM_CHAINS && in_chain[c]) {
        snprintf(mdt_st_use_b, sizeof(mdt_st_use_b), "SONG POS %02u TRK %u",
          (unsigned)(s + 1), (unsigned)(t + 1));
        return;
      }
    }
  snprintf(mdt_st_use_b, sizeof(mdt_st_use_b), "NOT IN SONG");
}

static FLASHMEM const char* _mdt_st_help_text(uint8_t cursor_col, bool edit_state)
{
  if (mdt_menu_state == MDT_MENU_MAIN)      return "R:SELECT P:OK";
  if (mdt_menu_state == MDT_MENU_QUANTIZE ||
    mdt_menu_state == MDT_MENU_GENERATE)  return mdt_form_edit ? "R:VALUE P:DONE" : "R:SELECT P:EDIT";
  if (mdt_menu_state != MDT_MENU_OFF)       return "R:VALUE P:OK";
  if (cursor_col == MDT_COL_PATTERN)        return "R:PATTERN L:COL";
  if (!edit_state)                          return "R:ROW P:EDIT L:COL";
  if (mdt_is_drum_pattern() && cursor_col == MDT_COL_NOTE) return "R:SAMPLE P:OK";
  return "R:VALUE P:OK L:COL";
}

static FLASHMEM void _mdt_draw_status(uint8_t cursor_row,
  uint8_t cursor_col,
  bool edit_state)
{
  if (mdt_is_vk_mode()) return;

  if (mdt_status_force_full) {
    mdt_status_force_full = false;
    checkWebRemote();
    display.fillRect(0, MDT_STATUS_Y, DISPLAY_WIDTH, MDT_STATUS_H, COLOR_BACKGROUND);
    display.console = false;
    _mdt_st_invalidate();
  }

  const uint8_t pat = seq.active_pattern;
  const uint8_t ct = seq.content_type[pat];
  const uint8_t len = _mdt_pattern_len();
  const uint8_t row = (cursor_row < MDT_ROWS) ? cursor_row : 0;
  char a[32], b[32];

  const uint32_t sig = _mdt_st_pattern_sig();
  if (!mdt_st_sig_valid || sig != mdt_st_sig) {
    mdt_st_sig = sig;
    mdt_st_sig_valid = true;
    _mdt_st_compute_pattern_info();
  }
#if defined OLED_DISPLAYS
  _mdt_st_set(MDT_ST_SUM, mdt_st_sum_txt, GREY2);
#else
  _mdt_st_set(MDT_ST_SUM, _mdt_st_help_text(cursor_col, edit_state), GREY1);
#endif
  _mdt_st_set(MDT_ST_KEY, mdt_st_key_txt, mdt_st_key_col);

  if (seq.running) {
    snprintf(a, sizeof(a), "%s %02u/%02u %d BPM", seq.recording ? "REC" : "PLAY",
      (unsigned)((seq.step < len ? seq.step : 0) + 1), (unsigned)len, seq.bpm);
    _mdt_st_set(MDT_ST_TRN, a, seq.recording ? RED : GREEN);
  }
  else {
    snprintf(a, sizeof(a), "STOP %d BPM", seq.bpm);
    _mdt_st_set(MDT_ST_TRN, a, GREY2);
  }

  const uint8_t note = seq.note_data[pat][row];
  const uint8_t vel = seq.vel[pat][row];
  const uint8_t fxCmd = seq.fx_cmd[pat][row];
  const uint8_t fxPar = seq.fx_val[pat][row];
  const uint8_t fxIns = seq.fx_inst[pat][row];
  uint16_t ca = GREY1, cb = GREY1;
  bool want_bar = false;
  a[0] = b[0] = '\0';

  switch (cursor_col) {

  case MDT_COL_PATTERN:
    if (mdt_st_use_pat != pat) { _mdt_st_compute_usage(); mdt_st_use_pat = pat; }
    strcpy(a, mdt_st_use_a);
    strcpy(b, mdt_st_use_b);
    ca = COLOR_CHORDS;
    break;

  case MDT_COL_CMD:
  case MDT_COL_PAR:
    if (fxCmd < MDT_FX_END) {
      if (mdt_cmd_is_voice(fxCmd)) {
        const uint8_t dx_idx = _mdt_voice_fx_target_instance(
            seq.selected_track, pat, row);

        if (dx_idx < NUM_DEXED) {
          const uint8_t cur_v = mdt_get_current_voice(dx_idx);
          const uint8_t cur_b = mdt_get_current_bank (dx_idx);
          const uint8_t cur_p = mdt_get_current_pool (dx_idx);

          uint8_t cur_disp    = 0;
          uint8_t tgt_disp    = 0;
          uint8_t mode        = MDT_VVM_VOICE;
          bool    show_target = false;

          switch (fxCmd) {
            case MDT_FX_VOICE_UP:
              cur_disp    = cur_v;
              tgt_disp    = (uint8_t)((cur_v + 1) % MAX_VOICES);
              mode        = MDT_VVM_VOICE;
              show_target = true;
              break;
            case MDT_FX_VOICE_DN:
              cur_disp    = cur_v;
              tgt_disp    = (cur_v == 0) ? (uint8_t)(MAX_VOICES - 1)
                                         : (uint8_t)(cur_v - 1);
              mode        = MDT_VVM_VOICE;
              show_target = true;
              break;
            case MDT_FX_VOICE_SEL:
              cur_disp    = cur_v;
              tgt_disp    = (uint8_t)constrain(fxPar, 0, MAX_VOICES - 1);
              mode        = MDT_VVM_VOICE;
              show_target = true;
              break;
            case MDT_FX_VOICE_CLR:
              cur_disp    = cur_v;
              tgt_disp    = cur_v;
              mode        = MDT_VVM_VOICE;
              show_target = false;
              break;
            case MDT_FX_BANK_SEL:
              cur_disp    = cur_b;
              tgt_disp    = fxPar;
              mode        = MDT_VVM_BANK;
              show_target = true;
              break;
            case MDT_FX_POOL_SEL:
              cur_disp    = cur_p;
              tgt_disp    = fxPar;
              mode        = MDT_VVM_POOL;
              show_target = true;
              break;
            default: break;
          }

          snprintf(a, sizeof(a), "%s %s",
                   mdt_fx_names[fxCmd], _mdt_fx_long_name(fxCmd));
          _mdt_st_set(MDT_ST_A, a, COLOR_ARP);

          _mdt_st_set_voice_value(MDT_ST_B, cur_p, cur_b,
                                  cur_disp, tgt_disp,
                                  mode, show_target);

          if (edit_state)                                   _mdt_st_set(MDT_ST_FLAG, "EDIT", RED);
          else if ((int16_t)row > 15 - seq.pattern_len_dec) _mdt_st_set(MDT_ST_FLAG, "LEN",  GREY3);
          else if (mdt_step_is_control(note, fxCmd))        _mdt_st_set(MDT_ST_FLAG, "FX",   COLOR_CHORDS);
          else                                              _mdt_st_set(MDT_ST_FLAG, "",     GREY3);
          return;
        }
        else {
          snprintf(a, sizeof(a), "%s %s",
                   mdt_fx_names[fxCmd], _mdt_fx_long_name(fxCmd));
          _mdt_st_set(MDT_ST_A, a, RED);
          _mdt_st_set(MDT_ST_B, "NO DX TARGET", RED);
        }
      }
      else {
        snprintf(a, sizeof(a), "%s %s",
                 mdt_fx_names[fxCmd], _mdt_fx_long_name(fxCmd));
        _mdt_fx_value_text(b, sizeof(b), fxCmd, fxPar);
        ca = COLOR_CHORDS;
        cb = COLOR_SYSTEXT;
      }
    }
    else {
      snprintf(a, sizeof(a), "CMD %u UNKNOWN", (unsigned)fxCmd);
      _mdt_st_set(MDT_ST_A, a, COLOR_CHORDS);
    }
    break;

  case MDT_COL_INS: {
    if (fxIns == 0xFF) snprintf(a, sizeof(a), "INS ---");
    else               snprintf(a, sizeof(a), "INS %03u", (unsigned)fxIns);
    _mdt_ins_label(b, sizeof(b), fxIns);
    if (fxIns == 0xFF) snprintf(b, sizeof(b), "TRACK DEFAULT");
    ca = COLOR_ARP;
    cb = COLOR_ARP;
    break;
  }

  case MDT_COL_VEL:
    if (note == 0) {
      snprintf(a, sizeof(a), "EMPTY STEP");
    }
    else if (note == 130) {
      snprintf(a, sizeof(a), "LATCH: NO VELOCITY");
    }
    else if (ct == 0 && vel > 209) {
      char nn[6]; _mdt_st_note_name(nn, sizeof(nn), note);
      snprintf(a, sizeof(a), "PITCHED SAMPLE");
      snprintf(b, sizeof(b), "PLAYS AT %s", nn);
      ca = COLOR_PITCHSMP;
    }
    else if (ct == 2 && vel >= 200 && vel <= 205) {
      snprintf(a, sizeof(a), "CHORD TYPE %s", _mdt_st_chord_name(vel - 200));
      ca = COLOR_CHORDS;
    }
    else {
      snprintf(a, sizeof(a), "VEL %03u  %3u%%", (unsigned)vel,
        (unsigned)((vel > 127 ? 127 : vel) * 100 / 127));
      want_bar = true;
    }
    break;

  case MDT_COL_NOTE:
  default:
    if (ct == 0) {
#if NUM_DRUMS > 0
      const uint8_t idx = (note == 0) ? 0xFF : _mdt_drum_index_for_step(note, vel);
      if (note == 0)                       snprintf(a, sizeof(a), "STEP: EMPTY");
      else if (idx == 0xFF)                snprintf(a, sizeof(a), "STEP: NOTE %u", (unsigned)note);
      else if (idx >= MDT_EMPTY_SLOT_IDX)  snprintf(a, sizeof(a), "STEP: EMPTY SLOT");
      else snprintf(a, sizeof(a), "STEP: %.14s", basename(drum_config[idx].name));

      if (activeSample > MDT_MAX_SAMPLE_IDX)       snprintf(b, sizeof(b), "SEL: (NONE)");
      else if (activeSample >= MDT_EMPTY_SLOT_IDX) snprintf(b, sizeof(b), "SEL: EMPTY SLOT");
      else if (drum_config[activeSample].midinote > 209) {
        char nn[6]; getNoteName(nn, mdt_pitch_buffer);
        snprintf(b, sizeof(b), "SEL: PIT %02u %s", (unsigned)activeSample, nn);
      }
      else {
        snprintf(b, sizeof(b), "SEL: %.12s", basename(drum_config[activeSample].name));
      }
      ca = COLOR_DRUMS;
      cb = edit_state ? COLOR_SYSTEXT : GREY1;
#endif
      break;
    }
    if (note == 0) {
      snprintf(a, sizeof(a), "%s", (fxCmd != MDT_FX_NONE) ? "FX STEP (NO NOTE)" : "EMPTY STEP");
      break;
    }
    if (note == 130) {
      int8_t src = -1;
      for (int8_t s = (int8_t)row - 1; s >= 0; s--) {
        const uint8_t n = seq.note_data[pat][s];
        if (n == 130) continue;
        if (n > 0) src = s;
        break;
      }
      snprintf(a, sizeof(a), "LATCH (HOLD)");
      if (src >= 0) {
        char nn[6]; _mdt_st_note_name(nn, sizeof(nn), seq.note_data[pat][src]);
        snprintf(b, sizeof(b), "HOLDS %s FROM %02d", nn, src + 1);
        cb = GREEN;
      }
      else {
        snprintf(b, sizeof(b), "NOTHING TO HOLD");
        cb = YELLOW;
      }
      break;
    }
    {
      char nn[6]; _mdt_st_note_name(nn, sizeof(nn), note);
      if (ct == 2 && vel >= 200 && vel <= 205) {
        const uint8_t t = vel - 200;
        snprintf(a, sizeof(a), "CHORD %s %s", nn, _mdt_st_chord_name(t));
        uint8_t cnt = seq.chord_key_ammount;
        if (cnt == 0) cnt = 4;
        if (cnt > 6)  cnt = 6;
        uint8_t p = 0;
        for (uint8_t k = 0; k < cnt && (size_t)p + 4 < sizeof(b); k++) {
          const char* nm = noteNames[(note + seq.arps[t][k]) % 12];
          p += snprintf(b + p, sizeof(b) - p, "%s%s", k ? " " : "", nm);
        }
        ca = COLOR_CHORDS;
      }
      else {
        snprintf(a, sizeof(a), "NOTE %s  MIDI %03u", nn, (unsigned)note);
        int8_t prv = -1;
        for (int8_t s = (int8_t)row - 1; s >= 0; s--) {
          const uint8_t n = seq.note_data[pat][s];
          if (n > 0 && n != 130) { prv = s; break; }
        }
        if (prv < 0) {
          snprintf(b, sizeof(b), "FIRST NOTE");
        }
        else {
          const int16_t d = (int16_t)note - seq.note_data[pat][prv];
          char pn[6]; _mdt_st_note_name(pn, sizeof(pn), seq.note_data[pat][prv]);
          if (d == 0) snprintf(b, sizeof(b), "SAME AS %s", pn);
          else        snprintf(b, sizeof(b), "%+d ST FROM %s", d, pn);
        }
        ca = COLOR_INSTR;
      }
    }
    break;
  }

  _mdt_st_set(MDT_ST_A, a, ca);
  if (want_bar) {
    if (mdt_st[MDT_ST_B].last[0] != '\0' || !mdt_st[MDT_ST_B].valid) {
      _mdt_st_set(MDT_ST_B, "", cb);
      mdt_st_bar_last = -1;
    }
    _mdt_st_bar(vel > 127 ? 127 : vel);
  }
  else {
    if (mdt_st_bar_last >= 0) {
      _mdt_st_bar(-1);
      mdt_st[MDT_ST_B].valid = false;
    }
    _mdt_st_set(MDT_ST_B, b, cb);
  }

  if (edit_state)                                   _mdt_st_set(MDT_ST_FLAG, "EDIT", RED);
  else if ((int16_t)row > 15 - seq.pattern_len_dec) _mdt_st_set(MDT_ST_FLAG, "LEN",  GREY3);
  else if (mdt_step_is_control(note, fxCmd))        _mdt_st_set(MDT_ST_FLAG, "FX",   COLOR_CHORDS);
  else                                              _mdt_st_set(MDT_ST_FLAG, "",     GREY3);
}

// ===========================================================================
//  TOUCH BUTTONS
// ===========================================================================
static FLASHMEM void _mdt_draw_type_button(void)
{
  TouchButton::drawButton(GRID.X[4], GRID.Y[5], "TYPE",
    _mdt_type_label(),
    TouchButton::BUTTON_ACTIVE);
}

static FLASHMEM void _mdt_draw_vk_latch_button(void)
{
  const bool can_latch = mdt_step_recording &&
    (seq.content_type[seq.active_pattern] != 0);
  TouchButton::drawButton(MDT_STEP_BTN_X, MDT_STEP_BTN_Y, "LATCH", "NOTE",
    can_latch ? TouchButton::BUTTON_NORMAL
    : TouchButton::BUTTON_INACTIVE);
}

static FLASHMEM void _mdt_draw_touch_buttons(void)
{
  if (mdt_is_vk_mode()) {
    _mdt_draw_vk_step_buttons();
    return;
  }

  TouchButton::drawButton(GRID.X[0], GRID.Y[5], "GO", "BACK", TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[1], GRID.Y[5], "JUMP", "SONG", TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[2], GRID.Y[5], "PAT", "-", TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[3], GRID.Y[5], "PAT", "+", TouchButton::BUTTON_NORMAL);

  _mdt_draw_type_button();

  TouchButton::drawButton(GRID.X[5], GRID.Y[5], "CLEAR", "STEP",
    TouchButton::BUTTON_RED);

  TouchButton::drawButton(MDT_MENU_BTN_X, MDT_MENU_BTN_Y, "MENU", "",
    TouchButton::BUTTON_NORMAL);
  TouchButton::drawVirtualKeyboardButton(MDT_VK_BTN_X, MDT_VK_BTN_Y);
  TouchButton::drawButton(MDT_STEP_BTN_X, MDT_STEP_BTN_Y, "STEP",
    mdt_step_recording ? "ON" : "REC",
    mdt_step_recording ? TouchButton::BUTTON_RED
    : TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(MDT_PLAY_BTN_X, MDT_PLAY_BTN_Y, "PLAY",
    seq.play_mode ? "LOOP" : "SONG",
    TouchButton::BUTTON_NORMAL);
}

static FLASHMEM void _mdt_draw_vk_adv_strip(void)
{
  const char* adv;
  if (seq.auto_advance_step == 0) adv = "ADV: KEEP";
  else if (seq.auto_advance_step == 1) adv = "ADV: AUTO";
  else                                 adv = "ADV: STOP";
  const ColorCombo c = TouchButton::getColors(TouchButton::BUTTON_NORMAL);
  checkWebRemote();
  display.fillRect(MDT_VK_ADV_X, MDT_VK_ADV_Y, MDT_VK_ADV_W, MDT_VK_ADV_H, c.bg);
  display.setTextSize(1);
  display.setTextColor(c.text, c.bg);
  display.setCursor(MDT_VK_ADV_X + 5, MDT_VK_ADV_Y + 6);
  display.print(adv);
}

static FLASHMEM void _mdt_draw_vk_velocity_bar(void)
{
  uint8_t v = ts.virtual_keyboard_velocity;
  if (v > 127) v = 127;

  checkWebRemote();
  display.fillRect(0, MDT_VK_ROWS_BOTTOM + 1, MDT_PREV_X,
    MDT_VK_TOP - MDT_VK_ROWS_BOTTOM - 1, COLOR_BACKGROUND);

  display.setTextSize(1);
  display.setTextColor(GREY2, COLOR_BACKGROUND);
  display.setCursor(MDT_VK_VELBAR_LX, MDT_VK_VELBAR_Y + 2);
  display.print(F("VEL"));

  display.drawRect(MDT_VK_VELBAR_X - 1, MDT_VK_VELBAR_Y - 1,
    MDT_VK_VELBAR_W + 2, MDT_VK_VELBAR_H + 2, GREY3);
  checkWebRemote();
  if (v > 0)
    display.fillRect(MDT_VK_VELBAR_X, MDT_VK_VELBAR_Y, v, MDT_VK_VELBAR_H,
      COLOR_PITCHSMP);

  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setCursor(MDT_VK_VELBAR_VX, MDT_VK_VELBAR_Y + 2);
  display.printf("%03d", (int)v);
}

static FLASHMEM void _mdt_draw_vk_step_buttons(void)
{
  const bool rec = mdt_step_recording;

  TouchButton::drawButton(MDT_MENU_BTN_X, MDT_MENU_BTN_Y,
    rec ? "RECORD" : "STEP", rec ? "ACTIVE" : "REC",
    rec ? TouchButton::BUTTON_RED : TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(MDT_VK_BTN_X, MDT_VK_BTN_Y, "BACK", "TO TRK",
    TouchButton::BUTTON_ACTIVE);
  _mdt_draw_vk_latch_button();
  TouchButton::drawButton(MDT_PLAY_BTN_X, MDT_PLAY_BTN_Y, "EMPTY", "STEP",
    rec ? TouchButton::BUTTON_NORMAL
    : TouchButton::BUTTON_INACTIVE);
  _mdt_draw_vk_adv_strip();
}

// ===========================================================================
//  PLAYHEAD UPDATE
// ===========================================================================
static void _mdt_update_playhead(void)
{
  checkWebRemote();
  uint8_t cur = seq.step;
  if (cur >= MDT_ROWS) cur = 0;

  if (seq.running) {
    if (mdt_prev_play_row != cur) {
      const uint8_t old = mdt_prev_play_row;
      mdt_prev_play_row = cur;

      if (old < MDT_ROWS && old != cur) {
        _mdt_draw_one_cell(old, MDT_CELL_ST,
          mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
      }
      _mdt_draw_one_cell(cur, MDT_CELL_ST,
        mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
    }
  }
  else if (mdt_prev_play_row != 0xFF) {
    const uint8_t old = mdt_prev_play_row;
    mdt_prev_play_row = 0xFF;
    if (old < MDT_ROWS) {
      _mdt_draw_one_cell(old, MDT_CELL_ST,
        mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
    }
  }
}

// ===========================================================================
//  TOUCH HANDLER
// ===========================================================================
static void _mdt_touch_handler(void)
{
  if (mdt_is_vk_mode() && mdt_menu_state == MDT_MENU_OFF) {
    handleTouchVirtualKeyboard();
  }

  if (numTouchPoints == 0) {
    TouchButton::isAreaPressed(0, 0, 1, 1);
    return;
  }

  if (mdt_menu_state != MDT_MENU_OFF) {
    if (!TouchButton::isInArea(MDT_MENU_X0, MDT_MENU_Y0, MDT_MENU_W, MDT_MENU_H)) {
      if (TouchButton::isAreaPressed(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
        _mdt_menu_back();
      }
    }
    else if (mdt_menu_state == MDT_MENU_MAIN) {
      const uint16_t row0 = MDT_MENU_ROWS_Y0;
      if (TouchButton::isAreaPressed(MDT_MENU_X0, row0, MDT_MENU_W,
        MDT_MI_COUNT * MDT_MENU_ROW_H)) {
        uint8_t idx = (ts.p.y - row0) / MDT_MENU_ROW_H;
        if (idx < MDT_MI_COUNT) {
          if (idx != mdt_menu_highlight) {
            _mdt_menu_draw_row(mdt_menu_highlight, false);
            mdt_menu_highlight = idx;
            _mdt_menu_draw_row(mdt_menu_highlight, true);
          }
          _mdt_menu_execute();
        }
      }
    }
    else {
      const bool confirm_only = (mdt_menu_state == MDT_MENU_CLEAR_PAT_CONFIRM ||
        mdt_menu_state == MDT_MENU_CLEAR_ALL_CONFIRM);
      const bool form = _mdt_is_form_state();
      if (form && TouchButton::isAreaPressed(MDT_MENU_X0, MDT_MENU_VALUE_Y - 1, MDT_MENU_W,
        (_mdt_form_nfields() + 1) * MDT_FORM_ROW_H)) {
        _mdt_form_tap((ts.p.y - (MDT_MENU_VALUE_Y - 1)) / MDT_FORM_ROW_H);
      }
      else if (TouchButton::isPressed(MDT_MENU_TB_X2, MDT_MENU_TB_OK_Y)) {
        if (form) _mdt_form_commit(); else _mdt_menu_execute();
      }
      else if (TouchButton::isPressed(MDT_MENU_TB_X1, MDT_MENU_TB_OK_Y)) {
        _mdt_menu_back();
      }
      else if (!confirm_only && TouchButton::isPressed(MDT_MENU_TB_X1, MDT_MENU_TB_ADJ_Y)) {
        if (form) _mdt_form_change_value(-1); else _mdt_menu_adjust(-1);
      }
      else if (!confirm_only && TouchButton::isPressed(MDT_MENU_TB_X2, MDT_MENU_TB_ADJ_Y)) {
        if (form) _mdt_form_change_value(+1); else _mdt_menu_adjust(+1);
      }
    }
    return;
  }

  if (mdt_is_vk_mode()) {
    if (ts.p.y < MDT_HDR_H &&
      ts.p.x >= MDT_TYPE_BOX_X &&
      ts.p.x < MDT_TYPE_BOX_X + MDT_TYPE_BOX_W) {
      if (TouchButton::isAreaPressed(MDT_TYPE_BOX_X, 0, MDT_TYPE_BOX_W, MDT_HDR_H))
        _mdt_cycle_pattern_type();
      return;
    }
    if (TouchButton::isPressed(MDT_VK_BTN_X, MDT_VK_BTN_Y)) {
      _mdt_close_virtual_keyboard(); return;
    }
    if (TouchButton::isPressed(MDT_MENU_BTN_X, MDT_MENU_BTN_Y)) {
      if (!seq.running) _mdt_set_step_recording(!mdt_step_recording);
      return;
    }
    if (TouchButton::isPressed(MDT_STEP_BTN_X, MDT_STEP_BTN_Y)) {
      _mdt_step_recording_insert_latch(); return;
    }
    if (TouchButton::isPressed(MDT_PLAY_BTN_X, MDT_PLAY_BTN_Y)) {
      _mdt_step_recording_insert_rest(); return;
    }
    if (TouchButton::isAreaPressed(MDT_VK_ADV_X, MDT_VK_ADV_Y,
      MDT_VK_ADV_W, MDT_VK_ADV_H)) {
      seq.auto_advance_step = (seq.auto_advance_step + 1) % 3;
      _mdt_draw_vk_adv_strip();
      return;
    }
    if (ts.p.y >= MDT_VK_VELBAR_TY0 && ts.p.y < MDT_VK_VELBAR_TY1 &&
      ts.p.x < MDT_PREV_X) {
      int16_t v = (int16_t)ts.p.x - MDT_VK_VELBAR_X;
      if (v < 1)   v = 1;
      if (v > 127) v = 127;
      if ((uint8_t)v != ts.virtual_keyboard_velocity) {
        ts.virtual_keyboard_velocity = (uint8_t)v;
        _mdt_draw_vk_velocity_bar();
        _mdt_draw_row(mdt_cursor_row, mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
      }
      return;
    }
    if (ts.p.y >= MDT_VK_TOP) return;
  }
  else {
    if (TouchButton::isPressed(GRID.X[0], GRID.Y[5])) { LCDML.BT_quit(); return; }
    if (TouchButton::isPressed(GRID.X[1], GRID.Y[5])) {
      LCDML.OTHER_jumpToFunc(UI_func_song); return;
    }
    if (TouchButton::isPressed(GRID.X[2], GRID.Y[5])) {
      uint8_t np = (seq.active_pattern == 0) ? NUM_SEQ_PATTERN - 1
        : seq.active_pattern - 1;
      if (np != seq.active_pattern) {
        seq.active_pattern = np;
        mdt_cursor_row = 0;
        mdt_prev_play_row = 0xFF;
        mdt_last_pattern = np;
        mdt_view_offset = 0;
        _mdt_preview_invalidate();
        _mdt_light_redraw_body();
        _mdt_draw_pattern_preview(mdt_cursor_row);
        _mdt_status_cache_reset();
        mdt_preview_dirty = false;
      }
      return;
    }
    if (TouchButton::isPressed(GRID.X[3], GRID.Y[5])) {
      uint8_t np = (seq.active_pattern + 1) % NUM_SEQ_PATTERN;
      if (np != seq.active_pattern) {
        seq.active_pattern = np;
        mdt_cursor_row = 0;
        mdt_prev_play_row = 0xFF;
        mdt_last_pattern = np;
        mdt_view_offset = 0;
        _mdt_preview_invalidate();
        _mdt_light_redraw_body();
        _mdt_draw_pattern_preview(mdt_cursor_row);
        _mdt_status_cache_reset();
        mdt_preview_dirty = false;
      }
      return;
    }

    if (TouchButton::isPressed(GRID.X[4], GRID.Y[5])) {
      _mdt_cycle_pattern_type();
      return;
    }

    if (TouchButton::isPressed(GRID.X[5], GRID.Y[5])) {
      _mdt_action_clear_step();
      return;
    }

    if (TouchButton::isPressed(MDT_MENU_BTN_X, MDT_MENU_BTN_Y)) {
      _mdt_open_menu(); return;
    }
    if (TouchButton::isPressed(MDT_VK_BTN_X, MDT_VK_BTN_Y)) {
      _mdt_open_virtual_keyboard(); return;
    }
    if (TouchButton::isPressed(MDT_STEP_BTN_X, MDT_STEP_BTN_Y)) {
      if (!seq.running) _mdt_set_step_recording(!mdt_step_recording);
      return;
    }
    if (TouchButton::isPressed(MDT_PLAY_BTN_X, MDT_PLAY_BTN_Y)) {
      seq.play_mode = !seq.play_mode;
      if (!seq.play_mode) seq.hunt_pattern = false;
      _mdt_draw_touch_buttons(); return;
    }

    if (ts.p.y < MDT_HDR_H &&
      ts.p.x >= MDT_TYPE_BOX_X &&
      ts.p.x < MDT_TYPE_BOX_X + MDT_TYPE_BOX_W) {
      if (TouchButton::isAreaPressed(MDT_TYPE_BOX_X, 0, MDT_TYPE_BOX_W, MDT_HDR_H))
        _mdt_cycle_pattern_type();
      return;
    }
  }

  if (ts.p.x < MDT_PREV_X &&
    ts.p.y >= MDT_DATA_Y &&
    ts.p.y < MDT_DATA_Y + (mdt_is_vk_mode() ? MDT_VK_VISIBLE_ROWS : MDT_ROWS) * MDT_ROW_H) {
    uint8_t rel = (ts.p.y - MDT_DATA_Y) / MDT_ROW_H;
    uint8_t row = mdt_is_vk_mode() ? (uint8_t)(mdt_view_offset + rel) : rel;
    if (row >= MDT_ROWS) row = MDT_ROWS - 1;
    if (row != mdt_cursor_row) {
      const uint8_t old_row = mdt_cursor_row;
      mdt_cursor_row = row;
      mdt_nopv_row = 0xFF;
      mdt_nopv_pat = 0xFF;
      _mdt_draw_row(old_row, mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
      _mdt_draw_row(mdt_cursor_row, mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
      _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
      _mdt_draw_pattern_preview(mdt_cursor_row);
      mdt_preview_dirty = false;
      mdt_prev_cursor_row = mdt_cursor_row;
      mdt_prev_cursor_col = mdt_cursor_col;
      mdt_prev_edit_state = mdt_edit_state;
      if (!mdt_edit_state) _mdt_preview_step(row);
    }
    return;
  }

  if (ts.p.y >= MDT_COLHDR_Y && ts.p.y < MDT_DATA_Y) {
    uint8_t col = MDT_COL_NOTE;
    if (ts.p.x >= MDT_ST_X && ts.p.x < MDT_NOTE_X) col = MDT_COL_PATTERN;
    else if (ts.p.x >= MDT_NOTE_X && ts.p.x < MDT_VEL_X)  col = MDT_COL_NOTE;
    else if (ts.p.x >= MDT_VEL_X && ts.p.x < MDT_INS_X)  col = MDT_COL_VEL;
    else if (ts.p.x >= MDT_INS_X && ts.p.x < MDT_CMD_X)  col = MDT_COL_INS;
    else if (ts.p.x >= MDT_CMD_X && ts.p.x < MDT_PAR_X)  col = MDT_COL_CMD;
    else if (ts.p.x >= MDT_PAR_X && ts.p.x < MDT_INFO_X) col = MDT_COL_PAR;
    if (col != mdt_cursor_col) {
      const uint8_t old_col = mdt_cursor_col;
      mdt_cursor_col = col;
      _mdt_draw_row(mdt_cursor_row, mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
      _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
      bool was_pat = (old_col == MDT_COL_PATTERN);
      bool is_pat = (mdt_cursor_col == MDT_COL_PATTERN);
      if (was_pat != is_pat) _mdt_draw_header();
      mdt_prev_cursor_row = mdt_cursor_row;
      mdt_prev_cursor_col = mdt_cursor_col;
      mdt_prev_edit_state = mdt_edit_state;
    }
    return;
  }
}

// ===========================================================================
//  FULL REDRAW
// ===========================================================================
static FLASHMEM void _mdt_full_redraw_body(void)
{
  if (mdt_is_vk_mode()) {
    _mdt_vk_redraw_top();
    return;
  }
  checkWebRemote();
  display.fillRect(MDT_PREV_X, 0,
    DISPLAY_WIDTH - MDT_PREV_X, MDT_STATUS_Y,
    COLOR_BACKGROUND);
  display.console = false;

  _mdt_draw_header();
  _mdt_draw_col_captions();
  _mdt_draw_all_rows(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  _mdt_draw_touch_buttons();
  mdt_view_changed = false;

  mdt_prev_cursor_row = mdt_cursor_row;
  mdt_prev_cursor_col = mdt_cursor_col;
  mdt_prev_edit_state = mdt_edit_state;
  mdt_cursor_moved = false;

  _mdt_update_oled_help();
}

static void _mdt_light_redraw_body(void)
{
  checkWebRemote();
  mdt_view_changed = false;
  _mdt_draw_header();
  _mdt_draw_all_rows(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
  _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);

  mdt_prev_cursor_row = mdt_cursor_row;
  mdt_prev_cursor_col = mdt_cursor_col;
  mdt_prev_edit_state = mdt_edit_state;
  mdt_cursor_moved = false;
}

// ===========================================================================
//  EDIT VALUE
// ===========================================================================
static FLASHMEM bool _mdt_edit_value(uint8_t row, uint8_t col, int16_t step)
{
  const uint8_t pat = seq.active_pattern;
  const bool is_drum = (seq.content_type[pat] == 0);
  const bool is_chordy = (seq.content_type[pat] == 2 ||
    seq.content_type[pat] == 3);

  switch (col) {
  case MDT_COL_NOTE: {
    const uint8_t old_note = seq.note_data[pat][row];
    const bool    was_empty = (old_note == 0);

    if (was_empty) {
      uint8_t in_note = 60, in_vel = is_chordy ? 200 : _mdt_vk_velocity_or(100);
      _mdt_insert_value(&in_note, &in_vel);
      seq.note_data[pat][row] = in_note;
      seq.vel[pat][row] = in_vel;
      mdt_buf_note = in_note;
      mdt_buf_vel = in_vel;
      mdt_note_preview_valid = true;
      return true;
    }

    int16_t v = (int16_t)old_note + step;

    if (v < 0)   v = 130;
    if (v > 130) v = 0;
    if (v > 108 && v < 130) v = (step > 0) ? 130 : 108;
    if (v > 0 && v < 12)    v = (step > 0) ? 12 : 0;

    if (!was_empty && v == old_note) return false;

    seq.note_data[pat][row] = (uint8_t)v;

    if (v == 0) {
      seq.vel[pat][row] = 0;
      mdt_nopv_row = row;
      mdt_nopv_pat = pat;
    }
    else if (seq.vel[pat][row] == 0) {
      seq.vel[pat][row] = is_chordy ? 200 : 100;
    }

    if (v != 0 && v != 130) {
      mdt_buf_note = (uint8_t)v;
      mdt_buf_vel = seq.vel[pat][row];
      mdt_note_preview_valid = true;
    }

    if (v == 0 && !seq.running) {
      _mdt_preview_release_all();
    }
    return true;
  }

  case MDT_COL_VEL: {
    const uint8_t old_vel = seq.vel[pat][row];
    int16_t v;

    if (is_chordy) {
      if (old_vel < 200) v = 200;
      else {
        v = (int16_t)old_vel + step;
        if (v < 200) v = 205;
        if (v > 205) v = 200;
      }
    }
    else if (is_drum && old_vel > 209) {
      const uint8_t old_note = seq.note_data[pat][row];
      int16_t n = (int16_t)old_note + ((step > 0) ? 1 : -1);
      if (n < 12)  n = 12;
      if (n > 108) n = 108;
      if (n == (int16_t)old_note) return false;
      seq.note_data[pat][row] = (uint8_t)n;
      mdt_pitch_buffer = (uint8_t)n;
      if ((uint8_t)(old_vel - 210) < MDT_EMPTY_SLOT_IDX)
        activeSample = (uint8_t)(old_vel - 210);
      mdt_drum_preview_valid = true;
      _mdt_preview_active_sample();
      _mdt_preview_invalidate();
      mdt_preview_dirty = true;
      return true;
    }
    else {
      v = (int16_t)old_vel + step;
      if (v < 0)   v = 0;
      if (v > 127) v = 127;
    }
    if (v == old_vel) return false;
    seq.vel[pat][row] = (uint8_t)v;

    const uint8_t cur_note = seq.note_data[pat][row];
    if (is_drum) {
      if (cur_note != 0 && v > 0 && v <= 127) mdt_buf_drum_vel = (uint8_t)v;
    }
    else if (cur_note != 0 && cur_note != 130) {
      mdt_buf_note = cur_note;
      mdt_buf_vel = (uint8_t)v;
      mdt_note_preview_valid = true;
    }
    return true;
  }

  case MDT_COL_INS: {
    uint8_t count = 0;
    const uint8_t* order = _mdt_ins_order(&count);
    int16_t idx = _mdt_ins_find(seq.fx_inst[pat][row]);
    if (idx < 0) idx = 0;
    int16_t new_idx = idx + step;
    if (new_idx < 0)      new_idx = 0;
    if (new_idx >= count) new_idx = count - 1;
    if (order[new_idx] == seq.fx_inst[pat][row]) return false;
    seq.fx_inst[pat][row] = order[new_idx];
    return true;
  }

  case MDT_COL_CMD: {
    // Walk through the command list, skipping commands that make no
    // sense for this pattern type (see mdt_cmd_allowed_for_type()).
    const uint8_t ct = seq.content_type[pat];
    const uint8_t old_cmd = seq.fx_cmd[pat][row];
    const int8_t  dir = (step > 0) ? 1 : -1;
    uint16_t      moves = (step > 0) ? step : -step;
    int16_t       v = old_cmd;

    while (moves--) {
      for (uint8_t guard = 0; guard < MDT_FX_END; guard++) {
        v += dir;
        if (v < 0)           v = MDT_FX_END - 1;
        if (v >= MDT_FX_END) v = 0;
        if (mdt_cmd_allowed_for_type(ct, (uint8_t)v)) break;
      }
    }

    if ((uint8_t)v == old_cmd) return false;
    seq.fx_cmd[pat][row] = (uint8_t)v;

    const uint8_t newmax = mdt_fx_param_max[(uint8_t)v];
    if (seq.fx_val[pat][row] > newmax) seq.fx_val[pat][row] = newmax;
    return true;
  }

  case MDT_COL_PAR: {
    const uint8_t cmd = seq.fx_cmd[pat][row];
    const uint8_t max = mdt_fx_param_max[cmd];
    if (max == 0) return false;

    int16_t v = (int16_t)seq.fx_val[pat][row] + step;
    if (v < 0)             v = 0;
    if (v > (int16_t)max)  v = max;
    if ((uint8_t)v == seq.fx_val[pat][row]) return false;
    seq.fx_val[pat][row] = (uint8_t)v;
    return true;
  }

  default: break;
  }
  return false;
}

static FLASHMEM bool _mdt_scroll_to_pattern(int8_t delta)
{
  int16_t np = (int16_t)seq.active_pattern + delta;
  if (np < 0)                np = NUM_SEQ_PATTERN - 1;
  if (np >= NUM_SEQ_PATTERN) np = 0;
  if (np == (int16_t)seq.active_pattern) return false;
  seq.active_pattern = (uint8_t)np;
  mdt_last_pattern = seq.active_pattern;
  mdt_prev_play_row = 0xFF;
  _mdt_preview_invalidate();
  mdt_light_redraw = true;
  mdt_preview_dirty = true;
  return true;
}

// ===========================================================================
//  MAIN UI
// ===========================================================================
FLASHMEM void UI_func_mdtracker_pattern_editor(uint8_t param)
{
  if (LCDML.FUNC_setup()) {
    registerTouchHandler(_mdt_touch_handler);
    LCDML.FUNC_setLoopInterval(MDT_LOOP_INTERVAL_MS);
    virtual_keyboard_smart_preselect_mode();

    display.fillScreen(COLOR_BACKGROUND);

    mdt_cursor_row = 0;
    mdt_cursor_col = MDT_COL_NOTE;
    mdt_prev_play_row = 0xFF;
    mdt_last_pattern = seq.active_pattern;
    mdt_last_track = seq.selected_track;
    mdt_edit_state = false;
    mdt_last_running = seq.running;
    mdt_view_offset = 0;
    mdt_view_changed = false;

    mdt_full_redraw = true;
    mdt_light_redraw = false;
    mdt_preview_dirty = true;
    mdt_force_preview = false;
    mdt_cursor_moved = false;
    mdt_prev_cursor_row = 0xFF;
    mdt_prev_cursor_col = 0xFF;
    mdt_prev_edit_state = false;

    mdt_menu_state = MDT_MENU_OFF;
    mdt_menu_highlight = 0;
    mdt_menu_value = 0;
    mdt_menu_value2 = 0;

    mdt_step_recording = seq.step_recording;
    mdt_menu_has_backup = false;

    _mdt_reset_previews();

    memset(mdt_last_oled_l, 0, sizeof(mdt_last_oled_l));
    memset(mdt_last_oled_r, 0, sizeof(mdt_last_oled_r));

    mdt_status_force_full = true;
    _mdt_preview_invalidate();

    seq.cycle_touch_element = 0;
    seq.edit_state = false;

    seq.note_in = 0;
    check_variable_samples_basespeed();

    _mdt_full_redraw_body();
    _mdt_draw_pattern_preview(mdt_cursor_row);
    _mdt_update_oled_help();

    mdt_full_redraw = false;
    mdt_light_redraw = false;
    mdt_preview_dirty = false;
  }

  if (LCDML.FUNC_loop()) {
    const EncoderEvents eR = getEncoderEvents(ENC_R);
    const EncoderEvents eL = getEncoderEvents(ENC_L);

    _mdt_periodic();

    if (mdt_menu_state != MDT_MENU_OFF) {
      if (eR.turned)       _mdt_menu_adjust((int8_t)eR.dir);
      else if (eR.pressed) _mdt_menu_execute();

      if (mdt_menu_state == MDT_MENU_OFF) {
        if (mdt_full_redraw)       _mdt_full_redraw_body();
        else if (mdt_light_redraw) _mdt_light_redraw_body();

        _mdt_draw_pattern_preview(mdt_cursor_row);

        mdt_full_redraw = false;
        mdt_light_redraw = false;
        mdt_preview_dirty = false;
        mdt_force_preview = false;
        return;
      }

      mdt_full_redraw = false;
      mdt_light_redraw = false;
      mdt_preview_dirty = false;
      mdt_force_preview = false;
      return;
    }

    if (eR.turned) {
      if (mdt_cursor_col == MDT_COL_PATTERN) {
        int16_t nv = (int16_t)seq.active_pattern + eR.dir;
        if (nv < 0)                nv = NUM_SEQ_PATTERN - 1;
        if (nv >= NUM_SEQ_PATTERN) nv = 0;
        if (nv != (int16_t)seq.active_pattern) {
          seq.active_pattern = (uint8_t)nv;
          mdt_cursor_row = 0;
          mdt_view_offset = 0;
          mdt_prev_play_row = 0xFF;
          mdt_last_pattern = seq.active_pattern;
          _mdt_preview_invalidate();
          mdt_light_redraw = true;
          mdt_preview_dirty = true;
        }
      }
      else if (!mdt_edit_state) {
        int16_t nv = (int16_t)mdt_cursor_row + eR.dir;
        if (nv < 0) {
          _mdt_scroll_to_pattern(-1);
          mdt_prev_cursor_row = mdt_cursor_row;
          mdt_cursor_row = MDT_ROWS - 1;
          mdt_cursor_moved = true;
          mdt_ensure_cursor_visible();
          mdt_nopv_row = 0xFF;
          mdt_nopv_pat = 0xFF;
          mdt_preview_dirty = true;
          if (!seq.running) _mdt_preview_step(mdt_cursor_row);
        }
        else if (nv >= MDT_ROWS) {
          _mdt_scroll_to_pattern(+1);
          mdt_prev_cursor_row = mdt_cursor_row;
          mdt_cursor_row = 0;
          mdt_cursor_moved = true;
          mdt_ensure_cursor_visible();
          mdt_nopv_row = 0xFF;
          mdt_nopv_pat = 0xFF;
          mdt_preview_dirty = true;
          if (!seq.running) _mdt_preview_step(mdt_cursor_row);
        }
        else {
          const uint8_t old_row = mdt_cursor_row;
          mdt_cursor_row = (uint8_t)nv;
          if (old_row != mdt_cursor_row) {
            mdt_prev_cursor_row = old_row;
            mdt_cursor_moved = true;
            mdt_ensure_cursor_visible();
            mdt_nopv_row = 0xFF;
            mdt_nopv_pat = 0xFF;
            mdt_preview_dirty = true;
            if (!seq.running) _mdt_preview_step(mdt_cursor_row);
          }
        }
      }
      else if (mdt_is_drum_pattern() && mdt_cursor_col == MDT_COL_NOTE) {
        _mdt_sync_activeSample_to_step();

        const uint8_t row = mdt_cursor_row;
        mdt_nopv_row = 0xFF;
        uint8_t pv_note, pv_vel;
        if (_mdt_row_insert_preview(row, &pv_note, &pv_vel)) {
          _mdt_commit_sample_to_step();
          _mdt_preview_active_sample();
          mdt_preview_dirty = true;
          _mdt_draw_row(row, row, mdt_cursor_col, mdt_edit_state);
          _mdt_draw_status(row, mdt_cursor_col, mdt_edit_state);
        }
        else {
          const uint8_t newSample = _mdt_cycle_activeSample(activeSample,
            (int8_t)eR.dir);
          if (newSample != activeSample) {
            activeSample = newSample;
            mdt_drum_preview_valid = true;
            _mdt_commit_sample_to_step();
            _mdt_preview_active_sample();
            mdt_preview_dirty = true;
          }
          _mdt_draw_row(row, row, mdt_cursor_col, mdt_edit_state);
          _mdt_draw_status(row, mdt_cursor_col, mdt_edit_state);
        }
      }
      else {
        uint8_t sp = eR.speed; if (sp == 0) sp = 1; if (sp > 4) sp = 4;

        const uint8_t old_note = seq.note_data[seq.active_pattern][mdt_cursor_row];
        const uint8_t old_vel = seq.vel[seq.active_pattern][mdt_cursor_row];

        if (_mdt_edit_value(mdt_cursor_row, mdt_cursor_col,
          (int16_t)eR.dir * sp)) {
          const uint8_t new_note = seq.note_data[seq.active_pattern][mdt_cursor_row];
          const uint8_t new_vel = seq.vel[seq.active_pattern][mdt_cursor_row];
          const bool note_changed = (old_note != new_note);
          const bool vel_changed = (old_vel != new_vel);

          if (seq.content_type[seq.active_pattern] == 2 &&
            (note_changed || vel_changed)) {
            mdt_force_preview = true;
            mdt_preview_dirty = true;
          }

          switch (mdt_cursor_col) {
          case MDT_COL_NOTE:
            _mdt_draw_one_cell(mdt_cursor_row, MDT_COL_NOTE,
              mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            if (vel_changed)
              _mdt_draw_one_cell(mdt_cursor_row, MDT_COL_VEL,
                mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            _mdt_draw_one_cell(mdt_cursor_row, MDT_CELL_INFO,
              mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            if ((old_note == 0) != (new_note == 0))
              _mdt_draw_one_cell(mdt_cursor_row, MDT_CELL_ST,
                mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            mdt_preview_dirty = true;
            if (note_changed && !seq.running) {
              _mdt_preview_step(mdt_cursor_row);
            }
            break;

          case MDT_COL_VEL:
            _mdt_draw_one_cell(mdt_cursor_row, MDT_COL_VEL,
              mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            if (note_changed)
              _mdt_draw_one_cell(mdt_cursor_row, MDT_COL_NOTE,
                mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            _mdt_draw_one_cell(mdt_cursor_row, MDT_CELL_INFO,
              mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            if (note_changed && ((old_note == 0) != (new_note == 0)))
              _mdt_draw_one_cell(mdt_cursor_row, MDT_CELL_ST,
                mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            if (vel_changed && !seq.running &&
              (seq.content_type[seq.active_pattern] == 2 ||
                seq.content_type[seq.active_pattern] == 3)) {
              _mdt_preview_step(mdt_cursor_row);
            }
            break;

          case MDT_COL_INS:
          case MDT_COL_CMD:
          case MDT_COL_PAR:
            _mdt_draw_one_cell(mdt_cursor_row, mdt_cursor_col,
              mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
            break;
          }
        }
      }
    }

    if (eL.turned) {
      const uint8_t old = mdt_cursor_col;
      int16_t nv = (int16_t)mdt_cursor_col + eL.dir;
      if (nv < 0)              nv = MDT_COL_COUNT - 1;
      if (nv >= MDT_COL_COUNT) nv = 0;
      mdt_cursor_col = (uint8_t)nv;
      if (old != mdt_cursor_col) {
        mdt_prev_cursor_col = old;
        mdt_prev_cursor_row = mdt_cursor_row;
        mdt_cursor_moved = true;
        bool was_pat = (old == MDT_COL_PATTERN);
        bool is_pat = (mdt_cursor_col == MDT_COL_PATTERN);
        if (was_pat != is_pat) _mdt_draw_header();

        _mdt_draw_row(mdt_cursor_row, mdt_cursor_row,
          mdt_cursor_col, mdt_edit_state);
      }
    }

    if (eR.pressed) {
      if (mdt_cursor_col == MDT_COL_PATTERN) {
        mdt_prev_cursor_col = mdt_cursor_col;
        mdt_cursor_col = MDT_COL_NOTE;
        mdt_cursor_row = 0;
        mdt_view_offset = 0;
        mdt_edit_state = false;
        seq.edit_state = false;
        mdt_light_redraw = true;
        mdt_preview_dirty = true;
      }
      else {
        const bool entering_edit = !mdt_edit_state;

        if (entering_edit && _mdt_insert_buffer_into_cursor_step()) {
          mdt_nopv_row = 0xFF;
        }

        mdt_edit_state = !mdt_edit_state;
        seq.edit_state = mdt_edit_state;

        if (entering_edit) {
          _mdt_sync_activeSample_to_step();
        }
        _mdt_load_buffer_from_cursor_step();
        _mdt_draw_header();

        mdt_cursor_moved = true;
        mdt_prev_cursor_row = mdt_cursor_row;
        mdt_prev_cursor_col = mdt_cursor_col;
        mdt_prev_edit_state = mdt_edit_state;
      }
    }

    if (eR.longPressed) {
      _mdt_open_menu();
      return;
    }

    if (seq.active_pattern != mdt_last_pattern) {
      mdt_last_pattern = seq.active_pattern;
      mdt_view_offset = 0;
      _mdt_preview_invalidate();
      mdt_full_redraw = true;
      mdt_light_redraw = false;
      mdt_preview_dirty = true;
      mdt_status_force_full = true;
    }
    if (seq.selected_track != mdt_last_track) {
      mdt_last_track = seq.selected_track;
      mdt_full_redraw = true;
      mdt_light_redraw = false;
      mdt_status_force_full = true;
    }
    if (seq.running != mdt_last_running) {
      mdt_last_running = seq.running;
    }

    if (mdt_full_redraw) {
      _mdt_full_redraw_body();
      _mdt_draw_pattern_preview(mdt_cursor_row);
      mdt_full_redraw = false;
      mdt_light_redraw = false;
      mdt_preview_dirty = false;
      mdt_force_preview = false;
    }
    else if (mdt_light_redraw) {
      _mdt_light_redraw_body();
      _mdt_draw_pattern_preview(mdt_cursor_row);
      mdt_light_redraw = false;
      mdt_preview_dirty = false;
      mdt_force_preview = false;
    }
    else {
      if (mdt_view_changed) {
        _mdt_draw_header();
        _mdt_draw_all_rows(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);
        mdt_view_changed = false;
        mdt_cursor_moved = false;
        mdt_prev_cursor_row = mdt_cursor_row;
        mdt_prev_cursor_col = mdt_cursor_col;
        mdt_prev_edit_state = mdt_edit_state;
      }
      if (mdt_cursor_moved) {
        if (mdt_prev_cursor_row < MDT_ROWS &&
          mdt_prev_cursor_row != mdt_cursor_row) {
          _mdt_draw_row(mdt_prev_cursor_row, mdt_cursor_row,
            mdt_cursor_col, mdt_edit_state);
        }
        if (mdt_cursor_row < MDT_ROWS) {
          _mdt_draw_row(mdt_cursor_row, mdt_cursor_row,
            mdt_cursor_col, mdt_edit_state);
        }
        mdt_prev_cursor_row = mdt_cursor_row;
        mdt_prev_cursor_col = mdt_cursor_col;
        mdt_prev_edit_state = mdt_edit_state;
        mdt_cursor_moved = false;
      }

      _mdt_draw_status(mdt_cursor_row, mdt_cursor_col, mdt_edit_state);

      if (mdt_preview_dirty) {
        if (mdt_force_preview) {
          _mdt_preview_invalidate();
          mdt_force_preview = false;
        }
        _mdt_draw_pattern_preview(mdt_cursor_row);
        mdt_preview_dirty = false;
      }
    }

    _mdt_update_oled_help();
    virtual_keyboard_smart_preselect_mode();
  }

  if (LCDML.FUNC_close()) {
    _mdt_preview_release_all();
    unregisterTouchHandler();
    seq.cycle_touch_element = 0;
    seq.edit_state = false;
    seq.step_recording = false;
    mdt_step_recording = false;
    mdt_view_offset = 0;
    display.fillScreen(COLOR_BACKGROUND);
    helptext_l(back_text);
  }
}

// ===========================================================================
//  MDTracker FX
// ===========================================================================
const char mdt_fx_names[MDT_FX_END][3] = {
  "--", "RV", "D1", "D2", "CH", "PN", "VL", "TR", "GT", "HL",
  "AS", "AY", "AD", "AL", "AN", "AO", "AV", "AE", "AF", "AR",
  "V+", "V-", "VS", "BS", "PS", "VC"
};

const uint8_t mdt_fx_param_max[MDT_FX_END] = {
  0,     // MDT_FX_NONE
  100,   // MDT_FX_REV
  100,   // MDT_FX_DLY1
  100,   // MDT_FX_DLY2
  100,   // MDT_FX_CHR
  127,   // MDT_FX_PAN
  100,   // MDT_FX_VOL
  96,    // MDT_FX_TRN
  16,    // MDT_FX_GAT
  1,     // MDT_FX_HLD
  1,     // MDT_FX_ARP_STOP
  3,     // MDT_FX_ARP_STYLE
  3,     // MDT_FX_ARP_SPEED
  16,    // MDT_FX_ARP_LEN
  22,    // MDT_FX_ARP_MAXNT
  4,     // MDT_FX_ARP_OCT
  127,   // MDT_FX_ARP_VEL
  1,     // MDT_FX_ARP_EUCL
  15,    // MDT_FX_ARP_EUOFF
  1,     // MDT_FX_ARP_RESET
  0,     // MDT_FX_VOICE_UP
  0,     // MDT_FX_VOICE_DN
  31,    // MDT_FX_VOICE_SEL
  99,    // MDT_FX_BANK_SEL
  99,    // MDT_FX_POOL_SEL
  0      // MDT_FX_VOICE_CLR
};

const uint8_t mdt_fx_param_default[MDT_FX_END] = {
  0, 0, 0, 0, 0, 64, 100, 48, 16, 0,
  0, 0, 0, 8, 22, 2, 100, 0, 0, 0,
  0, 0, 0, 0, 0, 0
};

FLASHMEM void mdtracker_init_fx_arrays(void)
{
  for (uint8_t p = 0; p < NUM_SEQ_PATTERN; p++) {
    memset(seq.fx_inst[p], 0xFF, 16);
    memset(seq.fx_cmd[p], 0x00, 16);
    memset(seq.fx_val[p], 0x00, 16);
  }
}

uint8_t seq_effective_instrument(uint8_t track, uint8_t pattern, uint8_t step)
{
  if (pattern < NUM_SEQ_PATTERN && step < 16) {
    const uint8_t ovr = seq.fx_inst[pattern][step];
    if (ovr != 0xFF) return ovr;
  }
  return seq.instrument[track];
}

static FLASHMEM void mdt_apply_fx_to_instrument(uint8_t inst, uint8_t cmd, uint8_t val)
{
  const uint8_t max = mdt_fx_param_max[cmd];
  const uint8_t v = (val > max) ? max : val;

  auto set_dexed = [&](uint8_t i) {
    switch (cmd) {
    case MDT_FX_REV:
      configuration.fx.reverb_send[i] = v;
      reverb_mixer_r.gain(i, volume_transform(v / 100.0f));
      reverb_mixer_l.gain(i, volume_transform(v / 100.0f));
      break;
    case MDT_FX_DLY1:
      configuration.fx.delay_level1[i] = v;
      set_dexed_delay_level(0, i, v);
      break;
    case MDT_FX_DLY2:
      configuration.fx.delay_level2[i] = v;
      set_dexed_delay_level(1, i, v);
      break;
    case MDT_FX_CHR:
      configuration.fx.chorus_level[i] = v;
      dexed_chorus_mixer_r[i].gain(1, mapfloat(v, 0, 100, 0.0, 0.5));
      dexed_chorus_mixer_l[i].gain(1, mapfloat(v, 0, 100, 0.0, 0.5));
      break;
    case MDT_FX_PAN:
      configuration.dexed[i].pan =
        map(v, 0, 127, PANORAMA_MIN, PANORAMA_MAX);
      dexed_mono2stereo[i]->panorama(
        mapfloat(configuration.dexed[i].pan,
          PANORAMA_MIN, PANORAMA_MAX, -1.0, 1.0));
      break;
    case MDT_FX_VOL:
      configuration.dexed[i].sound_intensity = v;
      MicroDexed[i]->setGain(
        midi_volume_transform(map(v, 0, 100, 0, 127)));
      break;
    default: break;
    }
    };

  if (inst < 2 || inst == 70 || inst == 71) {
    const uint8_t i = (inst < 2) ? inst : (uint8_t)(inst - 68);
    if (i < NUM_DEXED) set_dexed(i);
  }
  else if (inst == 2) {
    switch (cmd) {
    case MDT_FX_REV:
      configuration.fx.ep_reverb_send = v;
      reverb_mixer_r.gain(REVERB_MIX_CH_EPIANO, volume_transform(v / 100.0f));
      reverb_mixer_l.gain(REVERB_MIX_CH_EPIANO, volume_transform(v / 100.0f));
      break;
    case MDT_FX_DLY1:
      configuration.fx.ep_delay_send_1 = v;
      global_delay_in_mixer[0]->gain(7, v / 100.0f);
      global_delay_in_mixer[0]->gain(8, v / 100.0f);
      break;
    case MDT_FX_DLY2:
      configuration.fx.ep_delay_send_2 = v;
      global_delay_in_mixer[1]->gain(7, v / 100.0f);
      global_delay_in_mixer[1]->gain(8, v / 100.0f);
      break;
    case MDT_FX_CHR:
      configuration.fx.ep_chorus_level = v;
      ep_chorus_mixer_r.gain(1, mapfloat(v, 0, 100, 0.0, 0.5));
      ep_chorus_mixer_l.gain(1, mapfloat(v, 0, 100, 0.0, 0.5));
      break;
    case MDT_FX_PAN:
      configuration.epiano.pan = map(v, 0, 127, PANORAMA_MIN, PANORAMA_MAX);
      ep_stereo_panorama.panorama(
        mapfloat(configuration.epiano.pan,
          PANORAMA_MIN, PANORAMA_MAX, -1.0, 1.0));
      break;
    case MDT_FX_VOL:
      configuration.epiano.sound_intensity = v;
      ep.setVolume(v / 100.0f);
      break;
    default: break;
    }
  }
  else if (inst == 3 || inst == 4) {
    const uint8_t m = inst - 3;
    switch (cmd) {
    case MDT_FX_REV:
      microsynth[m].rev_send = v;
      microsynth_mixer_reverb.gain(m, volume_transform(v / 100.0f));
      break;
    case MDT_FX_DLY1:
      microsynth[m].delay_send[0] = v;
      global_delay_in_mixer[0]->gain(4 + m, v / 100.0f);
      break;
    case MDT_FX_DLY2:
      microsynth[m].delay_send[1] = v;
      global_delay_in_mixer[1]->gain(4 + m, v / 100.0f);
      break;
    case MDT_FX_CHR: microsynth[m].chorus_send = v; break;
    case MDT_FX_PAN: microsynth[m].pan = map(v, 0, 127, PANORAMA_MIN, PANORAMA_MAX); break;
    case MDT_FX_VOL: microsynth[m].sound_intensity = v; break;
    default: break;
    }
  }
  else if (inst == 5) {
    switch (cmd) {
    case MDT_FX_REV:
      braids_osc.rev_send = v;
      braids_mixer_reverb.gain(0, volume_transform(v / 100.0f));
      braids_mixer_reverb.gain(1, volume_transform(v / 100.0f));
      break;
    case MDT_FX_DLY1:
      braids_osc.delay_send_1 = v;
      global_delay_in_mixer[0]->gain(6, v / 100.0f);
      break;
    case MDT_FX_DLY2:
      braids_osc.delay_send_2 = v;
      global_delay_in_mixer[1]->gain(6, v / 100.0f);
      break;
    case MDT_FX_PAN: braids_osc.pan = map(v, 0, 127, PANORAMA_MIN, PANORAMA_MAX); break;
    case MDT_FX_VOL:
      braids_osc.sound_intensity = v;
      update_braids_volume();
      break;
    default: break;
    }
  }
}

void seq_apply_step_fx(uint8_t track, uint8_t pattern, uint8_t step)
{
  if (pattern >= NUM_SEQ_PATTERN || step >= 16) return;

  const uint8_t cmd = seq.fx_cmd[pattern][step];
  const uint8_t val = seq.fx_val[pattern][step];
  if (cmd == MDT_FX_NONE) return;

  // ---- Voice/Bank/Pool overrides target one DX instance ----
  if (mdt_cmd_is_voice(cmd)) {
    const uint8_t dx_idx = _mdt_voice_fx_target_instance(track, pattern, step);
    if (dx_idx < NUM_DEXED) _mdt_apply_voice_fx(dx_idx, cmd, val);
    return;
  }

  // ---- ARP control commands: global, valid on any track ----
  switch (cmd) {
  case MDT_FX_ARP_STOP:  seq.arp_stopped = (val != 0);              break;
  case MDT_FX_ARP_STYLE: seq.arp_style = constrain(val, 0, 3);    break;
  case MDT_FX_ARP_SPEED: seq.arp_speed = constrain(val, 0, 3);    break;
  case MDT_FX_ARP_LEN:   seq.arp_length = constrain(val, 1, 16);   break;
  case MDT_FX_ARP_MAXNT: seq.arp_num_notes_max = constrain(val, 1, 22);   break;
  case MDT_FX_ARP_OCT:   seq.oct_shift = constrain((int)val - 2, -2, 2); break;
  case MDT_FX_ARP_VEL:   seq.chord_vel = constrain(val, 0, 127);  break;
  case MDT_FX_ARP_EUCL:  seq.euclidean_active = (val != 0);              break;
  case MDT_FX_ARP_EUOFF: seq.euclidean_offset = constrain(val, 0, 15);   break;
  case MDT_FX_ARP_RESET:
    if (val != 0) {
      seq.arp_step = 0;
      seq.arp_counter = 0;
      seq.arp_num_notes_count = 0;
    }
    break;
  default: break;
  }

  // ---- Track-level commands (apply on any track) ----
  switch (cmd) {
  case MDT_FX_TRN: {
    const int8_t shift = (int8_t)val - 48;
    seq.element_shift = constrain(shift, -48, 48);
    break;
  }
  case MDT_FX_GAT: break;
  case MDT_FX_HLD: if (val == 0x00) seq.note_in = 0; break;
  default: break;
  }

  if (seq.track_type[track] == 0) return;

  if (mdt_cmd_is_arp(cmd) ||
    cmd == MDT_FX_TRN || cmd == MDT_FX_GAT || cmd == MDT_FX_HLD) {
    return;
  }

  const uint8_t inst = seq_effective_instrument(track, pattern, step);
  mdt_apply_fx_to_instrument(inst, cmd, val);
}