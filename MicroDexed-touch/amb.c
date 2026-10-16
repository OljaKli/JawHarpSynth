/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (amb.h). One compilation unit, -O2.
 *
 * MDT build: the pad and bell layers no longer synthesize audio. They still
 * run their schedulers, but emit MIDI note on/off messages on their own
 * status-byte channel (0 for the pad, 1 for the bells). ambient_garden_impl.h
 * catches those via en_midi_out() and routes them to MDT's Braids (pad) and
 * Dexed instance 1 (bells), both of which are alive in MDT with their full
 * effect chains. The bass and drum layers still synthesize audio in the
 * engine; their dry mix and their two effect sends are handed out on the
 * engine's four outputs through en_render_split().
 *
 * MIDI timing matches the reference instrument's MIDI:
 *  - pad: note-off for the old breath, then note-on for the new one, both in
 *    the same pad_breathe() call.
 *  - bell: note-on fires immediately, the scheduled note-off at t60 seconds
 *    (scaled by the loop's LOOP_DEC) retires the note. Voice-stealing sends
 *    the note-off for the stolen note before reusing the slot.
 *  - a layer whose level is 0 emits no note-ons at all, and its pending notes
 *    are retracted the moment the level reaches 0, so nothing reaches the
 *    plate or the delays either.
 *
 * The four bell loops fire on different sixteenths of the quarter (one loop
 * per sixteenth) so their notes never pile up on the same sample, and their
 * note-on t60 base is 2.2 s (was 4.2 s in the audio reference): against a
 * sustaining Dexed voice, the reference's long tails stacked under the next
 * quarter's notes.
 */
#include "amb.h"
#include "fastmath.h"

#if defined(__IMXRT1062__) && !defined(EN_HOST)
#define EN_COLD   __attribute__((section(".flashmem")))
#define EN_FLASH  __attribute__((section(".progmem")))
#define EN_RAM2   __attribute__((section(".dmabuffers"), used))
#else
#define EN_COLD
#define EN_FLASH
#define EN_RAM2
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char* const EN_NOTE_NAME[12] EN_FLASH = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
const char* const EN_MODE_NAME[EN_NMODES] EN_FLASH = { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Aeolian",
                                             "Pent maj", "Pent min" };
#define B(a, b, c, d, e, f, g) (1u << (a) | 1u << (b) | 1u << (c) | 1u << (d) | 1u << (e) | 1u << (f) | 1u << (g))
const uint16_t EN_MODE_POOL[EN_NMODES] = {
    B(0, 2, 4, 5, 7, 9, 11), B(0, 2, 3, 5, 7, 9, 10), B(0, 1, 3, 5, 7, 8, 10), B(0, 2, 4, 6, 7, 9, 11),
    B(0, 2, 4, 5, 7, 9, 10), B(0, 2, 3, 5, 7, 8, 10), B(0, 2, 4, 7, 9, 9, 9),  B(0, 3, 5, 7, 10, 10, 10) };
#undef B
const uint8_t EN_LOOP_LEN[EN_NLOOP] = { 13, 17, 23, 29 };
const uint8_t EN_DRUM_N[EN_NDRUM] = { 16, 12, 7 };

static const en_param_t PARAMS[P_NPARAMS] EN_FLASH = {
    {"Pad", 0, 100, 60},      {"Bells", 0, 100, 32},    {"Bass", 0, 100, 45},     {"Pulse", 0, 100, 40},
    {"Density", 0, 100, 40},  {"Evolve", 0, 100, 35},   {"Drift", 0, 100, 25},    {"Hits", 0, 100, 35},
    {"Echo", 0, 100, 35},     {"Echo time", 0, 5, 3},   {"Reverb", 0, 100, 65},   {"Size", 0, 100, 80},
    {"Bright", 0, 100, 40},   {"Swell", 0, 100, 60},    {"Bell tone", 0, 100, 50}, {"Motion", 0, 100, 50},
    {"Tune", -50, 50, 0},     {"MIDI out", 0, 1, 1},    {"Tempo", 30, 120, 72},   {"Root", 0, 11, 2},
    {"Mode", 0, EN_NMODES - 1, 1}, {"Bell oct", -2, 2, 0}, {"Pad tone", 0, 1, 0},
    {"Bass oct", -1, 1, 0},   {"Glide", 0, 100, 20},    {"Fifth", 0, 100, 35},
    {"Swing", 0, 100, 0},     {"Hats", 0, 100, 50},
};
EN_COLD const en_param_t* en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

static const struct {
    const char* name;
    int8_t v[P_NPARAMS];
} PIECES[EN_NPIECES] EN_FLASH = {
    {"GARDEN",       {55, 80, 35, 40, 40, 35, 25, 35, 45, 3, 75, 80, 40, 60, 50, 50, 0, 1, 72, 2, 1, 0, 0, 0, 20, 35, 0, 40}},
    {"DEPARTURES",   {95, 84, 0, 0, 35, 30, 40, 0, 30, 3, 70, 85, 20, 55, 5, 40, 0, 1, 60, 1, 0, -1, 0, 0, 30, 25, 0, 30}},
    {"VOICES",       {35, 0, 32, 0, 30, 30, 50, 0, 20, 5, 80, 90, 47, 70, 30, 55, 0, 1, 56, 5, 5, 0, 1, -1, 45, 30, 0, 40}},
    {"AFTERNOON",    {18, 60, 30, 35, 55, 45, 20, 30, 50, 2, 70, 75, 50, 50, 60, 55, 0, 1, 84, 7, 3, 0, 0, 0, 15, 40, 15, 35}},
    {"NIGHT BUS",    {35, 73, 30, 42, 45, 50, 15, 55, 55, 1, 65, 70, 35, 40, 75, 70, 0, 1, 96, 9, 5, 0, 0, 0, 10, 45, 30, 38}},
    {"SHORELINE",    {25, 65, 32, 0, 30, 40, 40, 0, 85, 4, 65, 95, 30, 80, 0, 85, 0, 1, 64, 4, 6, 0, 0, 0, 40, 20, 0, 10}},
    {"GLASS",        {14, 55, 26, 25, 60, 55, 25, 25, 45, 2, 75, 80, 45, 45, 90, 50, 0, 1, 76, 3, 7, 1, 0, 1, 10, 30, 10, 35}},
    {"TIDES",        {12, 76, 32, 45, 45, 75, 10, 40, 50, 3, 75, 85, 40, 60, 55, 95, 0, 1, 70, 10, 4, 0, 1, 0, 25, 55, 10, 30}},
};

EN_COLD const char* en_piece_name(int i) { return PIECES[(unsigned)i < EN_NPIECES ? i : 0].name; }
EN_COLD void en_piece(int i, int16_t* par, uint16_t* pool)
{
    int p;
    i = (unsigned)i < EN_NPIECES ? i : 0;
    for (p = 0; p < P_NPARAMS; p++) par[p] = PIECES[i].v[p];
    *pool = EN_MODE_POOL[PIECES[i].v[P_MODE]];
}

static const char* const ETIME_NAME[6] EN_FLASH = { "1/4.", "1/2", "1/2.", "1 bar", "2 bars", "4 bars" };

EN_COLD static char* itoa_s(int v, char* b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg) v = -v;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 6);
    if (neg) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
    return b;
}

EN_COLD static float drift_secs(int v) { return 20.0f * fm_exp2f((float)v * 0.039069f); }

EN_COLD void en_param_text(int i, int v, char* b)
{
    static const char* const ONOFF[2] = { "Off", "On" };
    const char* s = 0;
    if (i == P_ETIME) s = ETIME_NAME[(unsigned)v < 6u ? v : 0];
    else if (i == P_MIDI) s = ONOFF[v ? 1 : 0];
    else if (i == P_PADTONE) s = v ? "Voices" : "Rhodes";
    else if (i == P_MODE) s = EN_MODE_NAME[(unsigned)v < EN_NMODES ? v : 0];
    else if (i == P_ROOT) s = EN_NOTE_NAME[(unsigned)v < 12u ? v : 0];
    if (s) {
        int k = 0;
        while (s[k] && k < 9) { b[k] = s[k]; k++; }
        b[k] = 0;
        return;
    }
    if (i == P_DRIFT) {
        int sec = (int)(drift_secs(v) + 0.5f);
        if (sec < 60) { b = itoa_s(sec, b); *b++ = 's'; *b = 0; }
        else { b = itoa_s(sec / 60, b); *b++ = ':'; *b++ = (char)('0' + sec % 60 / 10); *b++ = (char)('0' + sec % 10); *b = 0; }
        return;
    }
    if (i == P_SWING) { b = itoa_s(50 + v / 4, b); *b++ = '%'; *b = 0; return; }
    if ((i == P_TUNE || i == P_BOCT || i == P_BASSOCT) && v > 0) *b++ = '+';
    itoa_s(v, b);
}

/* ------------------------------------------------------- the pure parts --- */
EN_COLD uint32_t en_euclid(int k, int n, int rot)
{
    uint32_t gb[32], m = 0;
    uint8_t gl[32];
    int ng, heads, tails, i, pos = 0;
    if (n <= 0 || n > 32 || k <= 0) return 0;
    if (k > n) k = n;
    for (i = 0; i < n; i++) { gb[i] = i < k ? 1u : 0u; gl[i] = 1; }
    ng = n; heads = k; tails = n - k;
    while (tails > 1 && heads > 0) {
        int pairs = heads < tails ? heads : tails;
        for (i = 0; i < pairs; i++) {
            gb[i] |= gb[ng - pairs + i] << gl[i];
            gl[i] = (uint8_t)(gl[i] + gl[ng - pairs + i]);
        }
        if (heads > tails) { tails = heads - pairs; heads = pairs; }
        else { tails -= pairs; heads = pairs; }
        ng = heads + tails;
    }
    for (i = 0; i < ng; i++) {
        uint32_t j;
        for (j = 0; j < gl[i]; j++)
            if (gb[i] >> j & 1u)
                m |= 1u << (((pos + (int)j + rot) % n + n) % n);
        pos += gl[i];
    }
    return m;
}

EN_COLD int en_scale(uint32_t pool, int* steps)
{
    int i, m = 0;
    pool |= 1u;
    for (i = 0; i < 12; i++) if (pool >> i & 1u) steps[m++] = i;
    return m;
}

EN_COLD int en_chord_tones(uint32_t pool, int root, int deg, int* pcs)
{
    static const uint8_t OFF[7] = { 0, 2, 4, 6, 1, 3, 5 };
    int st[12], m = en_scale(pool, st), want, n = 0, i, j;
    want = m >= 6 ? 4 : 3;
    deg = ((deg % m) + m) % m;
    for (i = 0; i < 7 && n < want; i++) {
        int pc = (root + st[(deg + OFF[i]) % m]) % 12, dup = 0;
        for (j = 0; j < n; j++) dup |= pcs[j] == pc;
        if (!dup) pcs[n++] = pc;
    }
    return n;
}

EN_COLD static int nearest(int from, int pc, int lo, int hi)
{
    int n = from + ((pc - from) % 12 + 12) % 12;
    if (n - from > 6) n -= 12;
    while (n < lo) n += 12;
    while (n > hi) n -= 12;
    return n;
}

EN_COLD void en_voice_lead(const int* from, const int* pcs, int nt, int* to, int lo, int hi)
{
    static const uint8_t PERM[24][4] EN_FLASH = {
        {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
        {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
        {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
        {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0} };
    int t[4], p, v, best = 1 << 30;
    for (v = 0; v < 4; v++) t[v] = pcs[v < nt ? v : 0];
    for (p = 0; p < 24; p++) {
        int c = 0, n[4], a, b;
        for (v = 0; v < 4; v++) {
            n[v] = nearest(from[v], t[PERM[p][v]], lo, hi);
            c += n[v] > from[v] ? n[v] - from[v] : from[v] - n[v];
        }
        for (a = 0; a < 4; a++)
            for (b = a + 1; b < 4; b++)
                if (n[a] == n[b]) c += 40;
        if (c < best) { best = c; for (v = 0; v < 4; v++) to[v] = n[v]; }
    }
}

EN_COLD const char* en_chord_name(const int* pcs, int n, char* buf)
{
    uint32_t iv = 0;
    int i, k = 0, third, fifth;
    const char* q = "", * r = EN_NOTE_NAME[pcs[0] % 12];
    for (i = 1; i < n; i++) iv |= 1u << ((pcs[i] - pcs[0] + 12) % 12);
    third = iv >> 4 & 1u ? 4 : iv >> 3 & 1u ? 3 : 0;
    fifth = iv >> 7 & 1u ? 7 : iv >> 6 & 1u ? 6 : iv >> 8 & 1u ? 8 : 0;
    if (third == 4) {
        q = fifth == 8 ? "aug" : iv >> 11 & 1u ? "M7" : iv >> 10 & 1u ? "7" : iv >> 9 & 1u ? "6"
            : iv >> 2 & 1u ? "add9" : "";
    }
    else if (third == 3) {
        if (fifth == 6) q = iv >> 10 & 1u ? "m7b5" : iv >> 9 & 1u ? "dim7" : "dim";
        else q = iv >> 10 & 1u ? "m7" : iv >> 11 & 1u ? "mM7" : iv >> 9 & 1u ? "m6" : iv >> 2 & 1u ? "madd9" : "m";
    }
    else {
        q = iv >> 5 & 1u ? (iv >> 10 & 1u ? "7sus4" : "sus4") : iv >> 2 & 1u ? "sus2" : "5";
    }
    while (*r) buf[k++] = *r++;
    while (*q) buf[k++] = *q++;
    buf[k] = 0;
    return buf;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_POOL, C_PLAY, C_NEXT, C_FREEZE, C_SEED, C_PANIC, C_CLOCK, C_REGROW };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
EN_COLD static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN) return;
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
EN_COLD void en_set(int p, int v) { post(C_SET, p, v); }
EN_COLD void en_pool(uint32_t pool) { post(C_POOL, 0, (int)(pool & 0xFFFu)); }
EN_COLD void en_play(int on) { post(C_PLAY, on, 0); }
EN_COLD void en_next(void) { post(C_NEXT, 0, 0); }
EN_COLD void en_freeze(int on) { post(C_FREEZE, on, 0); }
EN_COLD void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
EN_COLD void en_panic(void) { post(C_PANIC, 0, 0); }
EN_COLD void en_clock(int msg) { post(C_CLOCK, msg, 0); }
EN_COLD void en_regrow(void) { post(C_REGROW, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile uint8_t en_playing, en_frozen, en_ext;
volatile uint8_t en_chord[EN_NPAD], en_chord_root, en_chord_n;
volatile float en_drift, en_pad_level, en_bass_level;
volatile uint8_t en_loop_pos[EN_NLOOP];
volatile int8_t en_loop_note[EN_NLOOP][EN_LOOP_MAX];
volatile float en_loop_glow[EN_NLOOP];
volatile uint8_t en_drum_pos[EN_NDRUM];
volatile uint32_t en_drum_pat[EN_NDRUM];
volatile float en_drum_glow[EN_NDRUM];
volatile uint32_t en_bars;
volatile float en_arc;
volatile uint8_t en_pulse_rest;
volatile uint8_t en_pad_note[EN_NPAD];
volatile float en_pad_amp[EN_NPAD];

static int16_t par[P_NPARAMS];
static uint32_t pool = 0xAADu;
static int nsteps, steps[12];
static int deg;
static int chord_pcs[4], chord_nt;
static float tunefac = 1.0f;
static uint32_t rng = 0x9E3779B9u;

EN_COLD static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
EN_COLD static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
EN_COLD static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }

/* All note-ons and note-offs for the pad and bell layers go out through
 * en_midi_out(); the impl routes pad notes on status-byte channel 0 to
 * Braids and bell notes on status-byte channel 1 to Dexed 1. When the
 * PAD or BELLS layer level is zero, its scheduler emits no note-ons and
 * apply() retracts anything that was already ringing, so nothing reaches
 * the plate or the delays either. */
EN_COLD static void midi(uint32_t st, uint32_t d1, uint32_t d2) { en_midi_out(st, d1, d2); }

EN_COLD static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

#define SN 1024
static float sint[SN + 1] EN_RAM2;
static inline float sine(float ph)
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}

/* ------------------------------------------------- slow random motion --- */
#define NMOD 9
#define NMOD_PAD 6
static struct { float v, tgt, rate; uint32_t left; } mod[NMOD];

EN_COLD static void mods_step(uint32_t n)
{
    int i;
    for (i = 0; i < NMOD; i++) {
        if (mod[i].left <= n) {
            float secs = 7.0f + 33.0f * frand();
            mod[i].tgt = frand() * 2.0f - 1.0f;
            mod[i].left = (uint32_t)(secs * EN_SR);
            mod[i].rate = (float)n / (secs * EN_SR * 0.5f);
        }
        else mod[i].left -= n;
        mod[i].v += (mod[i].tgt - mod[i].v) * fm_minf(mod[i].rate, 1.0f);
    }
}
static float motion;

/* ---------------------------------------------------------------- pad ---
 * MIDI-only note scheduler. Each voice runs its own unequal breath cycle
 * (BREATH[i]); on the start of each breath it emits a note-off for the
 * previous MIDI note and a note-on for the next, both on status-byte
 * channel 0, exactly as the reference instrument emits its MIDI. */
typedef struct {
    float note, target;
    float t, rate, peak;
    uint8_t midi_note, hand, voices, on;
    float hf;
} pad_t;
static pad_t pad[EN_NPAD];
static const float BREATH[EN_NPAD] = { 1.0f, 1.21f, 1.43f, 1.67f };
static const float START_T[EN_NPAD] = { 0.0f, 0.9f, 0.82f, 0.95f };
static float arcm;

EN_COLD static float breath_env(float t)
{
    if (t < 0.4f) return 0.5f - 0.5f * fm_cosf(FM_PI * t * 2.5f);
    if (t < 0.55f) return 1.0f;
    if (t < 0.9f) return 0.5f + 0.5f * fm_cosf(FM_PI * (t - 0.55f) * (1.0f / 0.35f));
    return 0.0f;
}

/* the amplitude proxy the reference tracked in pad_t::amp, so the hand-off
 * and chord-leave tests can tell a singing voice from a resting one */
static inline float pad_amp(const pad_t* v) { return breath_env(v->t) * v->peak * v->hf; }

static float swell;

EN_COLD static void pad_breathe(pad_t* v, int i)
{
    float secs = (4.0f + 0.12f * swell) * BREATH[i] * (0.85f + 0.3f * frand());
    v->rate = 32.0f / (secs * EN_SR);
    v->peak = 0.6f + 0.4f * frand();
    if (en_playing && v->midi_note) midi(0x80, v->midi_note, 0);
    v->midi_note = 0;
    v->note = v->target;
    v->voices = (uint8_t)(par[P_PADTONE] != 0);
    if (en_playing && par[P_PAD] > 0) {
        v->midi_note = (uint8_t)v->target;
        midi(0x90, v->midi_note, (uint32_t)(50.0f + 40.0f * v->peak));
        v->on = 1;
    }
    else {
        v->on = 0;
    }
}

#define HAND_STEP (32.0f / (1.0f * EN_SR))
EN_COLD static void pad_handoff(void)
{
    int i;
    for (i = 0; i < EN_NPAD; i++) {
        pad_t* v = &pad[i];
        if (pad_amp(v) > 0.002f && ((float)v->midi_note != v->target || v->voices != (par[P_PADTONE] != 0)))
            v->hand = 1;
        else if (!v->hand)
            v->note = v->target;
    }
}

EN_COLD static void pad_leave(void)
{
    int i, k;
    for (i = 0; i < EN_NPAD; i++) {
        pad_t* v = &pad[i];
        int pc = v->midi_note % 12, shared = 0;
        for (k = 0; k < chord_nt; k++) shared |= chord_pcs[k] == pc;
        if (pad_amp(v) > 0.002f && !shared && !v->hand) v->hand = 1;
        else if (pad_amp(v) <= 0.002f && !v->hand) v->note = v->target;
    }
}

EN_COLD static void pad_coefs(pad_t* v, int i)
{
    if (v->hand == 1) {
        v->hf -= HAND_STEP;
        if (v->hf <= 0.0f) {
            v->hf = 0.0f;
            v->hand = 2;
            v->t = 0.15f;
            pad_breathe(v, i);
        }
    }
    else if (v->hand == 2) {
        v->hf += HAND_STEP * 0.6f;
        if (v->hf >= 1.0f) { v->hf = 1.0f; v->hand = 0; }
    }
    v->t += v->rate;
    if (v->t >= 1.0f) { v->t -= 1.0f; pad_breathe(v, i); }
}

/* wake any pad voice that isn't currently sounding, staggered by START_T[]
 * so a fresh start doesn't put all four note-ons in the same block. Used by
 * play_start and by apply() when the PAD slider moves off 0. */
EN_COLD static void wake_silent_pads(void)
{
    int i;
    if (!en_playing) return;
    for (i = 0; i < EN_NPAD; i++)
        if (!pad[i].on) {
            pad[i].t = START_T[i];
            pad[i].midi_note = 0;
            pad_breathe(&pad[i], i);
        }
}

/* ---------------------------------------------------------------- bass --- */
static struct { float ph, inc, note, target, amp; uint8_t midi_note, rest; } bass;
static float bass_vol, glide_k = 1.0f / (0.25f * EN_SR);

/* --------------------------------------------------------------- bells ---
 * MIDI-only note scheduler. A small pool of slots tracks each emitted note.
 * A note-on fires immediately; a scheduled note-off retires the note at t60
 * seconds, scaled by the loop's own LOOP_DEC, so the four loops ring for
 * visibly different times. Reusing the oldest slot when the pool is full
 * sends the old note's note-off first (as the reference did on voice steal).
 * The pool is sized for the worst case: four loops, low notes with LOOP_DEC
 * up to 1.6, plus the occasional octave-up shadow. */
#define BELL_OFF_Q 64
static struct {
    uint32_t off_at;
    uint8_t note;
    uint8_t vel;
    uint8_t active;
    uint8_t loop;               /* which of the four tape loops this note came from */
} bell_slot[BELL_OFF_Q];
static uint32_t bell_sample_count;
static int8_t loop_note[EN_NLOOP][EN_LOOP_MAX];
static uint8_t loop_vel[EN_NLOOP][EN_LOOP_MAX], loop_pos[EN_NLOOP];
static const uint8_t LOOP_BASE[EN_NLOOP] = { 72, 67, 79, 62 };
/* the four loops' note-off decay, so their bell tails can be told apart.
 * The base t60 below is set to 2.2 s (was 4.2 s in the audio reference):
 * against a Dexed voice that sustains, the reference's long tails piled up
 * under the next quarter's notes. 2.2 s keeps a real bell character without
 * stacking. */
static const float LOOP_DEC[EN_NLOOP] = { 1.0f, 1.35f, 0.75f, 1.6f };
#define BELL_T60_BASE   2.2f
/* Bell MIDI velocity multiplier. loop_vel is 50..99, so v/100 is 0.50..0.99;
 * this constant turns that into the outgoing MIDI velocity. Reduced from
 * 120.0f to 84.0f — a 30 % drop in the bell (Dexed) voice velocity. The
 * octave-up shadow in bell_ring() uses vel * 0.4f, so its ratio to the
 * parent note is preserved; only the overall level goes down. */
#define BELL_VEL (84.0f)

/* --------------------------------------------------------------- pulse --- */
typedef struct {
    float ph, inc, inc_end, amp, adec, idx, x1, x2, att, mph[4], pend;
    int32_t wait;
    uint8_t on, note;
} drum_t;
static drum_t drum[EN_NDRUM];
static uint8_t drum_pos[EN_NDRUM];
static uint32_t drum_pat[EN_NDRUM];
static float pulse_vol, swing, hat_f = 0.9f, hat_open = 0.42f;
static float step_len, pulse_left;
static uint32_t noise = 22222u;
static const uint8_t DRUM_KMAX[EN_NDRUM] = { 5, 7, 3 };
static const uint8_t DRUM_GM[EN_NDRUM] = { 36, 42, 46 };
static const float HAT_INC[4] = { 304.4f / EN_SR, 369.6f / EN_SR, 522.7f / EN_SR, 800.0f / EN_SR };

static float echo_send, rev_send;
static float rev_in_gain = 1.0f;

/* -------------------------------------------------------------- harmony --- */
EN_COLD static void scale_set(void) { nsteps = en_scale(pool, steps); }

EN_COLD static int bell_note(int l, int off)
{
    int d = deg + off, oct = d / nsteps, s = steps[d % nsteps], base, root = par[P_ROOT], n;
    base = LOOP_BASE[l] - ((LOOP_BASE[l] - root) % 12 + 12) % 12;
    n = base + s + 12 * oct + 12 * par[P_BOCT];
    while (n > 103) n -= 12;
    while (n < 36) n += 12;
    return n;
}

EN_COLD static void pad_retarget(void)
{
    int from[4], to[4], i;
    chord_nt = en_chord_tones(pool, par[P_ROOT], deg, chord_pcs);
    for (i = 0; i < EN_NPAD; i++) from[i] = (int)(pad[i].target + 0.5f);
    en_voice_lead(from, chord_pcs, chord_nt, to, 48, 77);
    for (i = 0; i < EN_NPAD; i++) {
        pad[i].target = (float)to[i];
        en_chord[i] = (uint8_t)to[i];
    }
    en_chord_root = (uint8_t)chord_pcs[0];
    en_chord_n = (uint8_t)chord_nt;
}

EN_COLD static void bass_to(int pc)
{
    int n = 36 + pc % 12 + 12 * par[P_BASSOCT];
    bass.target = (float)n;
    if (en_playing && bass.midi_note != n) {
        midi(0x85, bass.midi_note, 0);       // was 0x82
        midi(0x95, (uint32_t)n, 80);         // was 0x92
    }
    bass.midi_note = (uint8_t)n;
}

static float drift_left, drift_total, evolve;
static uint8_t change_pending, fifth_done;

EN_COLD static void drift_reset(void)
{
    drift_total = drift_secs(par[P_DRIFT]) * (0.7f + 0.6f * frand()) * EN_SR;
    drift_left = drift_total;
    fifth_done = 0;
}

EN_COLD static void chord_change(void)
{
    static const float W7[7] = { 4.0f, 2.0f, 1.0f, 3.0f, 3.0f, 3.0f, 0.4f };
    float w[12], sum = 0.0f, r;
    int i;
    for (i = 0; i < nsteps; i++) {
        w[i] = i == deg ? 0.0f : nsteps == 7 ? W7[i] : i == 0 ? 3.0f : 1.5f;
        sum += w[i];
    }
    r = frand() * sum;
    for (i = 0; i < nsteps; i++) { r -= w[i]; if (r <= 0.0f && w[i] > 0.0f) break; }
    if (i >= nsteps) i = deg == 0 ? (nsteps > 1 ? 1 : 0) : 0;
    deg = i;
    pad_retarget();
    pad_leave();
    bass_to(chord_pcs[0]);
    bass.rest = frand() < evolve * 0.3f + 0.25f * fm_maxf(-arcm, 0.0f);
    drift_reset();
}

/* ------------------------------------------------------------- the loops --- */
EN_COLD static int rand_offset(void)
{
    if (frand() < 0.72f) {
        int o = 2 * irand(chord_nt);
        return o + (frand() < 0.3f ? nsteps : 0);
    }
    return 1 + 2 * irand(3);
}

EN_COLD static int loop_count(int l) { int i, c = 0; for (i = 0; i < EN_LOOP_LEN[l]; i++) c += loop_note[l][i] >= 0; return c; }

EN_COLD static int loop_target(void)
{
    float d = (float)par[P_DENSITY] * 0.05f * (1.0f + 0.8f * arcm);
    return (int)(fm_clampf(d, 0.0f, 5.0f) + frand());
}
static uint8_t loop_rest[EN_NLOOP];

EN_COLD static void loop_add(int l)
{
    int len = EN_LOOP_LEN[l], i, s;
    for (i = 0; i < 8; i++) {
        s = irand(len);
        if (loop_note[l][s] < 0) {
            loop_note[l][s] = (int8_t)rand_offset();
            loop_vel[l][s] = (uint8_t)(50 + irand(50));
            return;
        }
    }
}

EN_COLD static int loop_pick(int l)
{
    int len = EN_LOOP_LEN[l], i, s = irand(len);
    for (i = 0; i < len; i++, s = (s + 1) % len) if (loop_note[l][s] >= 0) return s;
    return -1;
}

EN_COLD static void loop_publish(int l)
{
    int i;
    for (i = 0; i < EN_LOOP_MAX; i++) en_loop_note[l][i] = i < EN_LOOP_LEN[l] ? loop_note[l][i] : -1;
}

EN_COLD static void loop_regrow(int l)
{
    int i, n = loop_target();
    for (i = 0; i < EN_LOOP_MAX; i++) loop_note[l][i] = -1;
    for (i = 0; i < n; i++) loop_add(l);
    loop_publish(l);
}

EN_COLD static void loop_pass(int l)
{
    int c = loop_count(l), t = loop_target(), s;
    for (; c < t; c++) loop_add(l);
    for (; c > t && (s = loop_pick(l)) >= 0; c--) loop_note[l][s] = -1;
    loop_rest[l] = frand() < evolve * 0.15f + 0.15f * fm_maxf(-arcm, 0.0f);
    if (frand() < evolve * 0.7f && (s = loop_pick(l)) >= 0) {
        if (frand() < 0.5f) loop_note[l][s] = (int8_t)rand_offset();
        else {
            int d = irand(EN_LOOP_LEN[l]);
            if (loop_note[l][d] < 0) {
                loop_note[l][d] = loop_note[l][s];
                loop_vel[l][d] = loop_vel[l][s];
                loop_note[l][s] = -1;
            }
        }
    }
    loop_publish(l);
}

/* The bell voice: emit the note-on now, schedule the note-off for t60 * the
 * loop's decay. A slot whose note is already sounding gets its note-off first
 * (the reference did the same on voice steal). */
EN_COLD static void bell_voice(int l, int n, float vel)
{
    int i, best = 0, free_slot = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    float t60;
    uint8_t v;
    if (!par[P_BELLS]) return;   /* layer off: send nothing */
    for (i = 0; i < BELL_OFF_Q; i++) {
        if (!bell_slot[i].active) { free_slot = i; break; }
        if (bell_slot[i].off_at < oldest) { oldest = bell_slot[i].off_at; best = i; }
    }
    if (free_slot >= 0) best = free_slot;
    if (bell_slot[best].active)
        midi(0x80 | (uint32_t)(bell_slot[best].loop + 1), bell_slot[best].note, 0);
    v = (uint8_t)(vel * BELL_VEL);
    if (v > 127) v = 127;
    midi(0x90 | (uint32_t)(l + 1), (uint32_t)n, v);      /* was midi(0x91, n, v) */
    t60 = BELL_T60_BASE - (float)(n - 60) * 0.045f;
    t60 = fm_clampf(t60, 1.0f, BELL_T60_BASE) * LOOP_DEC[l];
    bell_slot[best].off_at = bell_sample_count + (uint32_t)(t60 * EN_SR);
    bell_slot[best].note = (uint8_t)n;
    bell_slot[best].vel = v;
    bell_slot[best].loop = (uint8_t)l;                  /* NEW */
    bell_slot[best].active = 1;
    en_loop_glow[l] = 1.0f;
}

EN_COLD static void bell_ring(int l, int slot)
{
    int n, i, has_free = 0;
    float vel;
    if (!par[P_BELLS]) return;
    n = bell_note(l, loop_note[l][slot]);
    vel = (float)loop_vel[l][slot] * 0.01f;
    bell_voice(l, n, vel);
    /* the octave-up shadow is a decoration: 3..8 % of notes, and never at
     * the cost of a ringing slot */
    if (n + 12 > 103) return;
    if (frand() >= 0.03f + 0.05f * fm_maxf(arcm, 0.0f)) return;
    for (i = 0; i < BELL_OFF_Q; i++)
        if (!bell_slot[i].active) { has_free = 1; break; }
    if (has_free) bell_voice(l, n + 12, vel * 0.4f);
}

/* ---------------------------------------------------------------- pulse --- */
static const uint8_t DRUM_ROT[EN_NDRUM] = { 0, 2, 3 };
static int drum_k[EN_NDRUM], drum_kb[EN_NDRUM], drum_rot[EN_NDRUM], pulse_rest;

EN_COLD static void drum_publish(int d) { drum_pat[d] = en_euclid(drum_k[d], EN_DRUM_N[d], drum_rot[d]); en_drum_pat[d] = drum_pat[d]; }

EN_COLD static void drums_pattern(void)
{
    int d;
    for (d = 0; d < EN_NDRUM; d++) {
        drum_kb[d] = drum_k[d] = (par[P_HITS] * DRUM_KMAX[d] + 50) / 100;
        drum_rot[d] = DRUM_ROT[d];
        drum_publish(d);
    }
}

EN_COLD static void drums_evolve(void)
{
    int d = irand(EN_NDRUM), n = EN_DRUM_N[d];
    if (!drum_kb[d]) return;
    if (frand() < 0.5f) drum_rot[d] = (drum_rot[d] + (frand() < 0.5f ? 1 : n - 1)) % n;
    else {
        int k = drum_k[d] + (frand() < 0.5f ? 1 : -1);
        if (k >= drum_kb[d] - 1 && k <= drum_kb[d] + 1 && k >= 1 && k <= n) drum_k[d] = k;
    }
    drum_publish(d);
}

EN_COLD static void drum_fire(int d, float vel)
{
    drum_t* v = &drum[d];
    v->on = 1;
    v->amp = vel;
    v->att = 0.0f;
    switch (d) {
    case 0: {
        const float f = 440.0f * fm_exp2f((36.0f - 69.0f) * (1.0f / 12.0f));
        v->note = DRUM_GM[0];
        v->inc = f * 2.4f * (1.0f / EN_SR); // higher start = more snap
        v->inc_end = f * (1.0f / EN_SR);
        v->adec = fm_expf(-6.9f / (0.35f * EN_SR)); // shorter body 
        break;
    }
    case 1:
        v->note = DRUM_GM[1];
        v->adec = fm_expf(-6.9f / (0.06f * EN_SR));
        drum[2].adec = fm_expf(-6.9f / (0.02f * EN_SR));
        break;
    default:
        v->note = DRUM_GM[2];
        v->adec = fm_expf(-6.9f / (hat_open * EN_SR));
        break;
    }
    en_drum_glow[d] = 1.0f;
    midi(0x99, v->note, (uint32_t)(vel * 100.0f));
    midi(0x89, v->note, 0);
}

EN_COLD static void drum_hit(int d, float vel, int off)
{
    int32_t w = off ? (int32_t)(swing * step_len) : 0;
    if (w > 0) { drum[d].wait = w; drum[d].pend = vel; }
    else drum_fire(d, vel);
}

EN_COLD static int32_t drums_wait(void)
{
    int32_t m = 0;
    int d;
    for (d = 0; d < EN_NDRUM; d++) if (drum[d].wait > 0 && (!m || drum[d].wait < m)) m = drum[d].wait;
    return m;
}

EN_COLD static void drums_wait_step(int32_t k)
{
    int d;
    for (d = 0; d < EN_NDRUM; d++)
        if (drum[d].wait > 0 && (drum[d].wait -= k) <= 0) { drum[d].wait = 0; drum_fire(d, drum[d].pend); }
}

static uint32_t sixteenth;
EN_COLD static void tick(void)
{
    int d, l;
    const float pa = fm_clampf(1.0f + 1.3f * arcm, 0.3f, 1.15f);
    if ((sixteenth & 15u) == 0u) {
        if (pulse_rest > 0) pulse_rest--;
        else if (frand() < evolve * 0.015f) pulse_rest = 2 + irand(3);
        en_pulse_rest = (uint8_t)(pulse_rest > 0);
        if (frand() < evolve * 0.25f) drums_evolve();
    }
    for (d = 0; d < EN_NDRUM; d++) {
        if (drum_pat[d] >> drum_pos[d] & 1u && !pulse_rest)
            drum_hit(d, ((drum_pos[d] == 0 ? 0.95f : 0.6f) + 0.25f * frand()) * pa, (int)(sixteenth & 1u));
        en_drum_pos[d] = drum_pos[d];
        if (++drum_pos[d] >= EN_DRUM_N[d]) drum_pos[d] = 0;
    }
    /* one loop per sixteenth: at quarter note = 4 sixteenths, the four loops
     * fire one after the other across the quarter instead of all on the same
     * sample. Each loop still advances once per quarter, so the loop lengths
     * (13, 17, 23, 29) keep their slow drift against each other. */
    l = (int)(sixteenth & 3u);
    if (l < EN_NLOOP) {
        int s = loop_pos[l];
        if (loop_note[l][s] >= 0 && !loop_rest[l]) bell_ring(l, s);
        en_loop_pos[l] = (uint8_t)s;
        if (++loop_pos[l] >= EN_LOOP_LEN[l]) { loop_pos[l] = 0; loop_pass(l); }
    }
    if ((sixteenth & 15u) == 0u && change_pending && !en_frozen) { change_pending = 0; chord_change(); }
    sixteenth++;
    en_bars = sixteenth;
}

/* ------------------------------------------------------------ parameters --- */
static int pulse;
static uint32_t ext_age;
EN_COLD static void tempo_set(void) { step_len = EN_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

/* retract the pad's currently-issued notes on ch 0 */
EN_COLD static void pad_all_off(void)
{
    int i;
    for (i = 0; i < EN_NPAD; i++) {
        if (pad[i].midi_note) midi(0x80, pad[i].midi_note, 0);
        pad[i].midi_note = 0;
        pad[i].on = 0;
    }
}
/* retract the bell slots' sounding notes on ch 1 */
EN_COLD static void bell_all_off(void)
{
    int i;
    for (i = 0; i < BELL_OFF_Q; i++) {
        if (bell_slot[i].active)
            midi(0x80 | (uint32_t)(bell_slot[i].loop + 1), bell_slot[i].note, 0);
        bell_slot[i].active = 0;
    }
}

EN_COLD static void apply(int p, int v)
{
    const en_param_t* pi = &PARAMS[p];
    int old;
    if (v < pi->lo) v = pi->lo;
    if (v > pi->hi) v = pi->hi;
    old = par[p];
    par[p] = (int16_t)v;
    switch (p) {
    case P_PAD:
        /* level 0: retract the pad's notes; 0 -> on: sing now instead of
         * waiting up to a full breath cycle (up to ~15 s with Swell 100) */
        if (v == 0)         pad_all_off();
        else if (old == 0)  wake_silent_pads();
        break;
    case P_BELLS:
        if (v == 0) bell_all_off();   /* level 0: no bell notes reach Dexed 1 */
        break;
    case P_BRIGHT:  break;
    case P_BTONE:   break;
    case P_BASS:    bass_vol = (float)(v * v) * 0.0001f; break;
    case P_PULSE:   pulse_vol = (float)(v * v) * 0.0001f; break;
    case P_HITS:    drums_pattern(); break;
    case P_BASSOCT: if (nsteps) bass_to(bass.midi_note % 12); break;
    case P_GLIDE:   glide_k = 1.0f / ((0.02f + 0.012f * (float)v) * EN_SR); break;
    case P_SWING:   swing = (float)v * 0.005f; break;
    case P_HATS:
        hat_f = 0.55f + 0.0070f * (float)v;
        hat_open = 0.6f - 0.0035f * (float)v;
        break;
    case P_DRIFT:
        if (drift_total > 0.0f) {
            float frac = drift_left / drift_total;
            drift_total = drift_secs(v) * EN_SR;
            drift_left = frac * drift_total;
        }
        break;
    case P_ECHO:   echo_send = (float)(v * v) * 0.0001f * 0.2f; break;
    case P_ETIME:  break;
    case P_REVERB: rev_send = (float)(v * v) * 0.0002f; break;
    case P_SIZE:   break;
    case P_SWELL:  swell = (float)v; break;
    case P_EVOLVE: evolve = (float)v * 0.01f; break;
    case P_PADTONE: pad_handoff(); break;
    case P_MOTION: motion = (float)v * 0.01f; break;
    case P_BOCT:
        if (en_playing && nsteps) {
            int l, k;
            for (l = 0; l < EN_NLOOP; l++)
                for (k = 0; k < EN_LOOP_LEN[l]; k++)
                    if (loop_note[l][k] >= 0) { bell_ring(l, k); return; }
        }
        break;
    case P_TUNE:   tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    case P_TEMPO:  tempo_set(); break;
    case P_ROOT:
        if (nsteps) { pad_retarget(); pad_handoff(); bass_to(chord_pcs[0]); }
        break;
    case P_MIDI:
        if (!v) { pad_all_off(); bell_all_off(); }
        break;
    default: break;
    }
}

EN_COLD static void pool_set(uint32_t p)
{
    pool = (p | 1u) & 0xFFFu;
    scale_set();
    if (deg >= nsteps) deg = 0;
    pad_retarget();
    pad_handoff();
    bass_to(chord_pcs[0]);
}

EN_COLD static void regrow(void)
{
    int l;
    for (l = 0; l < EN_NLOOP; l++) { loop_regrow(l); loop_rest[l] = 0; }
    deg = 0;
    pad_retarget();
    pad_handoff();
    bass_to(chord_pcs[0]);
    bass.rest = 0;
    pulse_rest = 0;
    en_pulse_rest = 0;
    drums_pattern();
    drift_reset();
    change_pending = 0;
}

EN_COLD static void notes_off(void)
{
    /* the pad and bell voices on channels 0 and 1 are the only ones the impl
     * forwards; retract everything, and clear the internal "on the wire"
     * state so the next start finds the pad voices silent and restarts them */
    pad_all_off();
    bell_all_off();
}

EN_COLD static void quiet(void)
{
    int i;
    for (i = 0; i < EN_NPAD; i++) pad[i].on = 0;
    for (i = 0; i < BELL_OFF_Q; i++) bell_slot[i].active = 0;
    for (i = 0; i < EN_NDRUM; i++) { drum[i].on = 0; drum[i].wait = 0; }
    bass.amp = 0.0f;
    notes_off();
}

EN_COLD void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++) sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    q_r = q_w = 0;
    for (i = 0; i < NMOD; i++) { mod[i].v = mod[i].tgt = 0.0f; mod[i].left = 0; }
    for (i = 0; i < EN_NPAD; i++) {
        pad[i].note = pad[i].target = (float)(50 + 7 * i);
        pad[i].t = 0.0f;
        pad[i].rate = 0.001f;
        pad[i].peak = 1.0f;
        pad[i].midi_note = 0;
        pad[i].hf = 1.0f;
        pad[i].hand = 0;
        pad[i].on = 0;
        pad[i].voices = 0;
    }
    for (i = 0; i < BELL_OFF_Q; i++) {
        bell_slot[i].active = 0;
        bell_slot[i].off_at = 0;
        bell_slot[i].note = 0;
        bell_slot[i].vel = 0;
    }
    bell_sample_count = 0;
    arcm = 0.0f;
    for (i = 0; i < P_NPARAMS; i++) apply(i, PARAMS[i].def);
    pool_set(EN_MODE_POOL[par[P_MODE]]);
    for (i = 0; i < EN_NPAD; i++) pad[i].note = pad[i].target;
    bass.note = bass.target;
    for (i = 0; i < EN_NLOOP; i++) { loop_pos[i] = 0; loop_regrow(i); }
    drums_pattern();
    drift_reset();
}

/* ------------------------------------------------------------ transport --- */
EN_COLD static void clock_pulse(void)
{
    if (pulse == 0) tick();
    if (!en_ext && par[P_MIDI]) midi(0xF8, 0, 0);   /* P_MIDI gates the clock out */
    if (++pulse >= 6) pulse = 0;
}

EN_COLD static void play_start(int from_top)
{
    int i;
    if (from_top) {
        notes_off();                             /* clear any stale notes */
        pulse = 0;
        sixteenth = 0;
        for (i = 0; i < EN_NDRUM; i++) drum_pos[i] = 0;
        for (i = 0; i < EN_NLOOP; i++) loop_pos[i] = 0;
    }
    pulse_left = 0.0f;
    en_playing = 1;
    /* every voice that isn't currently sounding starts now, staggered by
     * START_T[]; on a fresh start notes_off() cleared them all, so they all
     * start; on a continue only the silent ones do */
    wake_silent_pads();
    if (bass.amp < 1e-4f) bass.note = bass.target;
    midi(0x95, bass.midi_note, 80);          // was 0x92
    if (!en_ext) midi(from_top ? 0xFA : 0xFB, 0, 0);
}

EN_COLD static void play_stop(void)
{
    int d;
    for (d = 0; d < EN_NDRUM; d++) drum[d].wait = 0;
    en_playing = 0;
    if (!en_ext) midi(0xFC, 0, 0);
    notes_off();
}

EN_COLD static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET: if (a < P_NPARAMS) apply(a, b); break;
        case C_POOL: pool_set((uint32_t)b & 0xFFFu); break;
        case C_PLAY:
            if (a && !en_playing) play_start(1);
            else if (!a && en_playing) play_stop();
            break;
        case C_NEXT:
            if (en_playing) change_pending = 1;
            else chord_change();
            break;
        case C_FREEZE: en_frozen = (uint8_t)(a != 0); break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng) rng = 1u;
            break;
        case C_CLOCK:
            en_ext = 1;
            ext_age = 0;
            if (a == EN_CLK_TICK) { if (en_playing) clock_pulse(); }
            else if (a == EN_CLK_START) { play_start(1); }
            else if (a == EN_CLK_CONTINUE) { if (!en_playing) play_start(0); }
            else if (en_playing) { play_stop(); }
            break;
        case C_PANIC: quiet(); break;
        case C_REGROW: regrow(); break;
        }
    }
}

/* --------------------------------------------------------------- render --- */
#define BLK 128
static float mixl[BLK] EN_RAM2, mixr[BLK] EN_RAM2, rsend[BLK] EN_RAM2, esend[BLK] EN_RAM2;

/* The pad renderer only advances the breath state machine (which emits the
 * MIDI notes) and feeds the UI indicators. */
EN_COLD static void render_pad(uint32_t n)
{
    int i;
    float lvl = 0.0f;
    uint32_t j;
    for (i = 0; i < EN_NPAD; i++) {
        pad_t* v = &pad[i];
        for (j = 0; j < n; j++) if (!(j & 31u)) pad_coefs(v, i);
        {
            float amp_ui = breath_env(v->t) * v->peak * v->hf;
            en_pad_note[i] = (uint8_t)v->note;
            en_pad_amp[i] = amp_ui;
            lvl = fm_maxf(lvl, amp_ui);
        }
    }
    en_pad_level = lvl;
}

/* The bell renderer only fires the scheduled note-offs. */
EN_COLD static void render_bells(uint32_t n)
{
    int i;
    bell_sample_count += n;
    for (i = 0; i < BELL_OFF_Q; i++) {
        if (bell_slot[i].active && bell_slot[i].off_at <= bell_sample_count) {
            midi(0x80 | (uint32_t)(bell_slot[i].loop + 1), bell_slot[i].note, 0);   /* was 0x81 */
            bell_slot[i].active = 0;
        }
    }
}

EN_COLD static void render_bass(uint32_t n)
{
    uint32_t j;
    const float g = bass_vol * 0.42f, tgt = en_playing && !bass.rest ? 1.0f : 0.0f,
        rate = tgt > 0.0f ? 1.0f / (2.5f * EN_SR) : 1.0f / (4.0f * EN_SR);
    float ph = bass.ph, amp = bass.amp, inc;
    if (amp < 1e-5f && !en_playing) { bass.amp = 0.0f; en_bass_level = 0.0f; return; }
    bass.note += (bass.target - bass.note) * fm_minf((float)n * glide_k, 1.0f);
    inc = mtof(bass.note) * (1.0f / EN_SR);
    for (j = 0; j < n; j++) {
        float x = sine(ph) * 0.75f + sine(ph * 0.5f + 0.25f) * 0.45f + sine(ph * 2.0f) * 0.12f;
        ph += inc;
        if (ph >= 2.0f) ph -= 2.0f;
        amp += (tgt - amp) * rate;
        x *= amp * g;
        mixl[j] += x;
        mixr[j] += x;
        rsend[j] += x * 0.08f;
    }
    bass.ph = ph;
    bass.amp = amp;
    en_bass_level = amp;
}

EN_COLD static void render_hat(drum_t* v, uint32_t n, float g, float pl, float pr)
{
    uint32_t j;
    int k;
    float x1, x2, amp, att;
    if (!v->on) return;
    x1 = v->x1; x2 = v->x2; amp = v->amp; att = v->att;
    for (j = 0; j < n; j++) {
        float in = 0.0f, hp, x;
        for (k = 0; k < 4; k++) {
            v->mph[k] += HAT_INC[k];
            if (v->mph[k] >= 1.0f) v->mph[k] -= 1.0f;
            in += v->mph[k] < 0.5f ? 0.25f : -0.25f;
        }
        noise = noise * 1664525u + 1013904223u;
        in = 0.5f * in + 0.6f * (float)(int32_t)noise * (1.0f / 2147483648.0f);
        hp = in - x2 - 0.8f * x1;
        x1 += hat_f * hp;
        x2 += hat_f * x1;
        x = hp * amp * att * g;
        mixl[j] += x * pl;
        mixr[j] += x * pr;
        rsend[j] += x * 0.2f;
        esend[j] += x * 0.08f;
        amp *= v->adec;
        att += (1.0f - att) * 0.3f;
    }
    v->x1 = fm_flush(x1); v->x2 = fm_flush(x2); v->amp = amp; v->att = att;
    v->on = amp > 0.0005f;
}

EN_COLD static void render_drums(uint32_t n)
{
    uint32_t j;
    const float g = pulse_vol * 1.1f;
    drum_t* v = &drum[0];
    if (v->on) {
        float ph = v->ph, inc = v->inc, amp = v->amp, att = v->att;
        for (j = 0; j < n; j++) {
            float x = sine(ph) * amp * att * g;
            mixl[j] += x; mixr[j] += x; rsend[j] += x * 0.1f;
            ph += inc; if (ph >= 1.0f) ph -= 1.0f;
            inc += (v->inc_end - inc) * 0.0016f; // pitch sweep
            amp *= v->adec;
            att += (1.0f - att) * 0.02f;
        }
        v->ph = ph; v->inc = inc; v->amp = amp; v->att = att;
        v->on = amp > 0.0005f;
    }
    render_hat(&drum[1], n, g * 0.2f, 0.6f, 1.0f);
    render_hat(&drum[2], n, g * 0.14f, 1.0f, 0.7f);
}

EN_COLD static void render_chunk(float* l, float* r, float* rev, float* echo, uint32_t n)
{
    uint32_t j;
    const float rg = rev_send * rev_in_gain * (1.0f - 0.25f * arcm) * 0.8f, eg = echo_send;
    for (j = 0; j < n; j++) mixl[j] = mixr[j] = rsend[j] = esend[j] = 0.0f;
    render_pad(n);
    render_bass(n);
    render_bells(n);
    render_drums(n);
    for (j = 0; j < n; j++) {
        l[j] = fm_tanhf(mixl[j] * 0.36f);
        r[j] = fm_tanhf(mixr[j] * 0.36f);
        rev[j] = rsend[j] * rg * 0.36f;
        echo[j] = esend[j] * eg * 0.36f;
    }
}

static float arc, arc_tgt;
static uint32_t arc_left;

EN_COLD static void control(uint32_t n)
{
    int i;
    mods_step(n);
    if (arc_left <= n) {
        float r = frand() * 2.0f - 1.0f;
        arc_tgt = r * (0.6f + 0.4f * frand()) + (r > 0.0f ? 0.25f : -0.25f);
        arc_left = (uint32_t)((50.0f + 100.0f * frand()) * EN_SR);
    }
    else arc_left -= n;
    if (en_playing) arc += (fm_clampf(arc_tgt, -1.0f, 1.0f) - arc) * (float)n * (1.0f / (35.0f * EN_SR));
    arcm = arc * motion;
    en_arc = arcm;
    rev_in_gain += ((en_frozen ? 0.0f : 1.0f) - rev_in_gain) * 0.01f;
    if (en_playing && !en_frozen && !change_pending) {
        drift_left -= (float)n;
        if (!fifth_done && drift_left < 0.5f * drift_total) {
            fifth_done = 1;
            if (frand() < (float)par[P_FIFTH] * 0.01f && chord_nt >= 3) bass_to(chord_pcs[2]);
        }
        if (drift_left <= 0.0f) change_pending = 1;
    }
    en_drift = drift_total > 0.0f ? 1.0f - fm_clampf(drift_left / drift_total, 0.0f, 1.0f) : 0.0f;
    for (i = 0; i < EN_NLOOP; i++) en_loop_glow[i] *= 0.985f;
    for (i = 0; i < EN_NDRUM; i++) en_drum_glow[i] *= 0.95f;
}

EN_COLD void en_render_split(float* l, float* r, float* rev, float* echo, uint32_t n)
{
    drain();
    if (en_ext && ++ext_age > 100u) en_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        int timed = en_playing && !en_ext;
        if (timed) {
            if (pulse_left <= 0.0f) { clock_pulse(); pulse_left += step_len * (1.0f / 6.0f); }
            if ((float)k > pulse_left) k = (uint32_t)pulse_left + 1u;
        }
        if (k > n) k = n;
        {
            int32_t w = drums_wait();
            if (w > 0 && (uint32_t)w < k) k = (uint32_t)w;
        }
        if (timed) pulse_left -= (float)k;
        control(k);
        render_chunk(l, r, rev, echo, k);
        drums_wait_step((int32_t)k);
        l += k; r += k; rev += k; echo += k; n -= k;
    }
}