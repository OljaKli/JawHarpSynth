#include "ui_ra_panel.h"

#if defined(RA_DISPLAY) 

#include <LCDMenuLib2.h>
#include "RA8875.h"
#include "config.h"
#include "sequencer.h"
#include "touch.h"
#include "UI.h"
#include "livesequencer.h"

extern RA8875        display;
extern LCDMenuLib2   LCDML;
extern ts_t          ts;
extern int           numTouchPoints;
extern void          checkWebRemote();
extern void          handleStart(void);
extern void          handleStop(void);
extern sequencer_t   seq;
extern config_t      configuration;
extern void          set_volume(uint8_t v, uint8_t m);
extern bool          save_sys_flag;
extern elapsedMillis save_sys;

// These four are not in UI.h / livesequencer.h — forward-declare here
extern void UI_func_favorites(uint8_t);
extern void UI_func_smart_filter(uint8_t);
extern void UI_func_seq_probabilities(uint8_t);
extern void UI_func_liveseq_graphic(uint8_t);

volatile int8_t ra_panel_enc_l = 0;
volatile int8_t ra_panel_enc_r = 0;
volatile bool   ra_panel_btn_l = false;
volatile bool   ra_panel_btn_r = false;

// ── Panel geometry ────────────────────────────────────────────────────────
static constexpr uint16_t PANEL_X = 325;
static constexpr uint16_t PANEL_W = 70;

// Engine button grid: 4 rows × 2 cols  (Y=5–99)
static constexpr uint8_t BTN_W = 34;
static constexpr uint8_t BTN_H = 22;
static constexpr uint16_t BTN_COL0 = 325;
static constexpr uint16_t BTN_COL1 = 361;
static constexpr uint8_t BTN_ROWS[4] = { 5, 29, 53, 77 };

// Gamepad zone starts here
static constexpr uint8_t GP_Y0 = 100;

// ── L / R buttons — sit above the joystick in the 44 px gap (Y=100–143) ──
// Circles are visual only; touch zones are rectangles for easy tapping.
static constexpr uint16_t BTN_L_CX = 337;
static constexpr uint16_t BTN_R_CX = 367;
static constexpr uint8_t BTN_LR_CY = 122;   // centre Y of both circles
static constexpr uint8_t BTN_RL_R = 12;    // visual radius

// Touch-zone boundaries (rectangular, extended downward and R extended right)
static constexpr uint8_t BTN_ZONE_TOP = GP_Y0;                          // 100
static constexpr uint8_t BTN_ZONE_BOT = 144;  // = JOY_CTR_Y-JOY_RING_R-4 (joystick clear top)
static constexpr uint16_t BTN_LR_MID = (BTN_L_CX + BTN_R_CX) / 2;    // 352

// ── Joystick (Y=144–206 cleared; center Y=175) ───────────────────────────
// JOY_PAD_R <= JOY_CTR_X - PANEL_X - JOY_THUMB_R = 352-325-9 = 18 -> use 18
static constexpr uint16_t JOY_CTR_X = 352;
static constexpr uint8_t JOY_CTR_Y = 175;
static constexpr uint8_t JOY_RING_R = 27;
static constexpr uint8_t JOY_PAD_R = 18;
static constexpr uint8_t JOY_THUMB_R = 9;
static constexpr uint8_t JOY_THRESH = 12;
static constexpr uint8_t JOY_LOCK_THRESH = 8;    // px to commit to an axis
static constexpr uint8_t JOY_DOT_R = 3;    // cardinal dot radius
static constexpr uint8_t JOY_DOT_DIST = 22;   // dot distance from centre (< RING_R to clear vol bar)
static constexpr uint16_t JOY_HOLD_DELAY = 400;  // ms before repeat starts
static constexpr uint8_t JOY_REPEAT_RATE = 120;  // ms between normal repeat steps
static constexpr uint16_t JOY_ACCEL_DELAY = 1000; // ms hold before fast repeat (L/R only)
static constexpr uint8_t JOY_REPEAT_RATE_FAST = 40;   // ms between fast repeat steps

// ── Vertical volume bar — right dead-zone, beside all controls ───────────
// X=388-394 flush with panel right border; ring right edge 379, R button right 379
static constexpr uint16_t VOL_V_X = 388;  // right edge 394 = panel/button right border
static constexpr uint8_t VOL_V_W = 7;
static constexpr uint16_t VOL_V_TOP = GP_Y0;    // 100
static constexpr uint8_t VOL_V_BOT = 238;      // aligns with VOLUME label bottom (Y=228+8)
static constexpr uint16_t VOL_V_H = VOL_V_BOT - VOL_V_TOP;

// ── Volume slider — bottom of panel (Y=210–225) ──────────────────────────
static constexpr uint8_t VOL_SL_Y = 210;
static constexpr uint8_t SL_H = 16;
static constexpr uint8_t SL_THUMB_W = 8;
static constexpr uint8_t SL_THUMB_H = 12;
static constexpr uint16_t SL_TRACK_X = PANEL_X;
// Track and clear stop at VOL_V_X so the slider never writes into the bar column
static constexpr uint16_t SL_MIN_X = PANEL_X;                        // 325
// Thumb travels SL_MIN_X..SL_MAX_X; choose SL_MAX_X so thumb centre at mid = JOY_CTR_X
// SL_MAX_X = 2*JOY_CTR_X - SL_THUMB_W - SL_MIN_X = 2*352-8-325 = 371
static constexpr uint16_t SL_MAX_X = 2 * JOY_CTR_X - SL_THUMB_W - SL_MIN_X; // 371
static constexpr int16_t  SL_CTR_X = (int16_t)((SL_MIN_X + SL_MAX_X) / 2);
static constexpr uint8_t SL_DEAD_ZONE = 6;
static constexpr uint8_t SL_RATE_SLOW = 150;
static constexpr uint8_t SL_RATE_FAST = 40;

static constexpr uint16_t LONG_PRESS_MS = 500;

// Colours
static constexpr uint16_t COL_ACTIVE = RED;
static constexpr uint16_t COL_PRESSED = DX_DARKCYAN;

// ── State ─────────────────────────────────────────────────────────────────
volatile bool ra_panel_virt_up = false;
volatile bool ra_panel_virt_down = false;
volatile bool ra_panel_virt_left = false;
volatile bool ra_panel_virt_right = false;

// Volume slider
static int16_t  sl_thumb_x = SL_CTR_X;
static bool     sl_active = false;
static int16_t  sl_last_x = 0;
static uint32_t sl_rate_ms = 0;

// Volume bar incremental state — 0xFFFF forces a full redraw on first call
static uint16_t last_vol_fill_h = 0xFFFF;

// Joystick
static int16_t  joy_tx = JOY_CTR_X;
static int16_t  joy_ty = JOY_CTR_Y;
static bool     joy_active = false;
static bool     joy_axis_locked = false; // once set, thumb is clamped to one axis
static bool     joy_axis_is_vert = false; // true = vertical axis locked
static bool     joy_fired_once = false; // fired initial step for current deflection
static uint32_t joy_hold_ms = 0;     // time joystick first deflected in this stroke
static uint32_t joy_rate_ms = 0;     // time of last repeat fire

// R button (enter/confirm)
static bool btn_r_held = false;

// L button (short=back, long=seq toggle)
static bool     btn_l_held = false;
static bool     btn_l_long_fired = false;
static uint32_t btn_l_press_time = 0;

// Bookkeeping
static bool     was_down = false;
static bool     panel_drawn = false;
static uint8_t  last_active_btn = 255;
static bool     last_seq_running = false;
static bool     last_edit_mode = false;

// Engine button long-press / assign
static uint8_t  eng_held_idx = 255;  // which engine button is held (255 = none)
static uint32_t eng_press_time = 0;
static uint8_t  flash_btn = 255;  // button showing save-flash (255 = none)
static uint32_t flash_end_ms = 0;

// ── Assignable target descriptors ────────────────────────────────────────
// All pages that can be assigned to a panel button.
// Long-pressing a button saves the currently active page to that slot.
// label / sub: two display lines, each ≤4 chars.
// fn: LCDML function pointer used for navigation (OTHER_jumpToFunc) and
//     identification (OTHER_getIDFromFunction / FUNC_getID comparison).
struct BtnTarget {
  const char* label;
  const char* sub;
  void (*fn)(uint8_t);
};

// RA_NUM_TARGETS is defined in ui_ra_panel.h — keep in sync if adding entries.
// Indices 0-7 map to the 8 physical buttons and define the factory default layout:
//   Left col (top→bottom):  DXD/FM · GRAN · DRUM · SEQ/SNG
//   Right col (top→bottom): BRDS   · EPNO · MSP  · LIVE/SEQ
// Do NOT reorder these — sys.json stores indices, so reordering breaks saved configs.
// NOTE: on a fresh flash with no sys.json the global zero-init gives {0,0,...} (all DXD/FM).
//       Add a default initialiser to sys_t::ra_btn_target in config.h to fix this.
static const BtnTarget TARGETS[RA_NUM_TARGETS] = {
  // ── Slots 0-7: factory button layout ────────────────────────────────────
  { "DEXED",  "VOICE",   UI_func_voice_select          },  //  0  left  row 1
  { "BRDS", "",     UI_func_braids                },  //  1  right row 1
  { "GRANR", "SYNTH",     UI_func_granular              },  //  2  left  row 2
  { "ELEC", "PIANO",     UI_func_epiano                },  //  3  right row 2
  { "DRUM", "EDITR",     UI_func_drums                 },  //  4  left  row 3
  { "MULTI",  "SMP",     UI_func_MultiSamplePlay       },  //  5  right row 3
  { "SEQ",  "SNG",  UI_func_song                  },  //  6  left  row 4
  { "LIVE", "SEQ",  UI_func_livesequencer         },  //  7  right row 4
  // ── Mixer / voices ──────────────────────────────────────────────────────
  { "MAIN", "MIXER",     UI_func_mixer                 },  //  8
  { "VCE",  "EDT",  UI_func_voice_editor          },  //  9
  { "VCE",  "GRP",  UI_func_voice_group_config    },  // 10
  { "ARP",  "",     UI_func_arpeggio              },  // 11
  // ── Dexed sub-pages ─────────────────────────────────────────────────────
  { "DEXED",  "SETUP",  UI_func_dexed_setup           },  // 12
  { "DEXED",  "CONTL",  UI_func_dexed_controllers     },  // 13
  { "DEXED",  "AUDIO",  UI_func_dexed_audio           },  // 14
  // ── Effects ─────────────────────────────────────────────────────────────
  { "MFXS", "",     UI_func_master_effects        },  // 15
  { "MULTI", "BAND",  UI_func_multiband_dynamics    },  // 16
  { "SIDE", "CHAIN",     UI_func_sidechain             },  // 17
  { "FILT", "CUT",  UI_func_filter_cutoff         },  // 18
  { "FILT", "RES",  UI_func_filter_resonance      },  // 19
  // ── Sequencer ───────────────────────────────────────────────────────────
  { "SEQ",  "SET",  UI_func_seq_settings          },  // 20
  { "PAT",  "EDT",  UI_func_seq_pattern_editor    },  // 21
  { "MELO", "PAT",  UI_func_melodic_pattern_editor},  // 22
  { "DRM",  "PAT",  UI_func_drum_pattern_editor   },  // 23
  { "TRKR", "",     UI_func_mdtracker_pattern_editor},  // 24
  { "MUTE", "MTX",  UI_func_seq_mute_matrix       },  // 25
  { "VEL",  "EDT",  UI_func_seq_vel_editor        },  // 26
  { "PROB", "",     UI_func_seq_probabilities     },  // 27
  { "CHRD", "ARR",  UI_func_chord_arranger        },  // 28
  { "LIVE", "GFX",  UI_func_liveseq_graphic       },  // 29
  // ── Instruments ─────────────────────────────────────────────────────────
  { "MICRO", "SYNTH",     UI_func_microsynth            },  // 30
  { "NOISE", "MAKER",     UI_func_noisemaker            },  // 31
  { "SLCE", "",     UI_func_slice_editor          },  // 32
  // ── Utilities ───────────────────────────────────────────────────────────
  { "RCRD", "",     UI_func_recorder              },  // 33
  { "MIDI", "PLY",  UI_func_midiplayer            },  // 34
  { "MIDI", "CHN",  UI_func_midi_channels         },  // 35
  { "CMAP", "",     UI_func_custom_mappings       },  // 36
  { "CV",   "",     UI_func_cv_page               },  // 37
  { "SMRT", "FLT",  UI_func_smart_filter          },  // 38
  { "SINT", "",     UI_func_sound_intensity       },  // 39
  { "VOL",  "",     UI_func_volume                },  // 40
  { "FAVS", "",     UI_func_favorites             },  // 41
  { "FILE", "MGR",  UI_func_file_manager          },  // 42
  { "LOAD", "PFM",  UI_func_load_performance      },  // 43
  { "SAVE", "PFM",  UI_func_save_performance      },  // 44
  { "INFO", "",     UI_func_information           },  // 45
  { "SYS",  "SET",  UI_func_system_settings       },  // 46
};

static constexpr uint8_t NUM_BTNS = 8;  // number of physical button slots on the panel


// Per‑button last drawn state (for incremental updates)
static uint8_t last_btn_target[NUM_BTNS] = { 0 };
static bool    last_btn_active[NUM_BTNS] = { false };

static inline uint16_t btn_col(uint8_t i) { return (i & 1) ? BTN_COL1 : BTN_COL0; }
static inline uint16_t btn_row(uint8_t i) { return BTN_ROWS[i >> 1]; }

// Returns the button slot (0–7) whose assigned target matches the currently active LCDML
// function, or 255 if the current page has no button. Respects user reassignments.
static uint8_t active_button_index() {
  const auto id = LCDML.FUNC_getID();
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    const uint8_t t = configuration.sys.ra_btn_target[i];
    if (id == LCDML.OTHER_getIDFromFunction(TARGETS[t].fn))
      return i;
  }
  return 255;
}

// ── Drawing ───────────────────────────────────────────────────────────────
static void draw_engine_button(uint8_t idx, bool active) {
  const uint16_t x = btn_col(idx);
  const uint16_t y = btn_row(idx);
  const uint16_t bg = (flash_btn == idx) ? COL_ACTIVE
    : active ? DX_DARKCYAN : GREY3;
  checkWebRemote();
  display.fillRect(x, y, BTN_W, BTN_H, bg);
  display.setTextSize(1);
  display.setTextColor(COLOR_SYSTEXT, bg);
  const BtnTarget& t = TARGETS[configuration.sys.ra_btn_target[idx]];
  display.setCursor(x + 2, y + 2);
  display.print(t.label);
  if (t.sub[0]) {
    display.setCursor(x + 2, y + 12);
    display.print(t.sub);
  }
}

// L: short press = back/up in menu · long press = sequencer start/stop
// Colour: GREY3 idle, RED while sequencer is running, cyan while held
static void gp_draw_l(bool pressed) {
  const uint16_t bg = pressed ? COL_PRESSED : (seq.running ? COL_ACTIVE : GREY3);
  checkWebRemote();
  display.fillCircle(BTN_L_CX, BTN_LR_CY, BTN_RL_R, bg);
  display.setTextSize(1);
  display.setTextColor(COLOR_SYSTEXT, bg);
  display.setCursor(BTN_L_CX - 2, BTN_LR_CY - 3);
  display.print("L");
}

// R: enter / confirm selection
// Colour: GREY3 idle, RED while in seq parameter-edit mode, cyan while held
static void gp_draw_r(bool pressed) {
  const uint16_t bg = pressed ? COL_PRESSED : (seq.edit_state == 1 ? COL_ACTIVE : GREY3);
  checkWebRemote();
  display.fillCircle(BTN_R_CX, BTN_LR_CY, BTN_RL_R, bg);
  display.setTextSize(1);
  display.setTextColor(COLOR_SYSTEXT, bg);
  display.setCursor(BTN_R_CX - 2, BTN_LR_CY - 3);
  display.print("R");
}

// ── Volume slider (bottom of panel) ──────────────────────────────────────
static void draw_sl_thumb(int16_t x, uint16_t color) {
  const uint16_t cy = VOL_SL_Y + (SL_H - SL_THUMB_H) / 2;
  checkWebRemote();
  display.fillRect(x, cy, SL_THUMB_W, SL_THUMB_H, color);
}

// Track spans SL_TRACK_X to SL_MAX_X+SL_THUMB_W (= ring right edge) — centred at JOY_CTR_X.
// clear_w still covers to VOL_V_X to erase any old thumb artefacts at right extreme.
static void draw_slider() {
  const uint16_t clear_w = VOL_V_X - PANEL_X;                        // 61: clears to bar
  const uint16_t track_w = SL_MAX_X + SL_THUMB_W - SL_TRACK_X;      // 54: centred at 352
  checkWebRemote();
  display.fillRect(PANEL_X, VOL_SL_Y, clear_w, SL_H, COLOR_BACKGROUND);
  const uint16_t cy = VOL_SL_Y + (SL_H - SL_THUMB_H) / 2;
  checkWebRemote();
  display.fillRect(SL_TRACK_X, cy, track_w, SL_THUMB_H, GREY4);
  draw_sl_thumb(sl_thumb_x, GREY1);
}

static void move_sl_thumb(int16_t new_x) {
  if (new_x < (int16_t)SL_MIN_X) new_x = (int16_t)SL_MIN_X;
  if (new_x > (int16_t)SL_MAX_X) new_x = (int16_t)SL_MAX_X;
  if (new_x == sl_thumb_x) return;
  draw_sl_thumb(sl_thumb_x, GREY4);
  sl_thumb_x = new_x;
  draw_sl_thumb(sl_thumb_x, GREY1);
}

static uint32_t sl_rate_interval(int16_t offset) {
  const int16_t d = abs(offset);
  if (d >= 20)                    return SL_RATE_FAST;
  if (d >= (int16_t)SL_DEAD_ZONE) return SL_RATE_SLOW;
  return 0;
}

// ── Vertical volume bar ───────────────────────────────────────────────────
// Drawn incrementally — only redraws the strip that changed to avoid flicker.
// Pass force=true on init to do a full clear+fill regardless.
static void gp_draw_vol_bar(bool force = false) {
  const uint16_t fill_h = (uint16_t)((uint32_t)VOL_V_H * configuration.sys.vol / VOLUME_MAX);
  if (!force && fill_h == last_vol_fill_h) return;
  checkWebRemote();
  if (force || last_vol_fill_h > VOL_V_H) {
    // Full redraw (init or first call after reset)
    display.fillRect(VOL_V_X, VOL_V_TOP, VOL_V_W, VOL_V_H, GREY4);
    if (fill_h > 0)
      display.fillRect(VOL_V_X, VOL_V_BOT - fill_h, VOL_V_W, fill_h, DX_DARKCYAN);
  }
  else if (fill_h > last_vol_fill_h) {
    // Volume increased — extend cyan upward
    const uint16_t new_h = fill_h - last_vol_fill_h;
    display.fillRect(VOL_V_X, VOL_V_BOT - fill_h, VOL_V_W, new_h, DX_DARKCYAN);
  }
  else {
    // Volume decreased — erase vacated strip with grey
    const uint16_t erase_h = last_vol_fill_h - fill_h;
    display.fillRect(VOL_V_X, VOL_V_BOT - last_vol_fill_h, VOL_V_W, erase_h, GREY4);
  }
  last_vol_fill_h = fill_h;
}

// ── Joystick ──────────────────────────────────────────────────────────────
// Draw the 4 cardinal direction dots at the ring edge.
// Called inside gp_draw_joystick_full and gp_move_thumb (before drawing the thumb).
static void gp_draw_dir_dots() {
  const uint16_t d = JOY_DOT_DIST;
  display.fillCircle(JOY_CTR_X, JOY_CTR_Y - d, JOY_DOT_R, GREY3);
  display.fillCircle(JOY_CTR_X, JOY_CTR_Y + d, JOY_DOT_R, GREY3);
  display.fillCircle(JOY_CTR_X - d, JOY_CTR_Y, JOY_DOT_R, GREY3);
  display.fillCircle(JOY_CTR_X + d, JOY_CTR_Y, JOY_DOT_R, GREY3);
}

// Full redraw — only called on panel init (inside ra_panel_draw).
static void gp_draw_joystick_full() {
  checkWebRemote();
  const uint16_t clear_y = JOY_CTR_Y - JOY_RING_R - 4;
  const uint16_t clear_h = (JOY_RING_R + 4) * 2;
  display.fillRect(PANEL_X, clear_y, PANEL_W, clear_h, COLOR_BACKGROUND);
  display.drawCircle(JOY_CTR_X * 2, JOY_CTR_Y * 2, JOY_RING_R * 2, GREY3);
  display.drawCircle(JOY_CTR_X * 2, JOY_CTR_Y * 2, (JOY_RING_R - 1) * 2, GREY3);
  gp_draw_dir_dots();
  checkWebRemote();
  display.fillCircle(joy_tx, joy_ty, JOY_THUMB_R, GREY1);
}

// Incremental thumb move — minimises GPU ops between erase and redraw to kill flicker.
// Ring is only restored when the old thumb was close enough to overlap it.
// Only the one dot in the direction of travel is restored when needed.
static void gp_move_thumb(int16_t new_tx, int16_t new_ty) {
  if (new_tx == joy_tx && new_ty == joy_ty) return;

  const int16_t old_dx = joy_tx - (int16_t)JOY_CTR_X;
  const int16_t old_dy = joy_ty - (int16_t)JOY_CTR_Y;
  // With cross-shape movement one component is always 0, so abs sum == the real distance.
  const int16_t old_d = abs(old_dx) + abs(old_dy);

  checkWebRemote();
  display.fillCircle(joy_tx, joy_ty, JOY_THUMB_R, COLOR_BACKGROUND);

  // Restore ring only when old thumb reached into it (last ~7 px of travel).
  if (old_d >= (int16_t)(JOY_RING_R - JOY_THUMB_R)) {
    display.drawCircle(JOY_CTR_X * 2, JOY_CTR_Y * 2, JOY_RING_R * 2, GREY3);
    display.drawCircle(JOY_CTR_X * 2, JOY_CTR_Y * 2, (JOY_RING_R - 1) * 2, GREY3);
  }
  // Restore only the single dot in the direction of travel, when covered.
  if (old_d >= (int16_t)(JOY_DOT_DIST - JOY_THUMB_R)) {
    if (old_dx < 0) display.fillCircle(JOY_CTR_X - (int16_t)JOY_DOT_DIST, JOY_CTR_Y, JOY_DOT_R, GREY3);
    else if (old_dx > 0) display.fillCircle(JOY_CTR_X + (int16_t)JOY_DOT_DIST, JOY_CTR_Y, JOY_DOT_R, GREY3);
    else if (old_dy < 0) display.fillCircle(JOY_CTR_X, JOY_CTR_Y - (int16_t)JOY_DOT_DIST, JOY_DOT_R, GREY3);
    else if (old_dy > 0) display.fillCircle(JOY_CTR_X, JOY_CTR_Y + (int16_t)JOY_DOT_DIST, JOY_DOT_R, GREY3);
  }

  joy_tx = new_tx;
  joy_ty = new_ty;
  display.fillCircle(joy_tx, joy_ty, JOY_THUMB_R, GREY1);
}

// Returns 0=none, 1=up, 2=down, 3=left, 4=right
static uint8_t joy_current_dir() {
  const int16_t dx = joy_tx - (int16_t)JOY_CTR_X;
  const int16_t dy = joy_ty - (int16_t)JOY_CTR_Y;
  const int16_t adx = abs(dx);
  const int16_t ady = abs(dy);
  if (adx < (int16_t)JOY_THRESH && ady < (int16_t)JOY_THRESH) return 0;
  if (ady >= adx) return (dy < 0) ? 1 : 2;
  return (dx < 0) ? 3 : 4;
}

// ── Public: full panel redraw (called on page change) ────────────────────
// Draw order: engine buttons → L/R → joystick → slider → label → vol bar.
// Vol bar is drawn last because it shares the rightmost pixel column with earlier draws.
FLASHMEM void ra_panel_draw() {

  const uint8_t active = active_button_index();
  for (uint8_t i = 0; i < NUM_BTNS; i++)
    draw_engine_button(i, i == active);
  last_active_btn = active;

  // L / R buttons in the gap above the joystick
  gp_draw_l(btn_l_held);
  gp_draw_r(btn_r_held);

  // Joystick (clears Y=144–206)
  joy_tx = JOY_CTR_X; joy_ty = JOY_CTR_Y;
  gp_draw_joystick_full();

  // Volume slider + label at the bottom
  sl_thumb_x = SL_CTR_X;
  draw_slider();
  checkWebRemote();
  display.setTextSize(1);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setCursor(JOY_CTR_X - 18, VOL_SL_Y + SL_H + 2);  // "VOLUME" (36px) centred at JOY_CTR_X
  display.print("VOLUME");

  // Vertical vol bar drawn last — force full redraw on init
  last_vol_fill_h = 0xFFFF;
  gp_draw_vol_bar(true);

  // Initialise per‑button state
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    last_btn_target[i] = configuration.sys.ra_btn_target[i];
    last_btn_active[i] = (i == active);
  }
  last_active_btn = active;

  last_seq_running = seq.running;
  last_edit_mode = (seq.edit_state == 1);
  panel_drawn = true;

}

// ── Public: redraw engine buttons only (called when active page changes) ──
FLASHMEM void ra_panel_update_buttons() {
  const uint8_t active = active_button_index();
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    const uint8_t target = configuration.sys.ra_btn_target[i];
    const bool active_flag = (i == active);
    if (target != last_btn_target[i] || active_flag != last_btn_active[i]) {
      draw_engine_button(i, active_flag);
      last_btn_target[i] = target;
      last_btn_active[i] = active_flag;
    }
  }
  last_active_btn = active;
}

// ── Zone predicates ───────────────────────────────────────────────────────
static inline bool in_panel(uint16_t x, uint16_t y) {
  (void)y;
  return x >= PANEL_X && x < PANEL_X + PANEL_W;
}
static inline bool in_eng_btn_zone(uint16_t y) { return y < GP_Y0; }

// L and R use rectangular zones spanning from GP_Y0 down to the joystick clear border.
// R extends all the way to the panel right edge; L takes the left half.
static inline bool in_btn_l(uint16_t x, uint16_t y) {
  return in_panel(x, y)
    && y >= BTN_ZONE_TOP && y < BTN_ZONE_BOT
    && x < BTN_LR_MID;
}
static inline bool in_btn_r(uint16_t x, uint16_t y) {
  return in_panel(x, y)
    && y >= BTN_ZONE_TOP && y < BTN_ZONE_BOT
    && x >= BTN_LR_MID;
}

static inline bool in_vol_sl(uint16_t x, uint16_t y) {
  return in_panel(x, y) && y >= VOL_SL_Y && y < VOL_SL_Y + SL_H;
}
static inline bool in_joystick(uint16_t x, uint16_t y) {
  const int32_t dx = (int16_t)x - JOY_CTR_X;
  const int32_t dy = (int16_t)y - JOY_CTR_Y;
  return (dx * dx + dy * dy) <= (int32_t)JOY_RING_R * JOY_RING_R;
}

bool ra_panel_initialised = false;

// ── Public: touch handler — call every TOUCH_MAX_REFRESH_RATE_MS ─────────
void ra_panel_handle_touch() {

  // ── One‑time panel initialisation (draw once, then never again) ──────
  if (!ra_panel_initialised) {
    checkWebRemote();
    display.fillRect(PANEL_X, 0, PANEL_W, 240, COLOR_BACKGROUND);
    ra_panel_draw();                // draws everything once
    ra_panel_initialised = true;
  }

  // Check for external volume changes from left encoder
  static uint8_t last_volume = 0xFF;
  if (configuration.sys.vol != last_volume) {
    last_volume = configuration.sys.vol;
    gp_draw_vol_bar();
  }

  // ── Update only the engine buttons when the active page changes ──────
  const uint8_t cur_active = active_button_index();
  if (cur_active != last_active_btn) {
    ra_panel_update_buttons();     // redraws only the changed engine buttons
    last_active_btn = cur_active;
  }

  // ── Refresh L button colour when sequencer state changes ─────────────
  if (seq.running != last_seq_running && !btn_l_held) {
    gp_draw_l(false);
    last_seq_running = seq.running;
  }

  // ── Refresh R button colour when edit mode changes ───────────────────
  const bool cur_edit_mode = (seq.edit_state == 1);
  if (cur_edit_mode != last_edit_mode && !btn_r_held) {
    gp_draw_r(false);
    last_edit_mode = cur_edit_mode;
  }

  // ── Clear save‑flash when timer expires ──────────────────────────────
  if (flash_btn != 255 && millis() >= flash_end_ms) {
    const uint8_t was = flash_btn;
    flash_btn = 255;
    draw_engine_button(was, active_button_index() == was);
    // Update stored state after flash clears
    last_btn_target[was] = configuration.sys.ra_btn_target[was];
    last_btn_active[was] = (was == active_button_index());
  }

  // ── Touch state ──────────────────────────────────────────────────────
  const bool     down = (numTouchPoints > 0);
  const bool     pressed = down && !was_down;
  const uint16_t px = ts.p.x;
  const uint16_t py = ts.p.y;

  // ── Engine buttons: long‑press assignment & short‑tap navigation ─────
  if (pressed && in_panel(px, py) && in_eng_btn_zone(py)) {
    for (uint8_t i = 0; i < NUM_BTNS; i++) {
      const uint16_t bx = btn_col(i);
      const uint16_t by = btn_row(i);
      if (px >= bx && px < bx + BTN_W && py >= by && py < by + BTN_H) {
        eng_held_idx = i;
        eng_press_time = millis();
        break;
      }
    }
  }
  // Long‑press threshold reached: assign current page to button
  if (eng_held_idx != 255 && down && (millis() - eng_press_time >= LONG_PRESS_MS)) {
    const uint8_t i = eng_held_idx;
    eng_held_idx = 255;
    const auto cur_id = LCDML.FUNC_getID();
    for (uint8_t t = 0; t < RA_NUM_TARGETS; t++) {
      if (cur_id == LCDML.OTHER_getIDFromFunction(TARGETS[t].fn)) {
        configuration.sys.ra_btn_target[i] = t;
        save_sys_flag = true;
        save_sys = 0;
        flash_btn = i;
        flash_end_ms = millis() + 600;
        draw_engine_button(i, false);
        // Update the stored state
        last_btn_target[i] = t;
        last_btn_active[i] = (i == active_button_index());
        break;
      }
    }
  }
  // Short tap on engine button (finger lift)
  if (eng_held_idx != 255 && !down) {
    LCDML.OTHER_jumpToFunc(TARGETS[configuration.sys.ra_btn_target[eng_held_idx]].fn);
    eng_held_idx = 255;
  }

  // ── R button (enter / confirm) ────────────────────────────────────────
  if (pressed && in_btn_r(px, py)) {
    btn_r_held = true;
    gp_draw_r(true);
  }
  if (btn_r_held && !down) {
    ra_panel_btn_r = true;
    btn_r_held = false;
    gp_draw_r(false);
  }

  // ── L button (short = back, long = seq start/stop) ───────────────────
  if (pressed && in_btn_l(px, py)) {
    btn_l_held = true;
    btn_l_long_fired = false;
    btn_l_press_time = millis();
    gp_draw_l(true);
  }
  if (btn_l_held) {
    if (down && !btn_l_long_fired && (millis() - btn_l_press_time >= LONG_PRESS_MS)) {
      if (seq.running) handleStop();
      else             handleStart();
      btn_l_long_fired = true;
      last_seq_running = seq.running;
    }
    if (!down) {
      if (!btn_l_long_fired) ra_panel_btn_l = true;
      btn_l_held = false;
      last_seq_running = seq.running;
      gp_draw_l(false);
    }
  }

  // ── Volume slider ─────────────────────────────────────────────────────
  if (pressed && in_vol_sl(px, py)) {
    sl_active = true;
    sl_last_x = px;
    sl_rate_ms = millis();
  }
  if (sl_active) {
    if (down) {
      const int16_t raw = sl_thumb_x + ((int16_t)px - sl_last_x);
      const int16_t clamped = raw < (int16_t)SL_MIN_X ? (int16_t)SL_MIN_X
        : raw >(int16_t)SL_MAX_X ? (int16_t)SL_MAX_X : raw;
      sl_last_x += clamped - sl_thumb_x;  // only advance by actual movement; discards out-of-range excess
      move_sl_thumb(clamped);
    }
    if (!down) sl_active = false;
  }
  {
    const int16_t  offset = sl_thumb_x - SL_CTR_X;
    const uint32_t interval = sl_rate_interval(offset);
    const uint32_t now = millis();
    if (interval > 0 && now - sl_rate_ms >= interval) {
      const int8_t step = (offset > 0) ? 1 : -1;
      configuration.sys.vol = (uint8_t)constrain((int)configuration.sys.vol + step, VOLUME_MIN, VOLUME_MAX);
      set_volume(configuration.sys.vol, configuration.sys.mono);
      sl_rate_ms = now;
      gp_draw_vol_bar();
    }
    if (interval == 0) sl_rate_ms = now;
  }
  if (!sl_active)
    move_sl_thumb(sl_thumb_x + (sl_thumb_x < SL_CTR_X ? 1 : sl_thumb_x > SL_CTR_X ? -1 : 0));

  // ── Joystick ─────────────────────────────────────────────────────────
  if (pressed && in_joystick(px, py)) {
    joy_active = true;
    joy_axis_locked = false;
    joy_rate_ms = millis();
  }
  if (joy_active) {
    if (down) {
      int16_t dx = (int16_t)px - (int16_t)JOY_CTR_X;
      int16_t dy = (int16_t)py - (int16_t)JOY_CTR_Y;
      if (dx > (int16_t)JOY_PAD_R) dx = (int16_t)JOY_PAD_R;
      if (dx < -(int16_t)JOY_PAD_R) dx = -(int16_t)JOY_PAD_R;
      if (dy > (int16_t)JOY_PAD_R) dy = (int16_t)JOY_PAD_R;
      if (dy < -(int16_t)JOY_PAD_R) dy = -(int16_t)JOY_PAD_R;
      // Unlock if thumb returns to centre dead zone, allowing a new axis to be chosen
      if (joy_axis_locked && abs(dx) < (int16_t)JOY_LOCK_THRESH && abs(dy) < (int16_t)JOY_LOCK_THRESH)
        joy_axis_locked = false;
      // Lock to dominant axis once threshold is reached
      if (!joy_axis_locked && (abs(dx) >= (int16_t)JOY_LOCK_THRESH || abs(dy) >= (int16_t)JOY_LOCK_THRESH)) {
        joy_axis_is_vert = (abs(dy) >= abs(dx));
        joy_axis_locked = true;
      }
      if (joy_axis_locked) {
        if (joy_axis_is_vert) dx = 0;
        else                  dy = 0;
      }
      gp_move_thumb((int16_t)JOY_CTR_X + dx, (int16_t)JOY_CTR_Y + dy);
    }
    if (!down) {
      joy_active = false;
      joy_axis_locked = false;
    }
  }

  // Direction fire: immediate on first deflection, repeat after hold delay
  {
    const uint8_t  cur_dir = joy_current_dir();
    const uint32_t now = millis();
    if (cur_dir != 0) {
      if (!joy_fired_once) {
        switch (cur_dir) {
        case 1: ra_panel_virt_up = true; break;
        case 2: ra_panel_virt_down = true; break;
        case 3: ra_panel_virt_left = true; break;
        case 4: ra_panel_virt_right = true; break;
        }
        joy_fired_once = true;
        joy_hold_ms = now;
        joy_rate_ms = now;
      }
      else if (now - joy_hold_ms >= JOY_HOLD_DELAY) {
        // L/R accelerate after JOY_ACCEL_DELAY; U/D always use normal rate
        const bool is_horiz = (cur_dir == 3 || cur_dir == 4);
        const bool accel = is_horiz && (now - joy_hold_ms >= JOY_ACCEL_DELAY);
        const uint16_t rate = accel ? JOY_REPEAT_RATE_FAST : JOY_REPEAT_RATE;
        if (now - joy_rate_ms >= rate) {
          switch (cur_dir) {
          case 1: ra_panel_virt_up = true; break;
          case 2: ra_panel_virt_down = true; break;
          case 3: ra_panel_virt_left = true; break;
          case 4: ra_panel_virt_right = true; break;
          }
          joy_rate_ms = now;
        }
      }
    }
    if (cur_dir == 0) joy_fired_once = false;
  }

  // Spring return — snaps thumb to centre when finger lifts
  if (!joy_active && (joy_tx != (int16_t)JOY_CTR_X || joy_ty != (int16_t)JOY_CTR_Y)) {
    gp_move_thumb((int16_t)JOY_CTR_X, (int16_t)JOY_CTR_Y);
  }

  was_down = down;
}

#endif // RA_DISPLAY && TOUCHMENU
