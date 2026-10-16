/*
   MicroDexed-touch - Ambient Garden (implementation)

   The garden is AMB-1's own instrument (amb.c / amb.h, X0X), running in MDT's
   audio graph as one AudioStream object (AudioSynthAmbientGarden). The pad and
   bell layers no longer synthesize audio locally: they emit MIDI note on/off
   messages on their own status-byte channels and en_midi_out() below catches
   those and routes them to MDT's instruments.

   Channel map (pad/bells/bass/drums all in amb.c):
     0    pad    -> Braids          (single instance)
     1..4 bell   -> Dexed 0..3      (loops 0..3 of the four tape loops)
     5    bass   -> dropped         (already synthesized in the engine)
     9    drums  -> dropped         (already synthesized in the engine)

   --- Master volume ---
   The garden does not jump to UI_func_volume. Turning the left encoder while
   the garden page is active calls ambient_garden_adjust_volume() (see the
   interception in lcdml_menu_control() in UI.cpp), which adjusts
   configuration.sys.vol, applies it via set_volume(), and prints the new
   level on the row below the BPM readout on the top-right of the display.
   The garden never leaves its own page for a volume change, so there is no
   resume path here.

   The encoder can only *deactivate* the multiband compressor (when the
   volume drops below 100, so set_volume() has an audible effect). It never
   activates it: only the COMP touch button does.

   --- Bells: multiple Dexed instances ---
   With NUM_DEXED >= 4 (PSRAM enabled), each bell loop is played by its own
   Dexed instance with its own pan, taken from the reference instrument's
   per-loop LOOP_PAN = {-0.6, 0.35, 0.7, -0.25}.
   With NUM_DEXED == 2, the four loops fold onto the two instances.
   With NUM_DEXED == 1, a single instance is used and centred.

   --- Bell preset voices ---
   The bell voice loaded into every borrowed Dexed is one of five selectable
   preset slots (bank/voice pairs). The active preset is selectable on the
   garden page and stored with the rest of the garden state in
   /ambient_state.json. The voice NAME is not stored here -
   load_sd_voice() fills g_voice_name[i] from the SD card.

   The state file also stores the *currently loaded* bell bank/voice, so
   that a manual override entered via the top-line BANK / VOICE editors
   survives a page close/open just like a preset selection does.

   The impl borrows AG_NBELLDEX Dexed instances on entry: it saves their pool,
   bank, voice, op_enabled mask and pan, sets the garden's preset on all of
   them, forces them to distinct MIDI channels that don't collide with
   Braids, and hands everything back on exit. Dexed instances at indices
   >= AG_NBELLDEX have their MIDI channels silenced on entry and restored on
   exit as before.

   *** This file is #included at the VERY END of UI.cpp. ***

   Threads: the engine renders in the audio interrupt. en_midi_out() is called
   from the ISR for the pad/bell notes; it pushes into a lock-free ring which
   the sequencer tick drains from the main loop.
*/

#include "ambient_garden.h"
#include "amb.h"
#include "fastmath.h"
#include "sequencer.h"
#include "touch.h"
#include "midi_devices.h"
#include <math.h>
#include <ArduinoJson.h>
#include <SD.h>

// ============================================================================
// External MDT state
// ============================================================================
extern config_t                 configuration;
extern sequencer_t              seq;

extern AudioEffectPlateReverb               reverb;
extern AudioMixer<REVERB_MIX_CH_MAX>        reverb_mixer_r;
extern AudioMixer<REVERB_MIX_CH_MAX>        reverb_mixer_l;
extern AudioMixer<MASTER_MIX_CH_MAX>        master_mixer_r;
extern AudioMixer<MASTER_MIX_CH_MAX>        master_mixer_l;
extern AudioMixer<DELAY_MIX_CH_MAX>* global_delay_in_mixer[2];
extern AudioMixer<2>* delay_fb_mixer[NUM_DEXED];
extern AudioEffectMonoStereo* delay_mono2stereo[NUM_DEXED];
extern AudioEffectMonoStereo* dexed_mono2stereo[NUM_DEXED];
#ifdef PSRAM
extern AudioEffectDelayExternal8* delay_fx[2];
#else
extern AudioEffectDelay* delay_fx[2];
#endif
extern void set_fx_params(void);
extern void set_global_delay_filter(uint8_t instance);
extern void set_sidechain_levels(void);
extern void update_seq_speed(void);
extern void set_volume(uint8_t v, uint8_t m);
extern float volume_transform(float amp);
extern float midi_volume_transform(uint8_t midi_amp);
extern void send_midi_clock(void);
extern void set_dexed_delay_level(uint8_t delay_no, uint8_t instance, uint8_t level);

extern elapsedMillis save_sys;
extern bool          save_sys_flag;

extern char g_voice_name[NUM_DEXED][VOICE_NAME_LEN];
extern char g_bank_name[NUM_DEXED][BANK_NAME_LEN];
void loadDexedOrigADSR(int instance);

extern braids_t                    braids_osc;
extern AudioEffectEnvelope* braids_envelope[NUM_BRAIDS];
extern AudioMixer<2>               braids_mixer_reverb;
extern AudioSynthDexed* MicroDexed[NUM_DEXED];
extern void                        braids_update_all_settings(void);
extern void                        update_braids_volume(void);
extern bool                        load_sd_voice(uint8_t pool, uint8_t bank, uint8_t voice, uint8_t instance_id);

extern microsynth_t                microsynth[NUM_MICROSYNTH];
extern multisample_s               msp[NUM_MULTISAMPLES];
extern uint8_t                     drum_midi_channel;
extern uint8_t                     slices_midi_channel;
#ifdef GRANULAR
extern granular_params_t           granular_params;
#endif

void handleNoteOn(byte inChannel, byte inNumber, byte inVelocity, byte device);
void handleNoteOff(byte inChannel, byte inNumber, byte inVelocity, byte device);
void handleControlChange(byte inChannel, byte inCtrl, byte inValue);

// ============================================================================
// Settings
// ============================================================================
// LOUDNESS: One global multiplier applied to every garden signal path
// (engine dry, pad dry, bells dry, and — critically — to the reverb sends of
// the pad and bells as well). Because it is applied *before* the sends split
// off, the wet/dry ratio, the plate character, and the relative levels of
// pad / bells / bass / pulse stay exactly as they were designed.
//
// 3.0f is roughly +9.5 dB. Raise it if you want more, lower it if it feels
// too compressed. If you go very high, back the master volume down first —
// the final stereo2mono/volume_l/r stages downstream do clip.
#define AG_LOUDNESS       3.0f

// AG_ENGINE_GAIN: applied inside AudioSynthAmbientGarden::update() to the
// four outputs of the engine (dry L, dry R, reverb send, echo send). The
// original 2.0f is kept; the extra boost comes from AG_LOUDNESS here as well
// so the engine and the MIDI layers stay proportional.
#define AG_ENGINE_GAIN    (2.0f * AG_LOUDNESS)

#define AG_REV_SEND_COMP  4.0f
#define AG_UI_FRAME_MS    40

#define AG_REV_RETURN     VOL_MAX_FLOAT
#define AG_TAPE_IN_GAIN   0.75f
#define AG_PLEX_IN_GAIN   0.30f

#define AG_REV_HIDAMP     0
#define AG_REV_LODAMP     60
#define AG_REV_LOWPASS    80
#define AG_REV_DIFFUSION  100
#define AG_REV_LEVEL      100

#define AG_PLEX_MS        160.0f
#define AG_PLEX_FB        0.35f
#define AG_DLY_FILTER_MODE 1
#define AG_DLY_FILTER_FREQ 50
#define AG_DLY_PAN        20

#define AG_PAD_SEND_REV     70
#define AG_PAD_SEND_DLY_MAX 0.12f

#define AG_TITLE_Y     0

#define AG_TOP_BELL_X   CHAR_width_small*16
#define AG_TOP_BANK_X   CHAR_width_small*23
#define AG_TOP_VOICE_X  CHAR_width_small*28
#define AG_TOP_NAME_X   CHAR_width_small*33
#define AG_TOP_NAME_CHARS 11   /* 10 name chars + 1 trailing space */
#define AG_TOP_TEMPO_X  CHAR_width_small*46

/* Master volume readout: on the row below the BPM display, right-aligned
 * to the same column so "VOL:100" sits cleanly under " 72 BPM". */
#define AG_TOP_VOLUME_X AG_TOP_TEMPO_X
#define AG_TOP_VOLUME_Y (AG_TITLE_Y + 10)

 /* Editor indices in the garden's ui stack, matching the order of the
  * addEditor() calls in ag_build_page_ui(). Used by renderers and setters
  * that need to test the current selection or repaint the correct one. */
#define AG_EDIT_BELL    0
#define AG_EDIT_BANK    1
#define AG_EDIT_VOICE   2
#define AG_EDIT_TEMPO   3
#define AG_EDIT_PIECE   4

#if NUM_DEXED >= 4
#define AG_NBELLDEX 4
static const float AG_INST_PAN[4] = { -0.6f, 0.35f, 0.7f, -0.25f };
#elif NUM_DEXED >= 2
#define AG_NBELLDEX 2
static const float AG_INST_PAN[4] = { -0.5f, 0.5f, 0.0f, 0.0f };
#else
#define AG_NBELLDEX 1
static const float AG_INST_PAN[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
#endif

/* Bell preset voices: bank / voice pairs loaded into every borrowed Dexed
 * when the garden page opens. The voice NAME is not stored here -
 * load_sd_voice() fills g_voice_name[i] from the SD card.
 *
 * Bank 72 entries share one folder; bank 78 and bank 1 entries visibly
 * move the "B:xx" indicator when you cycle past them. */
static const struct {
    uint8_t bank;
    uint8_t voice;
} AG_BELL_PRESETS[AG_NBELLPRESETS] = {
    { 72,  7 },
    { 72, 13 },
    { 72, 14 },
    { 78, 10 },
    {  79, 19 },
    {  80, 2 },
    {  83, 23 }
};

static int ag_bell_preset = 0;

/* Persisted bell voice. Set by ag_load_state_from_sd() from the SD file
 * and consumed by the cold setup path. It carries whichever bank/voice
 * was loaded last, whether that came from a preset or from the user
 * typing a value into the BANK / VOICE editors on the top row. */
static uint8_t ag_saved_bellbank = 0;
static uint8_t ag_saved_bellvoice = 0;
static bool    ag_saved_bell_voice_valid = false;
static bool    ag_saved_multiband_on = false;


static const float AG_ETIME_BEATS[6] = { 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f };

static int16_t  ag_par[P_NPARAMS];
static uint16_t ag_pool = 0xAB5;
static int      ag_piece = 0;
static volatile bool ag_active = false;
static uint8_t  ag_subtick = 0;

static int      ag_chord_pcs[4] = { 0 };
static int      ag_chord_nt = 0;
static float    ag_meter[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

#define AG_NLOOP      EN_NLOOP
#define AG_LOOP_MAX   EN_LOOP_MAX
#define AG_NDRUM      EN_NDRUM
#define AG_LOOP_LEN   EN_LOOP_LEN
#define AG_DRUM_N     EN_DRUM_N
static const char* const* const AG_NOTE_NAME = EN_NOTE_NAME;

static inline uint8_t ag_pan_from_float(float p)
{
    int v = (int)((p + 1.0f) * 0.5f * (float)(PANORAMA_MAX - PANORAMA_MIN)
        + PANORAMA_MIN + 0.5f);
    if (v < PANORAMA_MIN) v = PANORAMA_MIN;
    if (v > PANORAMA_MAX) v = PANORAMA_MAX;
    return (uint8_t)v;
}

// ============================================================================
// The engine in MDT's audio graph
// ============================================================================
extern AudioSynthAmbientGarden ambient_garden_engine;
#define ag_engine ambient_garden_engine

static float ag_buf_l[AUDIO_BLOCK_SAMPLES] DMAMEM;
static float ag_buf_r[AUDIO_BLOCK_SAMPLES] DMAMEM;
static float ag_buf_rv[AUDIO_BLOCK_SAMPLES] DMAMEM;
static float ag_buf_ec[AUDIO_BLOCK_SAMPLES] DMAMEM;

static inline int16_t ag_s16(float x)
{
    x *= 32767.0f;
    return (int16_t)(x > 32767.0f ? 32767.0f : (x < -32767.0f ? -32767.0f : x));
}

FLASHMEM static void ag_pad_mixer_tick_audio(void);

void AudioSynthAmbientGarden::update(void)
{
    if (!running) return;
    audio_block_t* b[4];
    for (int k = 0; k < 4; k++) {
        b[k] = allocate();
        if (!b[k]) {
            for (int m = 0; m < k; m++) release(b[m]);
            return;
        }
    }
    en_render_split(ag_buf_l, ag_buf_r, ag_buf_rv, ag_buf_ec, AUDIO_BLOCK_SAMPLES);
    ag_pad_mixer_tick_audio();

    // LOUDNESS: AG_ENGINE_GAIN now carries the AG_LOUDNESS factor as well,
    // so the engine's dry mix, reverb send and echo send all get the same
    // boost that the MIDI layers get. Sending the same boost on all four
    // outputs keeps the internal wet/dry proportions of the engine intact.
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        b[0]->data[i] = ag_s16(ag_buf_l[i] * AG_ENGINE_GAIN);
        b[1]->data[i] = ag_s16(ag_buf_r[i] * AG_ENGINE_GAIN);
        b[2]->data[i] = ag_s16(ag_buf_rv[i] * AG_ENGINE_GAIN * AG_REV_SEND_COMP);
        b[3]->data[i] = ag_s16(ag_buf_ec[i] * AG_ENGINE_GAIN);
    }
    for (int k = 0; k < 4; k++) { transmit(b[k], k); release(b[k]); }
}

// ============================================================================
// Bell voice swap state (functions further down, after ag_draw_top_name)
//
// A bell voice change never loads into a sounding Dexed. The request only
// records the target; ag_bell_swap_tick() fades the bell Dexeds' gain down
// (which also fades their reverb / delay sends), swaps the voice on all
// instances at silence, and fades back in. Bell note-ons are held back
// while a swap is pending. The newest request wins, and the swap waits for
// the encoder to settle, so fast BANK / VOICE scrolling loads only once.
// ============================================================================
#define AG_SW_FADE_OUT_MS 150
#define AG_SW_FADE_IN_MS  300
#define AG_SW_SETTLE_MS    80
// If a bell-voice swap stays pending longer than this (stalled SD read,
// bank never becomes ready, bank never reports failure), give up, abort
// the loader, restore the previous voice and fade back in. Without the
// watchdog ag_sw_pending stays true forever, which silently swallows
// every bell note-on in ag_drain_midi_out() and eventually starves the
// audio ISR.
#define AG_SW_WATCHDOG_MS 8000
#define AG_BELL_DEX_GAIN  0.35f            /* bell Dexed gain at full level */

static bool          ag_sw_pending = false;
static int8_t        ag_sw_preset = -1;   /* >= 0: from cache, -1: bank/voice from SD */
static uint8_t       ag_sw_bank = 0;
static uint8_t       ag_sw_voice = 0;
static float         ag_sw_level = 1.0f; /* 0..1 */
static elapsedMillis ag_sw_dt;
static elapsedMillis ag_sw_req_age;

// ============================================================================
// MIDI out: lock-free ring, drained from the main loop
// ============================================================================
// ============================================================================
// MIDI out: lock-free ring, drained from the main loop and from
// ambient_garden_tick()
//
// Ring raised from 64 to 256 entries (768 bytes). en_midi_out() now
// purges an older pending note-on for the same (channel, note) when the
// ring is full, so a stuck voice can never be orphaned by a full queue.
// The drain now copies each message out with interrupts briefly masked
// so a concurrent purge cannot corrupt a partially-read slot.
// ============================================================================
#define AG_MIDI_Q 256
static volatile uint32_t ag_mq_w = 0, ag_mq_r = 0;
static uint8_t ag_mq_data[AG_MIDI_Q][3];    /* 768 bytes */

extern "C" void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, primask" : "=r"(primask) :: "memory");
    __asm__ volatile("cpsid i" ::: "memory");

    const uint32_t w = ag_mq_w;
    const uint32_t r = ag_mq_r;

    if (w - r >= AG_MIDI_Q) {
        // Ring is full. If we are about to push a note-on and an older
        // note-on for the same (channel, note) is still pending, turn
        // that older one into a note-off so the invariant "every
        // note-on in the queue has a matching note-off behind it"
        // survives.
        const uint32_t vch    = st & 0x0F;
        const uint32_t status = st & 0xF0;
        if (status == 0x90) {
            for (uint32_t k = r; k != w; k++) {
                const uint32_t idx = k % AG_MIDI_Q;
                const uint32_t s2  = ag_mq_data[idx][0];
                if ((s2 & 0x0F) == vch && (s2 & 0xF0) == 0x90 &&
                    ag_mq_data[idx][1] == d1) {
                    ag_mq_data[idx][0] = (uint8_t)(0x80u | vch);
                    ag_mq_data[idx][2] = 0;
                }
            }
        }
    }

    if (w - r < AG_MIDI_Q) {
        const uint32_t idx = w % AG_MIDI_Q;
        ag_mq_data[idx][0] = (uint8_t)st;
        ag_mq_data[idx][1] = (uint8_t)d1;
        ag_mq_data[idx][2] = (uint8_t)d2;
        __asm__ volatile("" ::: "memory");
        ag_mq_w = w + 1;
    }
    // else: still full. Drop the newest message. With Q=256 and the
    // periodic ambient_garden_tick() drain this is effectively
    // unreachable in normal play.

    if (!(primask & 1u))
        __asm__ volatile("cpsie i" ::: "memory");
}

FLASHMEM static void ag_drain_midi_out(void)
{
    static bool warned_channel_collision = false;

    while (true) {
        uint32_t st, d1, d2;

        // Copy the message out atomically so a concurrent purge in
        // en_midi_out() cannot corrupt a slot we are about to process.
        {
            uint32_t primask;
            __asm__ volatile("mrs %0, primask" : "=r"(primask) :: "memory");
            __asm__ volatile("cpsid i" ::: "memory");

            const uint32_t r = ag_mq_r;
            if (r == ag_mq_w) {
                if (!(primask & 1u))
                    __asm__ volatile("cpsie i" ::: "memory");
                break;
            }
            const uint32_t idx = r % AG_MIDI_Q;
            st = ag_mq_data[idx][0];
            d1 = ag_mq_data[idx][1];
            d2 = ag_mq_data[idx][2];
            __asm__ volatile("" ::: "memory");
            ag_mq_r = r + 1;

            if (!(primask & 1u))
                __asm__ volatile("cpsie i" ::: "memory");
        }

        const uint32_t status = st & 0xF0u;
        const uint32_t vch    = st & 0x0Fu;

        if (vch == 0u) {
            uint8_t ch = braids_osc.midi_channel;
            if (ch == MIDI_CHANNEL_OFF) continue;

            if (!warned_channel_collision) {
                for (int i = 0; i < AG_NBELLDEX; i++) {
                    if (configuration.dexed[i].midi_channel == ch) {
                        warned_channel_collision = true;
#ifdef DEBUG
                        LOG.print(F("Ambient Garden: Braids and a bell Dexed share MIDI ch "));
                        LOG.println(ch);
#endif
                        break;
                    }
                }
            }

            if (status == 0x90u)      handleNoteOn(ch, (uint8_t)d1, (uint8_t)d2, 0);
            else if (status == 0x80u) handleNoteOff(ch, (uint8_t)d1, (uint8_t)d2, 0);
        }
        else if (vch >= 1u && vch <= 4u) {
            int loop = (int)(vch - 1);
            int inst = loop % AG_NBELLDEX;
            uint8_t ch = configuration.dexed[inst].midi_channel;
            if (ch == MIDI_CHANNEL_OFF) continue;
            if (ch == braids_osc.midi_channel) continue;
            if (ag_sw_pending && status == 0x90u) continue;   /* voice swap in progress */

            if (status == 0x90u)      handleNoteOn(ch, (uint8_t)d1, (uint8_t)d2, 0);
            else if (status == 0x80u) handleNoteOff(ch, (uint8_t)d1, (uint8_t)d2, 0);
        }
    }
}

// ============================================================================
// Routing: engine outputs into MDT's mixers
// ============================================================================
FLASHMEM static void ag_route(bool open)
{
    const float g = open ? VOL_MAX_FLOAT : 0.0f;
    AudioNoInterrupts();
    master_mixer_l.gain(MASTER_MIX_CH_AMB, g);
    master_mixer_r.gain(MASTER_MIX_CH_AMB, g);
    AudioInterrupts();
}

// ============================================================================
// Saved state (tempo + fx)
// ============================================================================
static struct {
    bool     valid;
    uint32_t bpm;
    uint32_t tempo_ms;
    uint8_t  ticks_max;
    fx_t     fx;
} ag_saved = { false };

FLASHMEM static void ag_save_state(void)
{
    if (ag_saved.valid) return;
    ag_saved.bpm = seq.bpm;
    ag_saved.tempo_ms = seq.tempo_ms;
    ag_saved.ticks_max = seq.ticks_max;
    ag_saved.fx = configuration.fx;
    ag_saved.valid = true;
}

FLASHMEM static void ag_restore_state(void)
{
    if (!ag_saved.valid) return;
    seq.bpm = ag_saved.bpm;
    seq.tempo_ms = ag_saved.tempo_ms;
    seq.ticks_max = ag_saved.ticks_max;
    configuration.fx = ag_saved.fx;

    set_fx_params();
    set_global_delay_filter(0);
    set_global_delay_filter(1);
    AudioNoInterrupts();
    master_mixer_r.gain(MASTER_MIX_CH_REVERB, volume_transform(mapfloat(configuration.fx.reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX, 0.0, VOL_MAX_FLOAT)));
    master_mixer_l.gain(MASTER_MIX_CH_REVERB, volume_transform(mapfloat(configuration.fx.reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX, 0.0, VOL_MAX_FLOAT)));
    AudioInterrupts();
    update_seq_speed();
    ag_saved.valid = false;
}

// ============================================================================
// MDT's effects as the garden's space
// ============================================================================
FLASHMEM static void ag_fx_apply(void)
{
    const float size = (float)ag_par[P_SIZE] * 0.01f;

    const float d = 0.6f + 0.375f * size;
    const float rv = powf(d, 0.609f / 0.725f);
    const int   roomsize = constrain((int)((rv - 0.2f) / 0.75f * 100.0f + 0.5f), 0, 100) * 8;
    configuration.fx.reverb_roomsize = roomsize;
    configuration.fx.reverb_hidamp = AG_REV_HIDAMP;
    configuration.fx.reverb_lodamp = AG_REV_LODAMP;
    configuration.fx.reverb_lowpass = AG_REV_LOWPASS;
    configuration.fx.reverb_diffusion = AG_REV_DIFFUSION;
    configuration.fx.reverb_level = AG_REV_LEVEL;
    AudioNoInterrupts();
    reverb.size(mapfloat(roomsize, REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX, 0.0f, 1.0f));
    reverb.hidamp(mapfloat(AG_REV_HIDAMP, REVERB_HIDAMP_MIN, REVERB_HIDAMP_MAX, 0.0f, 1.0f));
    reverb.lodamp(mapfloat(AG_REV_LODAMP, REVERB_LODAMP_MIN, REVERB_LODAMP_MAX, 0.0f, 1.0f));
    reverb.lowpass(mapfloat(AG_REV_LOWPASS, REVERB_LOWPASS_MIN, REVERB_LOWPASS_MAX, 0.0f, 1.0f));
    reverb.diffusion(mapfloat(AG_REV_DIFFUSION, REVERB_DIFFUSION_MIN, REVERB_DIFFUSION_MAX, 0.0f, 1.0f));
    master_mixer_r.gain(MASTER_MIX_CH_REVERB, AG_REV_RETURN * 1.2);
    master_mixer_l.gain(MASTER_MIX_CH_REVERB, AG_REV_RETURN * 1.2);

    for (int k = 0; k < 2; k++)
        configuration.fx.delay_to_reverb[k] = 1.0;

    reverb_mixer_r.gain(REVERB_MIX_CH_AUX_DELAY1, 1.0);
    reverb_mixer_l.gain(REVERB_MIX_CH_AUX_DELAY1, 1.0);
    reverb_mixer_r.gain(REVERB_MIX_CH_AUX_DELAY2, 1.0);
    reverb_mixer_l.gain(REVERB_MIX_CH_AUX_DELAY2, 1.0);

    AudioInterrupts();

    float beat_ms = 60000.0f / (float)constrain((int)ag_par[P_TEMPO], 30, 120);
    int   i = constrain((int)ag_par[P_ETIME], 0, 5);
    float t = AG_ETIME_BEATS[i] * beat_ms;
    while (t > (float)DELAY_MAX_TIME && i > 0) t = AG_ETIME_BEATS[--i] * beat_ms;
    if (t > (float)DELAY_MAX_TIME) t = (float)DELAY_MAX_TIME;
    const float fb1 = 0.2f + 0.10f * size;

    float t2 = AG_PLEX_MS;
    if (t2 > (float)DELAY_MAX_TIME) t2 = (float)DELAY_MAX_TIME;

    for (int k = 0; k < 2; k++) {
        const float tk = k ? t2 : t;
        const float fbk = k ? AG_PLEX_FB : fb1;
        configuration.fx.delay_sync[k] = 0;
        configuration.fx.delay_time[k] = (uint16_t)(tk / 10.0f);
        configuration.fx.delay_feedback[k] = (uint8_t)(fbk * 100.0f) / 2;
        configuration.fx.delay_filter_mode[k] = AG_DLY_FILTER_MODE;
        configuration.fx.delay_filter_freq[k] = AG_DLY_FILTER_FREQ;
        configuration.fx.delay_pan[k] = AG_DLY_PAN;
        delay_fx[k]->delay(0, tk);
        delay_fb_mixer[k]->gain(1, fbk / 2);
        delay_mono2stereo[k]->panorama(mapfloat(AG_DLY_PAN, PANORAMA_MIN, PANORAMA_MAX, -1.0f, 1.0f));
        set_global_delay_filter(k);
        global_delay_in_mixer[k]->gain(9, 0.0f);
    }
}

// ============================================================================
// Garden patches
// ============================================================================
struct ag_braids_save_t {
    braids_t osc;
    bool     valid;
};
static ag_braids_save_t ag_braids_save = { {}, false };

struct ag_dexed_save_t {
    uint8_t pool, bank, voice;
    uint8_t op_enabled;
    uint8_t pan;
    uint8_t midi_channel;
    bool    valid;
};
static ag_dexed_save_t ag_dexed_save[NUM_DEXED] = { { 0, 0, 0, 0, 0, 0, false } };

FLASHMEM static void ag_braids_save_user_patch(void)
{
    if (ag_braids_save.valid) return;
    ag_braids_save.osc = braids_osc;
    ag_braids_save.valid = true;
}

FLASHMEM static void ag_braids_restore_user_patch(void)
{
    if (!ag_braids_save.valid) return;

    uint8_t keep_channel = braids_osc.midi_channel;
    uint8_t keep_notes[NUM_BRAIDS];
    memcpy(keep_notes, braids_osc.note_buffer, sizeof(keep_notes));

    braids_osc = ag_braids_save.osc;
    braids_osc.midi_channel = keep_channel;
    memcpy(braids_osc.note_buffer, keep_notes, sizeof(keep_notes));

    braids_update_all_settings();
    update_braids_volume();
    ag_braids_save.valid = false;
}

FLASHMEM static void ag_braids_apply_garden_patch(void)
{
    const uint8_t keep_channel = braids_osc.midi_channel;

    braids_osc.algo = 14;    /* SUPERSAW */
    braids_osc.color = 0;
    braids_osc.timbre = 87;
    braids_osc.coarse = 0;
    braids_osc.sound_intensity = 40;

    braids_osc.env_attack = 245;
    braids_osc.env_decay = 97;
    braids_osc.env_sustain = 75;
    braids_osc.env_release = 95;

    braids_osc.filter_mode = 1;
    braids_osc.filter_freq_from = 9500;
    braids_osc.filter_freq_to = 100;
    braids_osc.filter_resonance = 3;
    braids_osc.filter_speed = 1;
    braids_osc.filter_lfo_intensity = 1;
    braids_osc.filter_lfo_speed = 1;

    braids_osc.flanger = 33;
    braids_osc.flanger_spread = 66;
    braids_osc.pan = 0;

    braids_osc.rev_send = AG_PAD_SEND_REV;
    braids_osc.delay_send_1 = 0;
    braids_osc.delay_send_2 = 0;

    braids_osc.midi_channel = keep_channel;

    braids_update_all_settings();
    update_braids_volume();
}

FLASHMEM static void ag_dexed_save_user_voice(void)
{
    for (int i = 0; i < AG_NBELLDEX; i++) {
        if (ag_dexed_save[i].valid) continue;
        ag_dexed_save[i].pool = configuration.dexed[i].pool;
        ag_dexed_save[i].bank = configuration.dexed[i].bank;
        ag_dexed_save[i].voice = configuration.dexed[i].voice;
        ag_dexed_save[i].op_enabled = configuration.dexed[i].op_enabled;
        ag_dexed_save[i].pan = configuration.dexed[i].pan;
        ag_dexed_save[i].midi_channel = configuration.dexed[i].midi_channel;
        ag_dexed_save[i].valid = true;
    }
}

FLASHMEM static void ag_dexed_restore_user_voice(void)
{
    for (int i = 0; i < AG_NBELLDEX; i++) {
        if (!ag_dexed_save[i].valid) continue;
        configuration.dexed[i].pool = ag_dexed_save[i].pool;
        configuration.dexed[i].bank = ag_dexed_save[i].bank;
        configuration.dexed[i].voice = ag_dexed_save[i].voice;
        configuration.dexed[i].pan = ag_dexed_save[i].pan;
        configuration.dexed[i].midi_channel = ag_dexed_save[i].midi_channel;
        load_sd_voice(ag_dexed_save[i].pool, ag_dexed_save[i].bank,
            ag_dexed_save[i].voice, (uint8_t)i);
        configuration.dexed[i].op_enabled = ag_dexed_save[i].op_enabled;
        if (MicroDexed[i])
            MicroDexed[i]->setOPAll(ag_dexed_save[i].op_enabled);
        if (dexed_mono2stereo[i])
            dexed_mono2stereo[i]->panorama(
                mapfloat((float)ag_dexed_save[i].pan,
                    PANORAMA_MIN, PANORAMA_MAX, -1.0f, 1.0f));
        ag_dexed_save[i].valid = false;
    }
}

/* Load the given bank/voice into every borrowed Dexed. Called from setup()
 * on entry and whenever the BELL preset changes. Takes explicit bank/voice
 * so the caller can restore a saved manual override instead of a preset. */
FLASHMEM static void ag_dexed_apply_garden_voice(uint8_t preset_bank, uint8_t preset_voice)
{
    /* Pick AG_NBELLDEX MIDI channels that don't collide with Braids. Channels
     * are 1..16; skip Braids' channel if it falls in that range. */
    const uint8_t braids_ch = braids_osc.midi_channel;
    int cand = 1;

    for (int i = 0; i < AG_NBELLDEX; i++) {
        while (cand <= 16 && cand == (int)braids_ch) cand++;
        if (cand > 16) cand = 1;

        AudioSynthDexed* inst = MicroDexed[i];
        if (!inst) continue;

        configuration.dexed[i].bank = preset_bank;
        configuration.dexed[i].voice = preset_voice;
        configuration.dexed[i].op_enabled = OP_ENABLED_MAX;
        configuration.dexed[i].midi_channel = (uint8_t)cand;
        configuration.dexed[i].pan = ag_pan_from_float(AG_INST_PAN[i]);

        inst->setOPAll(OP_ENABLED_MAX);

        load_sd_voice(configuration.dexed[i].pool,
            preset_bank, preset_voice, (uint8_t)i);

        inst->setGain(AG_BELL_DEX_GAIN);

        if (dexed_mono2stereo[i])
            dexed_mono2stereo[i]->panorama(
                mapfloat((float)configuration.dexed[i].pan,
                    PANORAMA_MIN, PANORAMA_MAX, -1.0f, 1.0f));

        cand++;
        if (cand > 16) cand = 1;
    }
}

// ============================================================================
// MIDI channel borrow
// ============================================================================
struct ag_midi_channels_save_t {
    uint8_t epiano;
    uint8_t microsynth[NUM_MICROSYNTH];
    uint8_t msp[NUM_MULTISAMPLES];
    uint8_t drums;
    uint8_t slices;
    uint8_t dexed[NUM_DEXED];
#ifdef GRANULAR
    uint8_t granular;
#endif
    bool    valid;
};
static ag_midi_channels_save_t ag_mc_save = { 0 };

FLASHMEM static void ag_silence_other_midi_channels(void)
{
    if (ag_mc_save.valid) return;

    ag_mc_save.epiano = configuration.epiano.midi_channel;
    for (int i = 0; i < NUM_MICROSYNTH; i++)   ag_mc_save.microsynth[i] = microsynth[i].midi_channel;
    for (int i = 0; i < NUM_MULTISAMPLES; i++) ag_mc_save.msp[i] = msp[i].midi_channel;
    ag_mc_save.drums = drum_midi_channel;
    ag_mc_save.slices = slices_midi_channel;
    for (int i = 0; i < NUM_DEXED; i++)        ag_mc_save.dexed[i] = configuration.dexed[i].midi_channel;
#ifdef GRANULAR
    ag_mc_save.granular = granular_params.midi_channel;
#endif
    ag_mc_save.valid = true;

    configuration.epiano.midi_channel = MIDI_CHANNEL_OFF;
    for (int i = 0; i < NUM_MICROSYNTH; i++)   microsynth[i].midi_channel = MIDI_CHANNEL_OFF;
    for (int i = 0; i < NUM_MULTISAMPLES; i++) msp[i].midi_channel = MIDI_CHANNEL_OFF;
    drum_midi_channel = MIDI_CHANNEL_OFF;
    slices_midi_channel = MIDI_CHANNEL_OFF;
    for (int i = AG_NBELLDEX; i < NUM_DEXED; i++)
        configuration.dexed[i].midi_channel = MIDI_CHANNEL_OFF;
#ifdef GRANULAR
    granular_params.midi_channel = MIDI_CHANNEL_OFF;
#endif
}

FLASHMEM static void ag_restore_other_midi_channels(void)
{
    if (!ag_mc_save.valid) return;

    configuration.epiano.midi_channel = ag_mc_save.epiano;
    for (int i = 0; i < NUM_MICROSYNTH; i++)   microsynth[i].midi_channel = ag_mc_save.microsynth[i];
    for (int i = 0; i < NUM_MULTISAMPLES; i++) msp[i].midi_channel = ag_mc_save.msp[i];
    drum_midi_channel = ag_mc_save.drums;
    slices_midi_channel = ag_mc_save.slices;
    for (int i = 0; i < NUM_DEXED; i++) configuration.dexed[i].midi_channel = ag_mc_save.dexed[i];
#ifdef GRANULAR
    granular_params.midi_channel = ag_mc_save.granular;
#endif
    ag_mc_save.valid = false;
}

// ============================================================================
// Multiband compressor
// ============================================================================
extern MultiBandCompressor_UI_t MultiBandUIparams;
extern AudioMultiBandCompressor MultiBandCompressor;
extern bool                     multiband_active;
extern bool                     multiband_confirm;
extern void                     mb_set_master(void);

static struct {
    MultiBandCompressor_UI_t params;
    bool                     active;
    bool                     valid;
} ag_mb_save = { {}, false, false };

FLASHMEM static void ag_mb_apply_garden(void)
{
    if (ag_mb_save.valid) return;

    /* Remember the user's multiband settings so ag_mb_restore() can put
     * them back when the garden page is closed. */
    ag_mb_save.params = MultiBandUIparams;
    ag_mb_save.active = multiband_active;
    ag_mb_save.valid = true;

    /* Push the garden's aggressive mastering preset into the compressor's
     * parameter block, but do NOT enable the compressor. The garden keeps
     * whatever state the user had before entering. The preset is used
     * only if the user later presses the COMP button on the garden page. */
    MultiBandUIparams.master_gain = 55;
    MultiBandUIparams.master_mix = 100;
    MultiBandUIparams.crossover1 = 41;
    MultiBandUIparams.crossover2 = 1;
    MultiBandUIparams.crossover3 = 100;

    MultiBandUIparams.band1_thresh = 76;
    MultiBandUIparams.band1_ratio = 12;
    MultiBandUIparams.band1_attack = 69;
    MultiBandUIparams.band1_release = 32;
    MultiBandUIparams.band1_gain = 45;

    MultiBandUIparams.band2_thresh = 71;
    MultiBandUIparams.band2_ratio = 12;
    MultiBandUIparams.band2_attack = 25;
    MultiBandUIparams.band2_release = 43;
    MultiBandUIparams.band2_gain = 43;

    MultiBandUIparams.band3_thresh = 72;
    MultiBandUIparams.band3_ratio = 12;
    MultiBandUIparams.band3_attack = 49;
    MultiBandUIparams.band3_release = 53;
    MultiBandUIparams.band3_gain = 44;

    MultiBandUIparams.band4_thresh = 77;
    MultiBandUIparams.band4_ratio = 12;
    MultiBandUIparams.band4_attack = 24;
    MultiBandUIparams.band4_release = 30;
    MultiBandUIparams.band4_gain = 40;

    MultiBandUIparams.solo_mask = 0;

    MultiBandCompressor.setUI(MultiBandUIparams);

    /* Do not touch multiband_active and do not call mb_set_master() here:
     * the garden inherits the current routing. When the user presses COMP
     * on the garden page, the touch handler flips multiband_active itself
     * and calls mb_set_master(), which picks up the preset above. */
    multiband_confirm = false;
}

FLASHMEM static void ag_mb_restore(void)
{
    if (!ag_mb_save.valid) return;

    MultiBandUIparams = ag_mb_save.params;
    MultiBandCompressor.setUI(MultiBandUIparams);

    if (multiband_active != ag_mb_save.active) {
        multiband_active = ag_mb_save.active;
        mb_set_master();
    }
    multiband_confirm = false;
    ag_mb_save.valid = false;
}

// ============================================================================
// Volume routing
// ============================================================================
static float ag_pad_base_gain = 0.0f;
static float ag_pad_base_rev = 0.0f;
static float ag_pad_breath_sm = 0.0f;

FLASHMEM static void ag_pad_volume_apply(void)
{
    // LOUDNESS: base dry gain for the pad (Braids) is scaled by AG_LOUDNESS.
    // The breath envelope multiplier is applied later in the audio ISR,
    // so the wet/dry ratio of the pad is unchanged.
    const float n = (float)ag_par[P_PAD] * (float)ag_par[P_PAD] * 0.0001f;
    ag_pad_base_gain = VOL_MAX_FLOAT * n * AG_LOUDNESS;
}

// LOUDNESS: DEXED_MIXER_TRIM applies a -25% trim to the bells' master mixer
// channels. This is the last gain stage before the bell Dexed audio reaches
// the master bus, so it lowers the bells uniformly — dry and reverb send
// path stay in the same proportion as before, only the overall level
// of the bell layer drops. Set to 1.0f to restore the previous level.
#define DEXED_MIXER_TRIM  0.75f

FLASHMEM static void ag_bell_volume_apply(void)
{
    const float n = (float)ag_par[P_BELLS] * (float)ag_par[P_BELLS] * 0.0001f;
    const float g = VOL_MAX_FLOAT * n * AG_LOUDNESS * DEXED_MIXER_TRIM;
    for (int i = 0; i < AG_NBELLDEX; i++) {
        AudioNoInterrupts();
        master_mixer_r.gain(MASTER_MIX_CH_DEXED1 + i, g);
        master_mixer_l.gain(MASTER_MIX_CH_DEXED1 + i, g);
        AudioInterrupts();
    }
}

FLASHMEM static void ag_pad_send_apply(void)
{
    const uint8_t rev_fixed = AG_PAD_SEND_REV;
    const uint8_t echo1 = (uint8_t)ag_par[P_ECHO];
    const uint8_t echo2 = (uint8_t)((ag_par[P_ECHO] * 2) / 3);

    braids_osc.rev_send = rev_fixed;
    braids_osc.delay_send_1 = echo1;
    braids_osc.delay_send_2 = echo2;

    // LOUDNESS: reverb send for the pad is boosted by the same factor as
    // the pad dry signal, so the wet/dry ratio and therefore the perceived
    // reverb character are unchanged.
    ag_pad_base_rev = volume_transform(mapfloat(rev_fixed, REVERB_SEND_MIN, REVERB_SEND_MAX,
        0.0f, VOL_MAX_FLOAT)) * AG_LOUDNESS;
    AudioNoInterrupts();
    global_delay_in_mixer[0]->gain(6, mapfloat(echo1, DELAY_LEVEL_MIN, DELAY_LEVEL_MAX, 0.0f, AG_PAD_SEND_DLY_MAX));
    global_delay_in_mixer[1]->gain(6, mapfloat(echo2, DELAY_LEVEL_MIN, DELAY_LEVEL_MAX, 0.0f, AG_PAD_SEND_DLY_MAX));
    AudioInterrupts();
}

FLASHMEM static void ag_bell_send_apply(void)
{
    const uint8_t rev = (uint8_t)ag_par[P_REVERB];
    const uint8_t echo1 = (uint8_t)(ag_par[P_ECHO] / 5);
    const uint8_t echo2 = (uint8_t)((ag_par[P_ECHO] / 5) * 2 / 3);

    // LOUDNESS: bell reverb send boosted by the same factor as the bell
    // dry signal. Ratio preserved.
    const float rv = volume_transform(mapfloat(rev, REVERB_SEND_MIN, REVERB_SEND_MAX,
        0.0f, VOL_MAX_FLOAT)) * 0.95f * AG_LOUDNESS;

    for (int i = 0; i < AG_NBELLDEX; i++) {
        configuration.fx.reverb_send[i] = rev;
        configuration.fx.delay_level1[i] = echo1;
        configuration.fx.delay_level2[i] = echo2;
        AudioNoInterrupts();
        reverb_mixer_l.gain(i, rv);
        reverb_mixer_r.gain(i, rv);

        set_dexed_delay_level(0, (uint8_t)i, echo1);
        set_dexed_delay_level(1, (uint8_t)i, echo2);
        AudioInterrupts();
    }
}

// ============================================================================
// Top-line helpers
// ============================================================================
static inline void ag_top_set_colors(bool selected)
{
    if (!selected) {
        display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    }
    else if (seq.edit_state) {
        display.setTextColor(COLOR_SYSTEXT, RED);
    }
    else {
        display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
    }
}

FLASHMEM static void ag_draw_top_bank(bool selected)
{
    char buf[11];
    snprintf(buf, sizeof(buf), "B:%02d", (int)configuration.dexed[0].bank);
    ag_top_set_colors(selected);
    display.setTextSize(1);
    display.setCursor(AG_TOP_BANK_X, AG_TITLE_Y);
    display.print(buf);
}

FLASHMEM static void ag_draw_top_voice(bool selected)
{
    char buf[11];
    snprintf(buf, sizeof(buf), "V:%02d", (int)configuration.dexed[0].voice + 1);
    ag_top_set_colors(selected);
    display.setTextSize(1);
    display.setCursor(AG_TOP_VOICE_X, AG_TITLE_Y);
    display.print(buf);
}

FLASHMEM static void ag_draw_top_tempo(bool selected)
{
    char buf[11];
    snprintf(buf, sizeof(buf), "%3d BPM", (uint8_t)ag_par[P_TEMPO]);
    ag_top_set_colors(selected);
    display.setTextSize(1);
    display.setCursor(AG_TOP_TEMPO_X, AG_TITLE_Y);
    display.print(buf);
}

FLASHMEM static void ag_draw_top_bellpreset(bool selected)
{
    char buf[11];
    snprintf(buf, sizeof(buf), "BELL:%u", (unsigned)(ag_bell_preset + 1));
    ag_top_set_colors(selected);
    display.setTextSize(1);
    display.setCursor(AG_TOP_BELL_X, AG_TITLE_Y);
    display.print(buf);
}

FLASHMEM static void ag_draw_top_volume(void)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "VOL:%3u", (unsigned)configuration.sys.vol);
    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setCursor(AG_TOP_VOLUME_X, AG_TOP_VOLUME_Y);
    display.print(buf);
}

/* Voice name: fixed 11-cell field (10 name chars + 1 trailing space), in
 * reverse video against a DX_DARKCYAN background. Every call writes all
 * 11 cells, so a longer name from a previous call cannot survive and no
 * fillRect is needed. */
FLASHMEM static void ag_draw_top_name(void)
{
    char name[VOICE_NAME_LEN];
    name[0] = 0;
    strncpy(name, g_voice_name[0], sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;

    if (name[0] == 0 && MicroDexed[0]) {
        MicroDexed[0]->getName(name);
        name[sizeof(name) - 1] = 0;
    }

    char buf[AG_TOP_NAME_CHARS + 1];
    memset(buf, ' ', AG_TOP_NAME_CHARS);
    int n = (int)strlen(name);
    if (n > AG_TOP_NAME_CHARS - 1) n = AG_TOP_NAME_CHARS - 1;
    memcpy(buf, name, n);
    buf[AG_TOP_NAME_CHARS] = 0;

    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
    display.setCursor(AG_TOP_NAME_X, AG_TITLE_Y);
    display.print(buf);
}

// ============================================================================
// Bell voice swap: preset cache, request, tick
// ============================================================================
static uint8_t ag_bell_cache[AG_NBELLPRESETS][156];
static char    ag_bell_cache_voice[AG_NBELLPRESETS][VOICE_NAME_LEN];
static char    ag_bell_cache_bank[AG_NBELLPRESETS][BANK_NAME_LEN];
static bool    ag_bell_cache_ok[AG_NBELLPRESETS];

/* Read the five preset voices into RAM. Called on page entry, before the
 * real garden voice is applied and before the engine runs, so these loads
 * are silent. Afterwards a preset switch needs no SD access at all. */
FLASHMEM static void ag_bell_cache_fill(void)
{
    for (int p = 0; p < AG_NBELLPRESETS; p++) {
        ag_bell_cache_ok[p] = false;
        if (!MicroDexed[0]) continue;
        if (!load_sd_voice(configuration.dexed[0].pool,
            AG_BELL_PRESETS[p].bank, AG_BELL_PRESETS[p].voice, 0))
            continue;
        MicroDexed[0]->getVoiceData(ag_bell_cache[p]);
        strncpy(ag_bell_cache_voice[p], g_voice_name[0], VOICE_NAME_LEN - 1);
        ag_bell_cache_voice[p][VOICE_NAME_LEN - 1] = 0;
        strncpy(ag_bell_cache_bank[p], g_bank_name[0], BANK_NAME_LEN - 1);
        ag_bell_cache_bank[p][BANK_NAME_LEN - 1] = 0;
        ag_bell_cache_ok[p] = true;
    }
}

/* Ask for a new bell voice. preset >= 0 uses the RAM cache; -1 loads
 * bank/voice from the SD card (once, shared by all bell instances). */
FLASHMEM static void ag_bell_request(int8_t preset, uint8_t bank, uint8_t voice)
{
    ag_sw_preset = preset;
    ag_sw_bank = bank;
    ag_sw_voice = voice;
    ag_sw_pending = true;
    ag_sw_req_age = 0;
}

FLASHMEM static void ag_bell_set_gain(float level)
{
    // LOUDNESS: intentionally NOT scaled by AG_LOUDNESS. This is a relative
    // fade used during the bell voice swap, and scaling it here would
    // double-count the boost (the master mixer channel gain already carries
    // AG_LOUDNESS in ag_bell_volume_apply).
    const float g = AG_BELL_DEX_GAIN * level * level;   /* smoother than linear */
    for (int i = 0; i < AG_NBELLDEX; i++)
        if (MicroDexed[i]) MicroDexed[i]->setGain(g);
}

// ----------------------------------------------------------------------------
// Bank cache for manual BANK / VOICE changes
//
// The bank of the current bell voice (32 voices, 4 KB packed) is kept in
// RAM. A VOICE change inside that bank needs no SD access at all. A BANK
// change reads the new bank in small steps (directory, header, 8 x 512
// bytes, tail), each with audio interrupts held off only for that one SD
// call, so the audio interrupt runs between steps and the garden engine,
// pad, bass and drums never drop out. The read runs during the fade-out.
// ----------------------------------------------------------------------------
extern void init_MIDI_send_CC(void);
extern const char* sError;

#if defined(__IMXRT1062__)
#define AG_BANK_MEM DMAMEM
#else
#define AG_BANK_MEM
#endif

enum { AG_BK_IDLE = 0, AG_BK_OPEN, AG_BK_HEADER, AG_BK_DATA, AG_BK_TAIL };

AG_BANK_MEM static uint8_t ag_bk_data[4096];   /* 32 packed voices */
static char    ag_bk_name[BANK_NAME_LEN];
static bool    ag_bk_valid = false;          /* ag_bk_data holds ag_bk_pool / ag_bk_bank */
static uint8_t ag_bk_pool = 0, ag_bk_bank = 0;
static uint8_t ag_bk_state = AG_BK_IDLE;
static uint8_t ag_bk_want_pool = 0, ag_bk_want_bank = 0;
static bool    ag_bk_failed = false;          /* last load of want_pool / want_bank failed */
static uint16_t ag_bk_off = 0;
static File    ag_bk_file;

FLASHMEM static bool ag_bank_ready(uint8_t pool, uint8_t bank)
{
    return ag_bk_valid && ag_bk_pool == pool && ag_bk_bank == bank;
}

FLASHMEM static bool ag_bank_failed(uint8_t pool, uint8_t bank)
{
    return ag_bk_failed && ag_bk_state == AG_BK_IDLE &&
        ag_bk_want_pool == pool && ag_bk_want_bank == bank;
}

FLASHMEM static void ag_bank_close_file(void)
{
    if (ag_bk_file) {
        AudioNoInterrupts();
        ag_bk_file.close();
        AudioInterrupts();
    }
}

FLASHMEM static void ag_bank_abort(void)
{
    ag_bank_close_file();
    if (ag_bk_state != AG_BK_IDLE) ag_bk_valid = false;   /* data half-written */
    ag_bk_state = AG_BK_IDLE;
    ag_bk_failed = false;
}

/* Start (or restart) loading pool/bank. No-op if it is already in RAM or
 * already being loaded. */
FLASHMEM static void ag_bank_start(uint8_t pool, uint8_t bank)
{
    if (ag_bank_ready(pool, bank)) return;
    if (ag_bk_state != AG_BK_IDLE && ag_bk_want_pool == pool && ag_bk_want_bank == bank) return;
    if (ag_bank_failed(pool, bank)) return;            /* don't hammer a broken bank */

    ag_bank_close_file();
    ag_bk_want_pool = pool;
    ag_bk_want_bank = bank;
    ag_bk_failed = false;
    ag_bk_valid = false;                           /* ag_bk_data is about to be overwritten */
    ag_bk_state = AG_BK_OPEN;
}

FLASHMEM static void ag_bank_fail(void)
{
    ag_bank_close_file();
    ag_bk_state = AG_BK_IDLE;
    ag_bk_failed = true;
    ag_bk_valid = false;
#ifdef DEBUG
    LOG.printf("Ambient Garden: cannot load bank %d/%d\n", ag_bk_want_pool, ag_bk_want_bank);
#endif
}

/* One SD operation. Audio interrupts are held off only around that call. */
FLASHMEM static void ag_bank_step(void)
{
    switch (ag_bk_state) {
    case AG_BK_OPEN: {
        if (sd_card_internal == 0) { ag_bank_fail(); return; }
        char path[32];
        snprintf(path, sizeof(path), "/%s/%d/%d", DEXED_CONFIG_PATH, ag_bk_want_pool, ag_bk_want_bank);

        File dir, entry;
        AudioNoInterrupts();
        dir = SD.open(path);
        if (dir) {
            do {
                entry = dir.openNextFile();
            } while (entry && entry.isDirectory());
            dir.close();
        }
        AudioInterrupts();

        if (!entry) { ag_bank_fail(); return; }
        ag_bk_file = entry;
        strip_extension(ag_bk_file.name(), ag_bk_name, BANK_NAME_LEN);
        string_toupper(ag_bk_name);
        ag_bk_state = AG_BK_HEADER;
        break;
    }
    case AG_BK_HEADER: {
        uint8_t hdr[6];
        AudioNoInterrupts();
        const bool size_ok = (ag_bk_file.size() == 4104);
        const bool read_ok = size_ok && ag_bk_file.seek(0) && (ag_bk_file.read(hdr, 6) == 6);
        AudioInterrupts();
        if (!read_ok || hdr[0] != 0xF0 || hdr[1] != 0x43 || hdr[3] != 0x09) { ag_bank_fail(); return; }
        ag_bk_off = 0;
        ag_bk_state = AG_BK_DATA;
        break;
    }
    case AG_BK_DATA: {
        AudioNoInterrupts();
        const int n = ag_bk_file.read(ag_bk_data + ag_bk_off, 512);
        AudioInterrupts();
        if (n != 512) { ag_bank_fail(); return; }
        ag_bk_off += 512;
        if (ag_bk_off >= 4096) ag_bk_state = AG_BK_TAIL;
        break;
    }
    case AG_BK_TAIL: {
        uint8_t tail[2];
        AudioNoInterrupts();
        const bool read_ok = (ag_bk_file.read(tail, 2) == 2);
        AudioInterrupts();
        ag_bank_close_file();
        if (!read_ok || tail[1] != 0xF7) { ag_bank_fail(); return; }

        int32_t sum = 0;
        for (uint16_t i = 0; i < 4096; i++) sum -= ag_bk_data[i];
        if ((uint8_t)(sum & 0x7F) != tail[0]) { ag_bank_fail(); return; }   /* as get_sd_voice() */

        ag_bk_pool = ag_bk_want_pool;
        ag_bk_bank = ag_bk_want_bank;
        ag_bk_valid = true;
        ag_bk_failed = false;
        ag_bk_state = AG_BK_IDLE;
        break;
    }
    default:
        break;
    }
}

/* Run loader steps for at most ~2 ms per call. The audio interrupt gets
 * in between every step, so this only delays the UI, never the audio. */
FLASHMEM static void ag_bank_service(void)
{
    const uint32_t t0 = micros();
    while (ag_bk_state != AG_BK_IDLE && (micros() - t0) < 2000)
        ag_bank_step();
}

/* What load_sd_voice() does after loading, for every bell instance. */
FLASHMEM static void ag_bell_after_load(void)
{
    for (int i = 0; i < AG_NBELLDEX; i++) {
        if (!MicroDexed[i]) continue;
        loadDexedOrigADSR(i);
        if (configuration.sys.send_sysex_on_voice_change) {
            uint8_t copy[156];
            MicroDexed[i]->getVoiceData(copy);
            send_sysex_voice(configuration.dexed[i].midi_channel, copy);
        }
    }
    if (configuration.sys.send_midi_cc) init_MIDI_send_CC();
}

/* The actual swap. Only called while the bells are faded to silence and
 * the voice is in RAM (preset cache or bank cache). */
FLASHMEM static void ag_bell_swap_now(void)
{
    uint8_t data[156];
    char vname[VOICE_NAME_LEN];
    char bname[BANK_NAME_LEN];
    const uint8_t pool = configuration.dexed[0].pool;

    if (ag_sw_preset >= 0 && ag_sw_preset < AG_NBELLPRESETS && ag_bell_cache_ok[ag_sw_preset]) {
        memcpy(data, ag_bell_cache[ag_sw_preset], sizeof(data));
        strcpy(vname, ag_bell_cache_voice[ag_sw_preset]);
        strcpy(bname, ag_bell_cache_bank[ag_sw_preset]);
    }
    else if (ag_bank_ready(pool, ag_sw_bank) && MicroDexed[0]) {
        const uint8_t v = (uint8_t)constrain(ag_sw_voice, 0, MAX_VOICES - 1);
        uint8_t packed[128];
        memcpy(packed, ag_bk_data + (uint16_t)v * 128u, sizeof(packed));
        MicroDexed[0]->decodeVoice(data, packed);
        memset(vname, 0, sizeof(vname));
        memcpy(vname, packed + 118, 10);                 /* packed voice name */
        string_toupper(vname);
        strcpy(bname, ag_bk_name);
    }
    else {
        return;                                          /* caller checks readiness */
    }

    for (int i = 0; i < AG_NBELLDEX; i++) {
        AudioSynthDexed* inst = MicroDexed[i];
        if (!inst) continue;
        configuration.dexed[i].pool = pool;
        configuration.dexed[i].bank = ag_sw_bank;
        configuration.dexed[i].voice = ag_sw_voice;
        inst->loadVoiceParameters(data);
        inst->setTranspose(configuration.dexed[i].transpose);
        strcpy(g_voice_name[i], vname);
        strcpy(g_bank_name[i], bname);
    }
    ag_bell_after_load();
}

/* Runs from the touch handler. Time-based, so the call rate doesn't matter. */
FLASHMEM static void ag_bell_swap_tick(void)
{
    const float dt = (float)(uint32_t)ag_sw_dt;
    ag_sw_dt = 0;
    const float old = ag_sw_level;
    const uint8_t pool = configuration.dexed[0].pool;
    const bool from_bank = !(ag_sw_preset >= 0 && ag_sw_preset < AG_NBELLPRESETS &&
        ag_bell_cache_ok[ag_sw_preset]);

    /* Kick off the bank read as soon as a manual change comes in, so it
     * runs in parallel with the fade-out. */
    if (ag_sw_pending && from_bank) ag_bank_start(pool, ag_sw_bank);
    ag_bank_service();

    if (ag_sw_pending) {
        ag_sw_level -= dt / (float)AG_SW_FADE_OUT_MS;
        if (ag_sw_level <= 0.0f) {
            ag_sw_level = 0.0f;
            if ((uint32_t)ag_sw_req_age >= AG_SW_SETTLE_MS) {
                if (!from_bank || ag_bank_ready(pool, ag_sw_bank)) {
                    ag_bell_swap_now();
                    ag_sw_pending = false;
                    ag_draw_top_name();
                }
                               else if (ag_bank_failed(pool, ag_sw_bank) ||
                         (uint32_t)ag_sw_req_age > AG_SW_WATCHDOG_MS) {
                    /* Bank unreadable, or the SD read never completed.
                     * Abort the loader, keep the old voice, show the
                     * error like the voice page does, and fade back in.
                     * Without the watchdog a stalled SD read leaves
                     * ag_sw_pending set forever, which silently swallows
                     * every bell note-on in ag_drain_midi_out() and
                     * eventually starves the audio ISR. */
                    ag_bank_abort();
                    for (int i = 0; i < AG_NBELLDEX; i++) {
                        strcpy(g_voice_name[i], sError);
                        strcpy(g_bank_name[i], sError);
                    }
                    ag_sw_pending = false;
                    ag_draw_top_name();
                }
                /* else: still loading, stay silent a moment longer */
            }
        }
    }
    else if (ag_sw_level < 1.0f) {
        ag_sw_level += dt / (float)AG_SW_FADE_IN_MS;
        if (ag_sw_level > 1.0f) ag_sw_level = 1.0f;
    }

    if (ag_sw_level != old) ag_bell_set_gain(ag_sw_level);
}

/* Drop any pending swap or bank read and put the bells back at full gain. */
FLASHMEM static void ag_bell_swap_reset(void)
{
    ag_bank_abort();
    ag_sw_pending = false;
    ag_sw_level = 1.0f;
    ag_sw_dt = 0;
    ag_bell_set_gain(1.0f);
}

// ============================================================================
// Master volume adjustment (called from lcdml_menu_control() in UI.cpp)
//
// While the multiband compressor is active, mb_set_master() mutes the
// volume_l/r chain, so set_volume() would have no audible effect. To keep
// the encoder useful, we mirror what UI_func_volume does on entry and drop
// out of multiband - but only when the volume is being reduced below 100.
//
// Turning the encoder never re-enables the compressor: that is done
// exclusively by the COMP touch button. The button is redrawn immediately
// so its label always matches multiband_active.
// ============================================================================
FLASHMEM static void ag_draw_comp_button(void);   /* defined further down */

void ambient_garden_adjust_volume(int delta)
{
    if (delta == 0) return;

    int v = (int)configuration.sys.vol + delta;
    if (v < VOLUME_MIN) v = VOLUME_MIN;
    if (v > VOLUME_MAX) v = VOLUME_MAX;

    /* Encoder dropped below 100 while the compressor was on: turn the
     * compressor off so set_volume() has an audible effect. The encoder
     * click itself still counts - the new volume is applied normally. */
    if (v < 100 && multiband_active) {
        multiband_active = false;
        mb_set_master();
        ag_draw_comp_button();
    }

    if (v != (int)configuration.sys.vol) {
        configuration.sys.vol = (uint8_t)v;
        set_volume(configuration.sys.vol, configuration.sys.mono);
        save_sys_flag = true;
        save_sys = SAVE_SYS_MS / 2;
    }

    ag_draw_top_volume();
}

// ============================================================================
// Top-line editor callbacks
// ============================================================================
FLASHMEM static int16_t ag_dexed_bank_get(Editor* e) { (void)e; return (int16_t)configuration.dexed[0].bank; }
FLASHMEM static void ag_dexed_bank_set(Editor* e, int16_t v)
{
    (void)e;
    const uint8_t b = (uint8_t)constrain(v, 0, MAX_BANKS - 1);
    for (int i = 0; i < AG_NBELLDEX; i++)
        configuration.dexed[i].bank = b;
    ag_bell_request(-1, b, configuration.dexed[0].voice);   /* faded swap */
}
FLASHMEM static void ag_top_bank_renderer(Editor* e, bool refresh)
{
    (void)refresh;
    ag_draw_top_bank(generic_temp_select_menu == e->select_id);
}

FLASHMEM static int16_t ag_dexed_voice_get(Editor* e) { (void)e; return (int16_t)configuration.dexed[0].voice; }
FLASHMEM static void ag_dexed_voice_set(Editor* e, int16_t v)
{
    (void)e;
    int  bank = (int)configuration.dexed[0].bank;
    int  voice = (int)v;
    bool bankChanged = false;

    /* The voice editor's range is [-1, MAX_VOICES] so that turning past
     * either end of the bank can be detected here. Moving past the last
     * voice rolls into the next bank's first voice; moving before the
     * first voice rolls into the previous bank's last voice. The very
     * first / last voice of the first / last bank stays put. */
    if (voice >= MAX_VOICES) {
        if (bank < MAX_BANKS - 1) { bank++; voice = 0;  bankChanged = true; }
        else { voice = MAX_VOICES - 1; }   /* clamp at very top */
    }
    else if (voice < 0) {
        if (bank > 0) { bank--; voice = MAX_VOICES - 1; bankChanged = true; }
        else { voice = 0; }                /* clamp at very bottom */
    }

    const uint8_t bb = (uint8_t)constrain(bank, 0, MAX_BANKS - 1);
    const uint8_t vv = (uint8_t)constrain(voice, 0, MAX_VOICES - 1);

    for (int i = 0; i < AG_NBELLDEX; i++) {
        configuration.dexed[i].bank = bb;
        configuration.dexed[i].voice = vv;
    }
    ag_bell_request(-1, bb, vv);   /* faded swap */

    /* Voice renderer is invoked by Editor::draw_editor(true) after set().
     * The bank header is not touched by that path, so refresh it here
     * whenever the voice scroll silently crossed into another bank. */
    if (bankChanged) ag_draw_top_bank(false);
}
FLASHMEM static void ag_top_voice_renderer(Editor* e, bool refresh)
{
    (void)refresh;
    ag_draw_top_voice(generic_temp_select_menu == e->select_id);
}

FLASHMEM static int16_t ag_bell_preset_get(Editor* e) { (void)e; return ag_bell_preset; }
FLASHMEM static void ag_bell_preset_set(Editor* e, int16_t v)
{
    (void)e;
    int p = constrain((int)v, 0, AG_NBELLPRESETS - 1);
    if (p != ag_bell_preset) {
        ag_bell_preset = p;
        ag_saved_bell_voice_valid = false;   /* preset overrides any saved manual */
        for (int i = 0; i < AG_NBELLDEX; i++) {
            configuration.dexed[i].bank = AG_BELL_PRESETS[p].bank;
            configuration.dexed[i].voice = AG_BELL_PRESETS[p].voice;
        }
        ag_bell_request((int8_t)p, AG_BELL_PRESETS[p].bank,
            AG_BELL_PRESETS[p].voice);   /* faded swap from RAM */
        ag_bell_volume_apply();
        ag_bell_send_apply();

        /* Repaint the top row.
         *
         * The BELL indicator reflects the current editor selection (it is
         * the editor the user is actually turning), but BANK and VOICE are
         * only *consequences* of this preset switch: they must be printed
         * as regular text, not as if the user had selected them. When the
         * user then navigates to BANK or VOICE, their own renderers
         * (ag_top_bank_renderer / ag_top_voice_renderer) will re-draw with
         * the correct highlight from generic_temp_select_menu. */
        ag_draw_top_bellpreset(generic_temp_select_menu == AG_EDIT_BELL);
        ag_draw_top_bank(false);
        ag_draw_top_voice(false);
    }
}
FLASHMEM static void ag_top_bell_renderer(Editor* e, bool refresh)
{
    (void)refresh;
    ag_draw_top_bellpreset(generic_temp_select_menu == e->select_id);
}

FLASHMEM static void ag_set(int p, int v);

FLASHMEM static int16_t ag_top_tempo_get(Editor* e) { (void)e; return ag_par[P_TEMPO]; }
FLASHMEM static void ag_top_tempo_set(Editor* e, int16_t v) { (void)e; ag_set(P_TEMPO, v); }
FLASHMEM static void ag_top_tempo_renderer(Editor* e, bool refresh)
{
    (void)refresh;
    ag_draw_top_tempo(generic_temp_select_menu == e->select_id);
}

// ============================================================================
// Pad mixer automation (audio ISR)
// ============================================================================
#define AG_PAD_SMOOTH_ALPHA 0.0029f

FLASHMEM static void ag_pad_mixer_tick_audio(void)
{
    float lvl = en_pad_level;
    if (lvl > 1.0f) lvl = 1.0f;
    if (lvl < 0.0f) lvl = 0.0f;

    ag_pad_breath_sm += (lvl - ag_pad_breath_sm) * AG_PAD_SMOOTH_ALPHA;

    // LOUDNESS: both the pad dry gain and the pad reverb send carry
    // AG_LOUDNESS (via ag_pad_base_gain and ag_pad_base_rev). Here we just
    // apply the smoothed breath envelope to each, so the dry/wet ratio of
    // the pad is preserved exactly.
    const float dry = ag_pad_base_gain * ag_pad_breath_sm;
    master_mixer_r.gain(MASTER_MIX_CH_BRAIDS, dry);
    master_mixer_l.gain(MASTER_MIX_CH_BRAIDS, dry);

    const float rev = ag_pad_base_rev * ag_pad_breath_sm;
    braids_mixer_reverb.gain(0, rev);
    braids_mixer_reverb.gain(1, rev);
}

FLASHMEM static void ag_bright_apply(void)
{
    const float b = (float)ag_par[P_BRIGHT] * 0.015f;
    float lift = fm_exp2f(b * 0.8f + 0.25f * en_arc);
    if (lift < 0.85f) lift = 0.85f;
    if (lift > 1.20f) lift = 1.20f;

    uint16_t from = (uint16_t)(6500.0f * lift);
    uint16_t to   = (uint16_t)(40.0f * lift); 
   
    braids_osc.filter_mode          = 1;     // LP12
    braids_osc.filter_freq_from     = from;
    braids_osc.filter_freq_to       = to;
    braids_osc.filter_resonance     = 5;
    braids_osc.filter_speed         = 1;
    braids_osc.filter_lfo_intensity = 25;
    braids_osc.filter_lfo_speed     = 1;

    braids_update_all_settings();
}

// ============================================================================
// Parameters and pieces
// ============================================================================
/* Editor index -> amb parameter mapping.
 *
 * Created in ag_build_page_ui() in this order:
 *   0..4   BELL, BANK, VOICE, TEMPO, PIECE (top-line editors, custom getters)
 *   5..11  PAD, BELLS, BASS, PULSE, DENSITY, EVOLVE, DRIFT
 *   12..18 HITS, BRIGHT, SWELL, ECHO, REVERB, MOTION, ROOT
 *   19..25 MODE, FIFTH, GLIDE, SWING, HATS, B.OCT, BS.OCT
 *
 * The leading 0xFF entries are placeholders for the top-line editors. */
static const uint8_t AG_EDITOR_PARAM[] PROGMEM = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    P_PAD, P_BELLS, P_BASS, P_PULSE, P_DENSITY, P_EVOLVE, P_DRIFT,
    P_HITS, P_BRIGHT, P_SWELL, P_ECHO, P_REVERB, P_MOTION, P_ROOT,
    P_MODE, P_FIFTH, P_GLIDE, P_SWING, P_HATS, P_BOCT, P_BASSOCT,
};

FLASHMEM static int16_t ag_ed_get(Editor* e) { return ag_par[AG_EDITOR_PARAM[e->select_id]]; }
FLASHMEM static void ag_ed_set(Editor* e, int16_t v) { ag_set(AG_EDITOR_PARAM[e->select_id], v); }

FLASHMEM static void ag_set(int p, int v)
{
    const en_param_t* pi = en_param_info(p);
    v = constrain(v, (int)pi->lo, (int)pi->hi);
    ag_par[p] = (int16_t)v;
    en_set(p, v);
    switch (p) {
    case P_MODE:
        ag_pool = EN_MODE_POOL[v];
        en_pool(ag_pool);
        break;
    case P_PAD:   ag_pad_volume_apply();  break;
    case P_BELLS: ag_bell_volume_apply(); break;
    case P_BRIGHT: ag_bright_apply(); break;
    case P_REVERB: ag_fx_apply(); ag_pad_send_apply(); ag_bell_send_apply(); break;
    case P_ECHO:   ag_fx_apply(); ag_pad_send_apply(); ag_bell_send_apply(); break;
    case P_TEMPO:
        seq.bpm = (uint32_t)v;
        update_seq_speed();
        ag_fx_apply();
        break;
    case P_SIZE: case P_ETIME:
        ag_fx_apply();
        break;
    default: break;
    }
}

FLASHMEM static void ag_take_piece(int i)
{
    int16_t par[P_NPARAMS];
    uint16_t pool;
    ag_piece = ((i % AG_NPIECES) + AG_NPIECES) % AG_NPIECES;
    en_piece(ag_piece, par, &pool);
    for (int p = 0; p < P_NPARAMS; p++) {
        if (p == P_TUNE) continue;
        ag_par[p] = par[p];
        en_set(p, par[p]);
    }
    ag_pool = pool;
    en_pool(pool);
    en_regrow();
    ag_pad_volume_apply();
    ag_bell_volume_apply();
    ag_bright_apply();
    ag_pad_send_apply();
    ag_bell_send_apply();
    seq.bpm = (uint32_t)ag_par[P_TEMPO];
    update_seq_speed();
    ag_fx_apply();
}

static inline uint32_t ag_pool_abs(void)
{
    uint32_t p = ag_pool, r = (uint32_t)ag_par[P_ROOT] % 12u;
    return ((p << r) | (p >> (12 - r))) & 0xFFFu;
}

FLASHMEM static void ag_read_engine(void)
{
    int n = 0;
    ag_chord_pcs[n++] = en_chord_root % 12;
    for (int i = 0; i < EN_NPAD && n < 4; i++) {
        int pc = en_chord[i] % 12, dup = 0;
        for (int k = 0; k < n; k++) dup |= (ag_chord_pcs[k] == pc);
        if (!dup) ag_chord_pcs[n++] = pc;
    }
    ag_chord_nt = en_playing || en_chord_n ? n : 0;

    float bells = 0.0f, pulse = 0.0f;
    for (int l = 0; l < EN_NLOOP; l++) bells = fmaxf(bells, en_loop_glow[l]);
    for (int d = 0; d < EN_NDRUM; d++) pulse = fmaxf(pulse, en_drum_glow[d]);

    // const float lvl[4] = { en_pad_level, bells, en_bass_level, pulse };
    const float lvl[4] = {
        ag_active ? en_pad_level : 0.0f,
        ag_active ? bells : 0.0f,
        ag_active ? en_bass_level : 0.0f,
        ag_active ? pulse : 0.0f
    };

    for (int i = 0; i < 4; i++) {
        float t = constrain(lvl[i], 0.0f, 1.0f);
        if (t > ag_meter[i]) ag_meter[i] = t;
        else                 ag_meter[i] += (t - ag_meter[i]) * 0.15f;
    }

    static float last_arc = -999.0f;
    if (fabsf(en_arc - last_arc) > 0.02f) {
        last_arc = en_arc;
        ag_bright_apply();
    }
}

// ============================================================================
// Save / restore the garden state on the SD card
// ============================================================================

#define AG_STATE_FILE "/ambient_state.json"

FLASHMEM static void ag_save_state_to_sd(void)
{
    if (sd_card_internal <= 0) return;

    /* MDT's SD library does not truncate on FILE_WRITE, so remove first.
     * Every other save_*_json in the codebase does exactly this. Without
     * the remove, the new JSON is appended after the previous one and
     * deserializeJson() keeps reading the first object it finds, i.e.
     * the garden silently reloads the very first state forever. */
    SD.remove(AG_STATE_FILE);

    File f = SD.open(AG_STATE_FILE, FILE_WRITE);
    if (!f) return;

    StaticJsonDocument<1024> doc;
    for (int p = 0; p < P_NPARAMS; p++) {
        const en_param_t* pi = en_param_info(p);
        doc[pi->name] = (int)ag_par[p];
    }
    doc["__piece"] = ag_piece;
    doc["__bellpreset"] = ag_bell_preset;

    /* The bell bank/voice that is currently loaded. Saved explicitly (not
     * derived from ag_bell_preset) so a manual override via the top-line
     * BANK / VOICE editors also survives a page close. At this point in
     * FUNC_close(), configuration.dexed[] still holds the garden's values;
     * ag_dexed_restore_user_voice() runs later and overwrites them. */
    doc["__bellbank"] = (int)configuration.dexed[0].bank;
    doc["__bellvoice"] = (int)configuration.dexed[0].voice;
    doc["__multiband"] = (int)multiband_active;

    serializeJson(doc, f);
    f.close();
}
FLASHMEM static void ag_load_state_from_sd(void)
{
    if (sd_card_internal <= 0) return;
    if (!SD.exists(AG_STATE_FILE)) return;

    File f = SD.open(AG_STATE_FILE, FILE_READ);
    if (!f) return;

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;

    if (doc.containsKey("__piece")) {
        int p = constrain((int)doc["__piece"], 0, AG_NPIECES - 1);
        ag_piece = p;
    }
    if (doc.containsKey("__bellpreset")) {
        ag_bell_preset = constrain((int)doc["__bellpreset"], 0, AG_NBELLPRESETS - 1);
    }
    if (doc.containsKey("__bellbank") && doc.containsKey("__bellvoice")) {
        ag_saved_bellbank = (uint8_t)constrain((int)doc["__bellbank"], 0, MAX_BANKS - 1);
        ag_saved_bellvoice = (uint8_t)constrain((int)doc["__bellvoice"], 0, MAX_VOICES - 1);
        ag_saved_bell_voice_valid = true;
    }

    if (doc.containsKey("__multiband")) {
        ag_saved_multiband_on = (bool)(int)doc["__multiband"];
    }


    for (int p = 0; p < P_NPARAMS; p++) {
        const en_param_t* pi = en_param_info(p);
        if (doc.containsKey(pi->name)) {
            ag_set(p, (int)doc[pi->name]);
        }
    }
}

// ============================================================================
// MDT's transport on this page
// ============================================================================
FLASHMEM bool ambient_garden_playing(void) { return ag_active; }

FLASHMEM void ambient_garden_start_playing(void)
{
    ag_active = true;
    ag_subtick = 0;
    if (seq.clock == 1) en_clock(EN_CLK_START);
    else                en_play(1);
}

FLASHMEM void ambient_garden_stop_playing(void)
{
    ag_active = false;
    if (seq.clock == 1) en_clock(EN_CLK_STOP);
    else                en_play(0);
    ag_drain_midi_out();
}

void ambient_garden_sequencer(void)
{
    if (!ag_active || !seq.running) return;
    if (seq.clock == 2) { seq.ticks_max = 5; send_midi_clock(); }
    else if (seq.clock == 1) { en_clock(EN_CLK_TICK); }
#ifdef MUSIKANDMORE
    if (ag_subtick == 0)             digitalWrite(CV_CLOCK_OUT, LOW);
    if (ag_subtick == seq.ticks_max) digitalWrite(CV_CLOCK_OUT, HIGH);
#endif
    if (++ag_subtick > seq.ticks_max) ag_subtick = 0;
}

FLASHMEM void ambient_garden_start(void) { if (!seq.running) handleStart(); }
FLASHMEM void ambient_garden_stop(void) { if (seq.running) handleStop(); }
FLASHMEM void ambient_garden_panic(void) { ambient_garden_stop(); en_panic(); }

FLASHMEM void ambient_garden_init(void)
{
    ag_engine.running = false;
    en_init();
    ag_bell_preset = 0;
    ag_saved_bellbank = 0;
    ag_saved_bellvoice = 0;
    ag_saved_bell_voice_valid = false;
    ag_saved_multiband_on = false;

    ag_take_piece(0);
    for (int i = 0; i < 4; i++) ag_meter[i] = 0.0f;
    ag_pad_breath_sm = 0.0f;
    ag_mq_r = ag_mq_w = 0;
}

FLASHMEM const char* ambient_garden_piece_name(int i) { return en_piece_name(i); }

FLASHMEM void ambient_garden_set_piece(int i)
{
    ag_take_piece(i);
    ui.draw_editors(true);
}

// ============================================================================
// Incremental redraw caches
// ============================================================================
#define AG_VS_NONE 0xFF
static uint8_t  ag_vs_loop[AG_NLOOP][AG_LOOP_MAX];
static uint8_t  ag_vs_scale[12];
static uint8_t  ag_vs_pulse[AG_NDRUM][16];
static int8_t   ag_ld_meter_h[4] = { -1, -1, -1, -1 };
static uint32_t ag_ld_chord_key = 0xFFFFFFFFu;
static int8_t   ag_ld_root = -1;

#define AG_BG      COLOR_BACKGROUND
#define AG_LINE    RGB24toRGB565( 46,  48,  44)
#define AG_DIM     RGB24toRGB565(120, 118,  86)
#define AG_TEXT    RGB24toRGB565(232, 226, 200)
#define AG_ACC     RGB24toRGB565(118, 200, 190)
#define AG_PAD     RGB24toRGB565(150, 200,  96)
#define AG_PAD_T   RGB24toRGB565( 42,  58,  28)
#define AG_BELL    RGB24toRGB565(130, 210, 200)
#define AG_BELL_T  RGB24toRGB565( 26,  56,  56)
#define AG_BASS    RGB24toRGB565(220, 196,  96)
#define AG_BASS_T  RGB24toRGB565( 58,  52,  22)
#define AG_PULSE   RGB24toRGB565(190, 178, 110)
#define AG_PULSE_T RGB24toRGB565( 48,  44,  20)

#define AG_PIECE_Y    10
#define AG_PIECE_H    10

#define AG_GY0        20
#define AG_GY1       128
#define AG_GH        (AG_GY1 - AG_GY0)

#define AG_LEFT_W     56
#define AG_METER_X     0
#define AG_METER_Y    38
#define AG_METER_W     9
#define AG_METER_H    86
#define AG_METER_SP   13

/* The ring, scale dots, loop dots and chord name all use AG_RING_Y as their
 * vertical anchor; the extra -4 shifts the whole graphic up by 4 pixels
 * without changing any drawing logic. */
#define AG_RING_X     (DISPLAY_WIDTH / 2)
#define AG_RING_Y     (((AG_GY0 + AG_GY1) / 2) - 4)
#define AG_SCALE_R    20
#define AG_SCALE_DOT   4
#define AG_LOOP_DOT    3

#define AG_PULSE_E    (AG_GH / (2 * AG_NDRUM))
#define AG_PULSE_R    15
#define AG_PULSE_DOT   2
#define AG_PULSE_X    (DISPLAY_WIDTH - AG_PULSE_E)

static const uint8_t AG_LOOP_R_RADII[AG_NLOOP] = { 28, 35, 42, 49 };

static inline void ag_pulse_center(int d, int* cx, int* cy)
{
    *cx = AG_PULSE_X;
    *cy = AG_GY0 + AG_PULSE_E + d * (AG_GH - 2 * AG_PULSE_E) / (AG_NDRUM - 1) - 2;
}

static int8_t ag_pos_loop[AG_NLOOP][AG_LOOP_MAX][2];
static int8_t ag_pos_scale[12][2];
static int8_t ag_pos_pulse[AG_NDRUM][16][2];
static bool   ag_pos_ready = false;

FLASHMEM static void ag_polar(int32_t r, int i, int n, int8_t* out)
{
    float a = (float)i * (2.0f * PI / (float)n);
    float sx = sinf(a) * (float)r, sy = cosf(a) * (float)r;
    out[0] = (int8_t)(int)(sx + (sx < 0.0f ? -0.5f : 0.5f));
    out[1] = (int8_t)(-(int)(sy + (sy < 0.0f ? -0.5f : 0.5f)));
}

FLASHMEM static void ag_positions_init(void)
{
    if (ag_pos_ready) return;
    for (int l = 0; l < AG_NLOOP; l++)
        for (int s = 0; s < AG_LOOP_LEN[l]; s++)
            ag_polar(AG_LOOP_R_RADII[l], s, AG_LOOP_LEN[l], ag_pos_loop[l][s]);
    for (int i = 0; i < 12; i++)
        ag_polar(AG_SCALE_R, i, 12, ag_pos_scale[i]);
    for (int d = 0; d < AG_NDRUM; d++)
        for (int s = 0; s < AG_DRUM_N[d]; s++)
            ag_polar(AG_PULSE_R, s, AG_DRUM_N[d], ag_pos_pulse[d][s]);
    ag_pos_ready = true;
}

static inline void ag_shape(int x, int y, uint8_t size, uint16_t col)
{
    checkWebRemote();
    if (size <= 1) display.fillRect(x - 1, y - 1, 3, 3, col);
    else           display.fillCircle(x, y, size, col);
}

static const uint8_t AG_LOOP_SIZE[4] = { 1, 2, AG_LOOP_DOT, AG_LOOP_DOT };

static inline uint8_t ag_loop_code(int l, int s)
{
    return (uint8_t)((en_loop_note[l][s] >= 0 ? 2 : 0) |
        ((en_playing && en_loop_pos[l] == s) ? 1 : 0));
}

FLASHMEM static void ag_paint_loop_dot(int l, int s, uint8_t code)
{
    const int x = AG_RING_X + ag_pos_loop[l][s][0];
    const int y = AG_RING_Y + ag_pos_loop[l][s][1];
    const uint8_t old = ag_vs_loop[l][s];
    if (old != AG_VS_NONE && AG_LOOP_SIZE[old] > AG_LOOP_SIZE[code])
        ag_shape(x, y, AG_LOOP_SIZE[old], AG_BG);
    uint16_t col;
    switch (code) {
    case 0:  col = AG_LINE;   break;
    case 1:  col = AG_DIM;    break;
    case 2:  col = AG_BELL_T; break;
    default: col = AG_BELL;   break;
    }
    ag_shape(x, y, AG_LOOP_SIZE[code], col);
    ag_vs_loop[l][s] = code;
}

static const uint8_t AG_SCALE_SIZE[3] = { 2, 3, AG_SCALE_DOT };

static inline uint8_t ag_scale_outer(uint8_t code)
{
    return (code & 4) ? AG_SCALE_DOT : AG_SCALE_SIZE[code & 3];
}

FLASHMEM static void ag_paint_scale_dot(int i, uint8_t code)
{
    const int x = AG_RING_X + ag_pos_scale[i][0];
    const int y = AG_RING_Y + ag_pos_scale[i][1];
    const uint8_t old = ag_vs_scale[i];
    const uint8_t k = code & 3;
    const uint16_t col = (k == 2) ? AG_PAD : (k == 1) ? AG_PAD_T : AG_LINE;
    if (old != AG_VS_NONE && ag_scale_outer(old) > ag_scale_outer(code))
        ag_shape(x, y, ag_scale_outer(old), AG_BG);
    if (code & 4) {
        if (old == AG_VS_NONE || !(old & 4)) ag_shape(x, y, AG_SCALE_DOT, AG_TEXT);
        ag_shape(x, y, AG_SCALE_DOT - 1, col);
    }
    else ag_shape(x, y, AG_SCALE_SIZE[k], col);
    ag_vs_scale[i] = code;
}

static const uint8_t AG_PULSE_SIZE[5] = { 1, 1, AG_PULSE_DOT, AG_PULSE_DOT, AG_PULSE_DOT };

static inline uint8_t ag_pulse_code(int d, int s)
{
    const bool hit = (en_drum_pat[d] >> s) & 1u;
    const bool cur = en_playing && en_drum_pos[d] == s;
    if (hit) return en_pulse_rest ? 2 : (cur ? 4 : 3);
    return cur ? 1 : 0;
}

FLASHMEM static void ag_paint_pulse_dot(int d, int s, uint8_t code)
{
    int cx, cy;
    ag_pulse_center(d, &cx, &cy);
    const int x = cx + ag_pos_pulse[d][s][0];
    const int y = cy + ag_pos_pulse[d][s][1];
    const uint8_t old = ag_vs_pulse[d][s];
    if (old != AG_VS_NONE && AG_PULSE_SIZE[old] > AG_PULSE_SIZE[code])
        ag_shape(x, y, AG_PULSE_SIZE[old], AG_BG);
    uint16_t col;
    switch (code) {
    case 0:  col = AG_LINE;    break;
    case 1:  col = AG_DIM;     break;
    case 2:  col = AG_LINE;    break;
    case 3:  col = AG_PULSE_T; break;
    default: col = AG_PULSE;   break;
    }
    ag_shape(x, y, AG_PULSE_SIZE[code], col);
    ag_vs_pulse[d][s] = code;
}

#define AG_NAME_CHARS 5

static inline uint32_t ag_chord_key(void)
{
    uint32_t k = (uint32_t)ag_chord_nt;
    for (int i = 0; i < 4; i++)
        k = (k << 6) | (uint32_t)((i < ag_chord_nt ? ag_chord_pcs[i] : 63) & 63);
    return k;
}

FLASHMEM static void ag_paint_chord_name(void)
{
    char nm[16];
    nm[0] = 0;
    if (ag_chord_nt > 0) en_chord_name(ag_chord_pcs, ag_chord_nt, nm);
    nm[AG_NAME_CHARS] = 0;
    const int len = (int)strlen(nm);
    const int x0 = AG_RING_X - AG_NAME_CHARS * 3;
    const int x1 = x0 + AG_NAME_CHARS * 6;
    const int y0 = AG_RING_Y - 4;
    const int xt = AG_RING_X - len * 3;
    const int xe = xt + len * 6;

    if (xt > x0) {
        checkWebRemote();
        display.fillRect(x0, y0, xt - x0, 8, AG_BG);
    }
    if (len > 0) {
        display.setTextSize(1);
        display.setTextColor(AG_TEXT, AG_BG);
        display.setCursor(xt, y0);
        display.print(nm);
    }
    if (x1 > xe) {
        checkWebRemote();
        display.fillRect(xe, y0, x1 - xe, 8, AG_BG);
    }
}

FLASHMEM static void ag_paint_meter(int i, int h)
{
    uint16_t col, col_t;
    switch (i) {
    case 0:  col = AG_PAD;   col_t = AG_PAD_T;   break;
    case 1:  col = AG_BELL;  col_t = AG_BELL_T;  break;
    case 2:  col = AG_BASS;  col_t = AG_BASS_T;  break;
    default: col = AG_PULSE; col_t = AG_PULSE_T; break;
    }
    const int x = AG_METER_X + i * AG_METER_SP;
    const int base = AG_METER_Y + AG_METER_H;
    const int old = ag_ld_meter_h[i];
    checkWebRemote();
    if (old < 0) {
        if (h < AG_METER_H) display.fillRect(x, AG_METER_Y, AG_METER_W, AG_METER_H - h, col_t);
        if (h > 0)          display.fillRect(x, base - h, AG_METER_W, h, col);
    }
    else if (h > old) display.fillRect(x, base - h, AG_METER_W, h - old, col);
    else if (h < old)   display.fillRect(x, base - old, AG_METER_W, old - h, col_t);
    ag_ld_meter_h[i] = (int8_t)h;
}

FLASHMEM static void ag_draw_header_info(void)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "root %-2s", AG_NOTE_NAME[(ag_par[P_ROOT] % 12 + 12) % 12]);
    display.setTextSize(1);
    display.setTextColor(AG_ACC, AG_BG);
    display.setCursor(0, AG_GY0 + 3);
    display.print(buf);
}

// ============================================================================
// PIECE selector line
// ============================================================================
FLASHMEM static void ag_redraw_piece_line(int highlight)
{
    display.setTextSize(1);
    if (highlight == 2)      display.setTextColor(COLOR_SYSTEXT, RED);
    else if (highlight == 1) display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
    else                     display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.setCursor(0, AG_PIECE_Y);
    display.print(F("PIECE: "));
    const char* pn = ambient_garden_piece_name(ag_piece);
    display.print(pn);
    int pad = 10 - (int)strlen(pn);
    while (pad-- > 0) display.print(" ");
}

FLASHMEM static int16_t ag_piece_get(Editor* e) { (void)e; return (int16_t)ag_piece; }
FLASHMEM static void ag_piece_set(Editor* e, int16_t v)
{
    (void)e;
    int p = (int)v;
    if (p < 0) p = 0;
    if (p >= AG_NPIECES) p = AG_NPIECES - 1;
    ambient_garden_set_piece(p);
}
FLASHMEM static void ag_piece_renderer(Editor* e, bool refresh)
{
    (void)refresh;
    int hl = (generic_temp_select_menu == e->select_id) ? (seq.edit_state ? 2 : 1) : 0;
    ag_redraw_piece_line(hl);
}

// ============================================================================
// Incremental redraw
// ============================================================================
FLASHMEM static void ag_draw_state_invalidate(void)
{
    ag_positions_init();
    memset(ag_vs_loop, AG_VS_NONE, sizeof(ag_vs_loop));
    memset(ag_vs_scale, AG_VS_NONE, sizeof(ag_vs_scale));
    memset(ag_vs_pulse, AG_VS_NONE, sizeof(ag_vs_pulse));
    for (int i = 0; i < 4; i++) ag_ld_meter_h[i] = -1;
    ag_ld_chord_key = 0xFFFFFFFFu;
    ag_ld_root = -1;
}

FLASHMEM static void ag_draw_garden_if_needed(void)
{
    ag_read_engine();

    if (ag_ld_root != (int8_t)ag_par[P_ROOT]) {
        ag_ld_root = (int8_t)ag_par[P_ROOT];
        ag_draw_header_info();
    }

    {
        const uint32_t pool = ag_pool_abs();
        uint32_t chord = 0;
        for (int k = 0; k < ag_chord_nt; k++) chord |= 1u << (ag_chord_pcs[k] % 12);
        const int root = (ag_par[P_ROOT] % 12 + 12) % 12;
        for (int i = 0; i < 12; i++) {
            uint8_t code = (uint8_t)((i == root) ? 4 : 0);
            if (chord >> i & 1u)     code |= 2;
            else if (pool >> i & 1u) code |= 1;
            if (code != ag_vs_scale[i]) ag_paint_scale_dot(i, code);
        }
    }

    {
        const uint32_t ck = ag_chord_key();
        if (ck != ag_ld_chord_key) { ag_ld_chord_key = ck; ag_paint_chord_name(); }
    }

    for (int l = 0; l < AG_NLOOP; l++)
        for (int s = 0; s < AG_LOOP_LEN[l]; s++) {
            const uint8_t code = ag_loop_code(l, s);
            if (code != ag_vs_loop[l][s]) ag_paint_loop_dot(l, s, code);
        }

    for (int i = 0; i < 4; i++) {
        int h = (int)(ag_meter[i] * (float)AG_METER_H + 0.5f);
        if (h < 0) h = 0;
        if (h > AG_METER_H) h = AG_METER_H;
        if (h != ag_ld_meter_h[i]) ag_paint_meter(i, h);
    }

    for (int d = 0; d < AG_NDRUM; d++)
        for (int s = 0; s < AG_DRUM_N[d]; s++) {
            const uint8_t code = ag_pulse_code(d, s);
            if (code != ag_vs_pulse[d][s]) ag_paint_pulse_dot(d, s, code);
        }
}

// ============================================================================
// Editor renderers
// ============================================================================
FLASHMEM static void ag_root_renderer(Editor* e, bool refresh)
{
    display.setTextSize(1);
    if (!refresh) {
        setCursor_textGrid_small(e->x + 10, e->y);
        display.setTextColor(GREY1, COLOR_BACKGROUND);
        display.print(e->name);
    }
    print_small_scaled_bar(e->x, e->y, e->get(), 0, 11, e->select_id, 1, NULL);
}

FLASHMEM static void ag_mode_renderer(Editor* e, bool refresh)
{
    display.setTextSize(1);
    if (!refresh) {
        setCursor_textGrid_small(e->x + 10, e->y);
        display.setTextColor(GREY1, COLOR_BACKGROUND);
        display.print(e->name);
    }
    setModeColor(e->select_id);
    setCursor_textGrid_small(e->x, e->y);
    const int m = e->get();
    const char* nm = EN_MODE_NAME[(m >= 0 && m < EN_NMODES) ? m : 0];
    display.print(nm);
    int pad = 10 - (int)strlen(nm);
    while (pad-- > 0) display.print(" ");
}

// ============================================================================
// Master compressor button
// ============================================================================
extern bool multiband_active;
extern bool multiband_confirm;

FLASHMEM static void ag_draw_comp_button(void)
{
    TouchButton::drawButton(GRID.X[5], GRID.Y[5], "COMP", multiband_active ? "ON" : "OFF",
        multiband_active ? TouchButton::BUTTON_RED : TouchButton::BUTTON_NORMAL);
}

// ============================================================================
// Touch handler
// ============================================================================
static bool   ag_touch_armed = false;
static int8_t ag_ld_running = -1;

FLASHMEM static void ag_draw_play_button(void)
{
    if (seq.running)
        TouchButton::drawButton(GRID.X[1], GRID.Y[5], "STOP", "GARDEN", TouchButton::BUTTON_RED);
    else
        TouchButton::drawButton(GRID.X[1], GRID.Y[5], "PLAY", "GARDEN", TouchButton::BUTTON_ACTIVE);
    ag_ld_running = (int8_t)seq.running;
}

/* Short red flash on the three action buttons (NEXT CHORD / PREV PIECE /
 * NEXT PIECE). Those three buttons are stateless, so without this the
 * screen gives no sign that a press was registered. The flash lives for
 * AG_FEEDBACK_MS and is cleared from the periodic UI frame below, which
 * is the only place the screen is redrawn while the garden page is idle. */
#define AG_FEEDBACK_MS  150
#define AG_FB_NONE      255
#define AG_FB_NEXTCH    2
#define AG_FB_PREVP     3
#define AG_FB_NEXTP     4

static uint8_t       ag_feedback_button = AG_FB_NONE;
static elapsedMillis ag_feedback_timer;

FLASHMEM static void ag_draw_action_button(uint8_t which, bool highlighted)
{
    const TouchButton::Color col = highlighted ? TouchButton::BUTTON_RED
        : TouchButton::BUTTON_NORMAL;
    switch (which) {
    case AG_FB_NEXTCH:
        TouchButton::drawButton(GRID.X[AG_FB_NEXTCH], GRID.Y[5],
            "NEXT", "CHORD", col);
        break;
    case AG_FB_PREVP:
        TouchButton::drawButton(GRID.X[AG_FB_PREVP], GRID.Y[5],
            "PREV", "PIECE", col);
        break;
    case AG_FB_NEXTP:
        TouchButton::drawButton(GRID.X[AG_FB_NEXTP], GRID.Y[5],
            "NEXT", "PIECE", col);
        break;
    }
}

FLASHMEM static void handle_touchscreen_ambient_garden(void)
{
    ag_bell_swap_tick();
    ag_drain_midi_out();

    static elapsedMillis ag_ui_frame;
    if (ag_ui_frame >= AG_UI_FRAME_MS) {
        ag_ui_frame = 0;
        ag_draw_garden_if_needed();
        if (ag_ld_running != (int8_t)seq.running) ag_draw_play_button();

        /* Undo a red action-button flash. Both the press path (which sets
         * the button) and this path (which clears it) run in the main
         * loop, not in the audio interrupt, so no locking is needed. */
        if (ag_feedback_button != AG_FB_NONE &&
            ag_feedback_timer >= AG_FEEDBACK_MS) {
            ag_draw_action_button(ag_feedback_button, false);
            ag_feedback_button = AG_FB_NONE;
        }
    }

    if (numTouchPoints == 0) { TouchButton::isAreaPressed(0, 0, 0, 0); ag_touch_armed = true; return; }
    if (!ag_touch_armed) return;

    if (TouchButton::isAreaPressed(0, AG_PIECE_Y, DISPLAY_WIDTH - 62, AG_PIECE_H)) {
        ambient_garden_set_piece(ag_piece + 1);
        return;
    }
    if (TouchButton::isPressed(GRID.X[0], GRID.Y[5])) { LCDML.BT_quit(); return; }
    if (TouchButton::isPressed(GRID.X[1], GRID.Y[5])) {
        if (seq.running) handleStop();
        else             handleStart();
        ag_draw_play_button();
        return;
    }
    if (TouchButton::isPressed(GRID.X[2], GRID.Y[5])) {
        en_next();
        ag_draw_action_button(AG_FB_NEXTCH, true);
        ag_feedback_button = AG_FB_NEXTCH;
        ag_feedback_timer = 0;
        return;
    }
    if (TouchButton::isPressed(GRID.X[5], GRID.Y[5])) {
        multiband_active = !multiband_active;
        multiband_confirm = false;
        mb_set_master();
        ag_draw_comp_button();
        return;
    }
    if (TouchButton::isPressed(GRID.X[3], GRID.Y[5])) {
        ambient_garden_set_piece(ag_piece - 1);
        ag_draw_action_button(AG_FB_PREVP, true);
        ag_feedback_button = AG_FB_PREVP;
        ag_feedback_timer = 0;
        return;
    }
    if (TouchButton::isPressed(GRID.X[4], GRID.Y[5])) {
        ambient_garden_set_piece(ag_piece + 1);
        ag_draw_action_button(AG_FB_NEXTP, true);
        ag_feedback_button = AG_FB_NEXTP;
        ag_feedback_timer = 0;
        return;
    }
}

// Called once per main-loop iteration from MicroDexed-touch.ino::loop(),
// regardless of which page is currently active. Without this, an idle
// garden page — or one whose touch handler is starved by a slow display
// flush, a slow SD read, or a heavy chord change — leaves the MIDI-out
// ring to fill and drop. Dropped bell note-offs orphan Dexed voices,
// which is what causes the audio crackle and the frozen screen after a
// few minutes of playing.
FLASHMEM void ambient_garden_tick(void)
{
    ag_drain_midi_out();
}

// ============================================================================
// Page build
// ============================================================================
FLASHMEM static void ag_build_page_ui(void)
{
    ui.reset();

    display.setTextSize(1);
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.setCursor(1, AG_TITLE_Y);
    display.print(F("AMBIENT GARDEN"));

    ag_draw_top_bellpreset(false);
    ag_draw_top_bank(false);
    ag_draw_top_voice(false);
    ag_draw_top_name();
    ag_draw_top_tempo(false);
    ag_draw_top_volume();

    ag_draw_state_invalidate();

    ui.setCursor(0, 0);
    ui.addEditor("BELL", 0, AG_NBELLPRESETS - 1, ag_bell_preset_get, ag_bell_preset_set, ag_top_bell_renderer);
    ui.setCursor(0, 0);
    ui.addEditor("BANK", 0, MAX_BANKS - 1, ag_dexed_bank_get, ag_dexed_bank_set, ag_top_bank_renderer);
    ui.setCursor(0, 0);
    ui.addEditor("VOICE", -1, MAX_VOICES, ag_dexed_voice_get, ag_dexed_voice_set, ag_top_voice_renderer);
    ui.setCursor(0, 0);
    ui.addEditor("TEMPO", 30, 120, ag_top_tempo_get, ag_top_tempo_set, ag_top_tempo_renderer);

    ui.setCursor(0, 0);
    ui.addEditor("PIECE", 0, AG_NPIECES - 1, ag_piece_get, ag_piece_set, ag_piece_renderer);

    ui.setCursor(0, 13);
    ui.addEditor("PAD", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("BELLS", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("BASS", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("PULSE", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("DENSITY", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("EVOLVE", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("DRIFT", 0, 100, ag_ed_get, ag_ed_set);

    ui.setCursor(19, 13);
    ui.addEditor("HITS", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("BRIGHT", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("SWELL", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("ECHO", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("REVERB", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("MOTION", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("ROOT", 0, 11, ag_ed_get, ag_ed_set, ag_root_renderer);

    ui.setCursor(37, 13);
    ui.addEditor("MODE", 0, EN_NMODES - 1, ag_ed_get, ag_ed_set, ag_mode_renderer);
    ui.addEditor("FIFTH", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("GLIDE", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("SWING", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("HATS", 0, 100, ag_ed_get, ag_ed_set);
    ui.addEditor("B.OCT", -2, 2, ag_ed_get, ag_ed_set);
    ui.addEditor("BS.OCT", -1, 1, ag_ed_get, ag_ed_set);

    TouchButton::drawButton(GRID.X[0], GRID.Y[5], "BACK", "", TouchButton::BUTTON_NORMAL);
    ag_draw_action_button(AG_FB_NEXTCH, false);
    ag_draw_action_button(AG_FB_PREVP, false);
    ag_draw_action_button(AG_FB_NEXTP, false);
    ag_draw_comp_button();
}

// ============================================================================
// The UI page
// ============================================================================
FLASHMEM void UI_func_ambient_garden(uint8_t param)
{
    if (LCDML.FUNC_setup()) {
        ag_feedback_button = AG_FB_NONE;
        if (seq.running) handleStop();
        ag_save_state();

        ag_braids_save_user_patch();
        ag_dexed_save_user_voice();
        ag_silence_other_midi_channels();

        ag_braids_apply_garden_patch();

        ag_bell_preset = 0;
        ambient_garden_init();
        ag_load_state_from_sd();

        /* Restore the bell voice. If the SD state contained a bank/voice,
         * use that — it covers both the last preset (whose bank/voice is
         * what the preset applied) and any manual override the user typed
         * into the top-line BANK / VOICE editors. Only on a truly fresh
         * start (no state file, or an older file that predates the
         * __bellbank / __bellvoice keys) do we fall back to applying the
         * current preset index from the table. */
        ag_bell_cache_fill();
        ag_bell_swap_reset();
        ag_bk_valid = false;              /* banks may have been edited on other pages */
        if (ag_saved_bell_voice_valid) {
            ag_dexed_apply_garden_voice(ag_saved_bellbank, ag_saved_bellvoice);
        }
        else {
            ag_dexed_apply_garden_voice(AG_BELL_PRESETS[ag_bell_preset].bank,
                AG_BELL_PRESETS[ag_bell_preset].voice);
        }
        /* Prefetch the current bell bank, so the first VOICE change is
         * instant. Runs in steps from the touch handler. */
        ag_bank_start(configuration.dexed[0].pool, configuration.dexed[0].bank);

        ag_mb_apply_garden();
        if (ag_saved_multiband_on) {
            multiband_active = true;
            mb_set_master();
        }

        ag_route(true);
        ag_engine.running = true;

        ag_touch_armed = false;
        registerTouchHandler(handle_touchscreen_ambient_garden);

        ag_build_page_ui();

        handleStart();
        ag_draw_play_button();
        ag_draw_garden_if_needed();
    }

    if (LCDML.FUNC_loop()) {
        ui.handle_input();
        ag_drain_midi_out();
    }

    if (LCDML.FUNC_close()) {
        ag_bell_swap_reset();
        if (seq.running) handleStop();
        if (ag_active) ambient_garden_stop_playing();

        dac_mute();
        AudioNoInterrupts();
        ag_save_state_to_sd();

        uint8_t pad_ch = braids_osc.midi_channel;
        if (pad_ch == MIDI_CHANNEL_OMNI) pad_ch = 1;
        if (pad_ch != MIDI_CHANNEL_OFF) {
            for (uint8_t i = 0; i < NUM_BRAIDS; i++) {
                if (braids_osc.note_buffer[i] > 0) {
                    braids_envelope[i]->noteOff();
                    braids_osc.note_buffer[i] = 0;
                }
            }
        }
        for (int i = 0; i < AG_NBELLDEX; i++) {
            uint8_t ch = configuration.dexed[i].midi_channel;
            if (ch == MIDI_CHANNEL_OMNI) ch = 1;
            if (ch != MIDI_CHANNEL_OFF)
                handleControlChange(ch, 123, 0);
        }

        ag_braids_restore_user_patch();
        ag_dexed_restore_user_voice();
        ag_restore_other_midi_channels();
        ag_mb_restore();
        set_sidechain_levels();

        ag_engine.running = false;
        ag_route(false);
        unregisterTouchHandler();
        ag_restore_state();

        AudioInterrupts();
        dac_unmute();

        display.fillScreen(COLOR_BACKGROUND);
    }
}