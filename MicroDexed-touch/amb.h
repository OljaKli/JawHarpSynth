/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: a generative ambient garden. Four layers draw on one pool of notes (a scale:
 * the root plus up to eleven more pitch classes) and one chord that drifts slowly through it:
 *
 *   PAD     four voices with unequal breath cycles (as the loops of Music for Airports). Each
 *           takes a note of the current chord when its breath starts, so the chord forms and
 *           dissolves. In the MDT build the pad does not synthesize audio: it emits MIDI notes
 *           on status-byte channel 0, and ambient_garden_impl.h routes them to Braids.
 *   BELLS   four tape loops of unequal length (13, 17, 23, 29 quarter notes) hold short phrases
 *           that shift against each other. On each quarter note a note of the loop is sent as
 *           MIDI on status-byte channel 1; the impl routes those to Dexed instance 1.
 *   BASS    a sine and its sub-octave on the chord's root (now and then its fifth), gliding.
 *           Synthesized locally in the engine.
 *   PULSE   a soft thump, a closed hat and an open hat, each on its own Euclidean pattern
 *           (16, 12 and 7 sixteenths). Synthesized locally in the engine.
 *
 * Over it all, the ARC: a slow tide, minutes long, between sparse and full. Then, after
 * Discreet Music's chain, an Echoplex and a tape loop, and a plate; in the MDT build those
 * are MDT's own plate reverb and delays, fed from the engine's two effect sends.
 *
 * Threads: en_render_split() runs in the audio interrupt. Everything else posts commands, which
 * the render drains at the start of a block. The state below marked (UI) is written by the
 * render and read elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EN_SR 44100.0f
#define EN_NLOOP 4
#define EN_LOOP_MAX 29
#define EN_NDRUM 3
#define EN_NPAD 4

enum { MODE_IONIAN, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_AEOLIAN, MODE_PENT_MAJ,
       MODE_PENT_MIN, EN_NMODES };
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];
extern const char *const EN_NOTE_NAME[12];
extern const uint8_t EN_LOOP_LEN[EN_NLOOP];
extern const uint8_t EN_DRUM_N[EN_NDRUM];

enum {
    P_PAD, P_BELLS, P_BASS, P_PULSE,
    P_DENSITY, P_EVOLVE, P_DRIFT, P_HITS,
    P_ECHO, P_ETIME, P_REVERB, P_SIZE,
    P_BRIGHT, P_SWELL, P_BTONE, P_MOTION,
    P_TUNE, P_MIDI, P_TEMPO, P_ROOT, P_MODE, P_BOCT,
    P_PADTONE,
    P_BASSOCT, P_GLIDE, P_FIFTH,
    P_SWING, P_HATS,
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);

void en_init(void);
void en_set(int param, int value);
void en_pool(uint32_t pool);
void en_play(int on);
void en_next(void);
void en_freeze(int on);
void en_seed(uint32_t s);
void en_panic(void);
void en_regrow(void);
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);

void en_render_split(float *l, float *r, float *rev, float *echo, uint32_t n);

/* MIDI out, from the render: status, data 1, data 2. In the MDT build this is
 * a lock-free ring drained by ambient_garden_impl.h. The pad/bell notes go
 * out on status-byte channels 0 and 1; the impl routes them to Braids and
 * Dexed 1. Bass/drum/clock messages are also emitted but currently dropped by
 * the impl (the original MDT build did not send garden MIDI out). */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

extern volatile uint8_t en_playing, en_frozen, en_ext;
extern volatile uint8_t en_chord[EN_NPAD];
extern volatile uint8_t en_chord_root;
extern volatile uint8_t en_chord_n;
extern volatile float en_drift;
extern volatile float en_pad_level, en_bass_level;
extern volatile uint8_t en_loop_pos[EN_NLOOP];
extern volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];
extern volatile float en_loop_glow[EN_NLOOP];
extern volatile uint8_t en_drum_pos[EN_NDRUM];
extern volatile uint32_t en_drum_pat[EN_NDRUM];
extern volatile float en_drum_glow[EN_NDRUM];
extern volatile uint32_t en_bars;
extern volatile float en_arc;
extern volatile uint8_t en_pulse_rest;
extern volatile uint8_t en_pad_note[EN_NPAD];
extern volatile float en_pad_amp[EN_NPAD];

uint32_t en_euclid(int k, int n, int rot);
int en_scale(uint32_t pool, int *steps);
int en_chord_tones(uint32_t pool, int root, int deg, int *pcs);
void en_voice_lead(const int *from, const int *pcs, int ntones, int *to, int lo, int hi);
const char *en_chord_name(const int *pcs, int n, char *buf);

#define EN_NPIECES 8
const char *en_piece_name(int i);
void en_piece(int i, int16_t *par, uint16_t *pool);

#ifdef __cplusplus
}
#endif