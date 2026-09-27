/*
   MicroDexed-touch  -  MDTracker/New Pattern Editor
   (c) 2024-2026 MicroDexed-touch contributors. GPLv3.
*/
#pragma once

#include "config.h"


#include <stdint.h>

extern bool mdtracker_get_ring_feedback(int16_t* value,
                                        int16_t* minVal,
                                        int16_t* maxVal);

// ---------------------------------------------------------------------------
// MDTracker per-step effect commands
//
// The list is stored as a single byte per step (see seq.fx_cmd in
// sequencer.h).  MDT_FX_END must remain the last enumerator: it is used
// both as the "wrap point" for the tracker's encoder and as the size of
// the name / range tables below.
// ---------------------------------------------------------------------------
enum MdtFxCmd : uint8_t {
  MDT_FX_NONE       = 0x00,
  MDT_FX_REV        = 0x01,
  MDT_FX_DLY1       = 0x02,
  MDT_FX_DLY2       = 0x03,
  MDT_FX_CHR        = 0x04,
  MDT_FX_PAN        = 0x05,
  MDT_FX_VOL        = 0x06,
  MDT_FX_TRN        = 0x07,
  MDT_FX_GAT        = 0x08,
  MDT_FX_HLD        = 0x09,
  // ---- ARP control commands (global; valid on any track) ----
  MDT_FX_ARP_STOP   = 0x0A,   // 0 = run, 1 = stop
  MDT_FX_ARP_STYLE  = 0x0B,   // 0..3   (UP / DOWN / U&D / RAND)
  MDT_FX_ARP_SPEED  = 0x0C,   // 0..3   (1/16 / 1/8 / 1/32 / 1/64)
  MDT_FX_ARP_LEN    = 0x0D,   // 1..16
  MDT_FX_ARP_MAXNT  = 0x0E,   // 1..22
  MDT_FX_ARP_OCT    = 0x0F,   // 0..4   (2 = no shift)
  MDT_FX_ARP_VEL    = 0x10,   // 0..127
  MDT_FX_ARP_EUCL   = 0x11,   // 0/1
  MDT_FX_ARP_EUOFF  = 0x12,   // 0..15
  MDT_FX_ARP_RESET  = 0x13,   // 0 = idle, 1 = reset on this step
  MDT_FX_END
};

// Human-readable 2-char short names, indexed by MdtFxCmd.
extern const char    mdt_fx_names[MDT_FX_END][3];

// Maximum parameter value for each command.  This is the single source
// of truth used by the tracker UI (encoder clamp, ring feedback, status
// bar) and by the apply code in this file, so nothing can disagree
// about the ranges.
extern const uint8_t mdt_fx_param_max[MDT_FX_END];
extern const uint8_t mdt_fx_param_default[MDT_FX_END];

// ---------------------------------------------------------------------------
// Command classification helpers (inlined for use in headers / other TUs).
// ---------------------------------------------------------------------------

// Is the given step an FX-only control step?
// A step qualifies when it carries an FX command and has no note at
// all.  A latched step (note == 130) is a real note.
inline bool mdt_step_is_control(uint8_t note, uint8_t fx_cmd) {
  return (fx_cmd != MDT_FX_NONE) && (note == 0);
}

// Audio FX that target a specific drum sample slot.
inline bool mdt_cmd_is_drum_audio(uint8_t cmd) {
  return (cmd == MDT_FX_REV  || cmd == MDT_FX_DLY1 || cmd == MDT_FX_DLY2 ||
          cmd == MDT_FX_PAN  || cmd == MDT_FX_VOL);
}

// Arpeggiator control commands.
inline bool mdt_cmd_is_arp(uint8_t cmd) {
  return (cmd >= MDT_FX_ARP_STOP && cmd <= MDT_FX_ARP_RESET);
}

// ---------------------------------------------------------------------------
// Library entry points
// ---------------------------------------------------------------------------
void    mdtracker_init_fx_arrays(void);
uint8_t seq_effective_instrument(uint8_t track, uint8_t pattern, uint8_t step);
void    seq_apply_step_fx(uint8_t track, uint8_t pattern, uint8_t step);

// The MDTracker pattern editor UI page (registered in the Sequencer menu).
void UI_func_mdtracker_pattern_editor(uint8_t param);

// Exposed so the sequencer and MIDI path can drive step recording when the
// MDTracker page is active. Kept in ui_mdtracker.cpp.
extern bool mdtracker_step_recording_active(void);