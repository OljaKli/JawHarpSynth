#ifdef COMPILE_FOR_PSRAM

#include <Arduino.h>
#include <math.h>
#include <noisemaker.h>
#include "config.h"
#include "drums.h"
#include "psramloader.h"

#include <stdint.h>
#include <stdlib.h>
#include <algorithm>

extern void drawDrumsSampleWave(bool fullRedraw);
extern PsramLoader sampleLoader();
extern void play_sample(uint8_t index);
extern drum_config_t drum_config[NUM_DRUMSET_CONFIG];
extern config_t configuration;
extern bool drumScreenActive;
extern uint8_t activeSample;
extern CustomSample customSamples[NUM_CUSTOM_SAMPLES];

const char* noisemaker_category_names[] = {
   "BASSDRUM" , "SNARE", "TOM", "CONGA", "RIMSHOT", "CLAP", "HIHAT", "CRASH", "RIDE", "COWBELL", "CLAVES", "MARACAS", "ZAP"
};

const char* noisemaker_short_category_names[] = {
   "BD" , "SNARE", "TOM", "CONGA", "RIM", "CLAP", "HH", "CRASH", "RIDE", "COW", "CLAV", "MARA", "ZAP"
};

FLASHMEM void addAutoUpdateParameterEditor_uint8_t(const char* name, uint8_t minv, uint8_t maxv, uint8_t* value);
FLASHMEM void addAutoUpdateParameterEditor_uint16_t(const char* name, uint16_t minv, uint16_t maxv, uint16_t* value);

NoiseMakerParams nm_params = { NM_BD };
uint8_t noisemaker_custom_slot = 0;
int16_t* nm_preview_data = nullptr;
uint32_t nm_preview_len = 0;
//void generateDrum(NoiseMakerParams &p, int16_t* buf, uint32_t n, uint8_t channels);

// Add stereo helper functions
inline float stereoPanLeft(float sample, float pan) {
  return sample * sqrtf(1.0f - pan);
}

inline float stereoPanRight(float sample, float pan) {
  return sample * sqrtf(pan);
}

inline float stereoSpread(float sample, float width, int channel) {
  if (channel == 0) { // left
    return sample * (1.0f - width * 0.5f);
  }
  else { // right
    return sample * (1.0f + width * 0.5f);
  }
}


// ---- helpers: PSRAM alloc/free with safe fallback -----------------------
void* nm_psram_malloc(size_t size) {
  return extmem_malloc(size);
}
void nm_psram_free(void* ptr) {
  if (ptr) extmem_free(ptr);
}

// --- Noise preview buffer (PSRAM) ---
static int16_t* noisemaker_preview_buffer = nullptr;
static size_t noisemaker_preview_length = 0;

void allocateNoisePreview(size_t length) {
  if (noisemaker_preview_buffer) {
    extmem_free(noisemaker_preview_buffer);
    noisemaker_preview_buffer = nullptr;
  }
  noisemaker_preview_buffer = (int16_t*)extmem_malloc(length * sizeof(int16_t));
  noisemaker_preview_length = length;
}


FLASHMEM void nm_preview_free() {
  if (nm_preview_data) {
    nm_psram_free(nm_preview_data);
    nm_preview_data = nullptr;
  }
  nm_preview_len = 0;

}

// Modified allocation function for stereo
// FLASHMEM bool nm_preview_alloc(uint32_t frames, uint8_t channels) {
//   const uint32_t bytes = frames * channels * sizeof(int16_t);

//   // Free existing data
//   nm_preview_free();

//   // Allocate new memory with error checking
//   nm_preview_data = (int16_t*)nm_psram_malloc(bytes);
//   if (!nm_preview_data) {
//     nm_preview_len = 0;
//     return false;
//   }

//   // Initialize the memory to zero
//   memset(nm_preview_data, 0, bytes);

//   nm_preview_len = frames;
//   return true;
// }

// Modified allocation function to always allocate a 5-second buffer.
FLASHMEM bool nm_preview_alloc(uint32_t frames, uint8_t channels) {
  // Hardcode buffer size to 5 seconds to ensure enough length for reverb tail.
  const uint32_t REVERB_BUFFER_SECONDS = 5;
  const uint32_t targetFrames = (uint32_t)(SAMPLE_RATE * REVERB_BUFFER_SECONDS);
  const uint32_t bytes = targetFrames * channels * sizeof(int16_t);

  // Free existing data
  nm_preview_free();

  // Allocate new memory with error checking
  nm_preview_data = (int16_t*)nm_psram_malloc(bytes);
  if (!nm_preview_data) {
    nm_preview_len = 0;
    return false;
  }

  // Initialize the memory to zero
  memset(nm_preview_data, 0, bytes);

  nm_preview_len = targetFrames;
  return true;
}

#include <Audio.h>
#include <TeensyVariablePlayback.h>
#include "offline_reverb.h"
extern AudioPlayArrayResmp WAV_preview_PSRAM;
OfflineReverb noiseReverb;

// helper function to convert a uint8_t value (0-100) to a float (0.0-1.0)
float convertUint8ToFloat(uint8_t value) {  
    return static_cast<float>(value) / 100.0f;
}

// ------------------------------- Sample Analysis ---------------------------------
// A function to check if the generated sample itself has high-pitched noise.
FLASHMEM bool isSampleDangerous() {

  if (nm_params.category!=NM_ZAP) //ZAP is supposed to have high frequencies like this
  {
    const float DANGER_THRESHOLD = 0.05f; // Empirical value for detecting harsh high-freq content
    const uint8_t channels = nm_params.stereo_mode ? 2 : 1;
    
    float high_pass_energy = 0.0f;
    float prev_sample = 0.0f;
    uint32_t samples_analyzed = 0;

    for (uint32_t i = 0; i < nm_preview_len; i += channels) {
        if (nm_preview_data[i] != 0) {
            float sample = nm_preview_data[i] / 32768.0f; // Normalize to -1.0 to 1.0
            
            // Simple 1st-order high-pass filter to isolate high frequencies
            float high_passed_sample = sample - prev_sample;
            prev_sample = sample;

            high_pass_energy += fabsf(high_passed_sample);
            samples_analyzed++;
        }
    }

    if (samples_analyzed == 0) return false; // Avoid division by zero

    // Calculate the average energy over the analysis window
    float average_energy = high_pass_energy / (float)samples_analyzed;
    return average_energy > DANGER_THRESHOLD;
  }
  else
  return false;
}

FLASHMEM void setupNoiseReverb() {
    noiseReverb.init(44100.0f); // Assuming 44.1kHz sample rate
    // Set default reverb parameters
    noiseReverb.setRoomSize(convertUint8ToFloat(nm_params.reverb_room_size));
    noiseReverb.setDamping(convertUint8ToFloat(nm_params.reverb_damping));
    noiseReverb.setWetLevel( float(convertUint8ToFloat(nm_params.reverb_wet))/102);
    noiseReverb.setDryLevel(convertUint8ToFloat(nm_params.reverb_dry));
noiseReverb.setWidth(convertUint8ToFloat(nm_params.reverb_width));
}

FLASHMEM void generateAndPlayNoise() {
  setupNoiseReverb();
   // if (!nm_preview_data || nm_preview_len == 0) {
        generateNoisePreview();
   // }
    if (!nm_preview_data || nm_preview_len == 0) return;

if (nm_params.reverb_enable)
 noiseReverb.applyReverb(nm_preview_data, nm_preview_len,nm_params.stereo_mode);

    uint8_t channels = (nm_params.stereo_mode == 0) ? 1 : 2;
    if ((const uint8_t*)nm_preview_data != NULL && nm_preview_len > 0)
     {
      if (isSampleDangerous()==false)
        WAV_preview_PSRAM.playRaw((int16_t*)(const uint8_t*)nm_preview_data, 
                                 nm_preview_len, channels);
    }

    // Give the audio engine a moment to start reading
    delay(40);
}

FLASHMEM void PlayOnly() {
    if (!nm_preview_data || nm_preview_len == 0) return;
    uint8_t channels = (nm_params.stereo_mode == 0) ? 1 : 2;
    if ((const uint8_t*)nm_preview_data != NULL && nm_preview_len > 0)
     {
      if (isSampleDangerous()==false)
        WAV_preview_PSRAM.playRaw((int16_t*)(const uint8_t*)nm_preview_data, 
                                 nm_preview_len, channels);
    }
    // Give the audio engine a moment to start reading
    delay(40);
}


FLASHMEM void PlaySlot() {
  play_sample(NUM_STATIC_PITCHED_SAMPLES + noisemaker_custom_slot);
  delay(40);
}

extern FLASHMEM void saveNoiseToSdCard();

FLASHMEM void saveNoiseToCustomSlot() {
    if (!nm_preview_data || nm_preview_len == 0) {
        generateNoisePreview();
        if (!nm_preview_data || nm_preview_len == 0) return;
    }
    
    uint8_t targetSlot = NUM_STATIC_PITCHED_SAMPLES + noisemaker_custom_slot;
    uint8_t channels = (nm_params.stereo_mode == 0) ? 1 : 2;
    // Free existing data if any
    if (drum_config[targetSlot].drum_data != nullptr) {
        extmem_free(const_cast<uint8_t*>(drum_config[targetSlot].drum_data));
        drum_config[targetSlot].drum_data = nullptr;
    }
    
    // Corrected size calculation. nm_preview_len is the total number of samples.
    // It's already the number of samples * channels.
    size_t bytes = nm_preview_len * sizeof(int16_t);
    uint8_t* newData = (uint8_t*)extmem_malloc(bytes);
    
    if (newData) {
        // Correctly copy the data
        memcpy(newData, nm_preview_data, bytes);
        
        // Update drum config
        drum_config[targetSlot].drum_data = newData;
        drum_config[targetSlot].len = nm_preview_len;
        drum_config[targetSlot].numChannels = channels;
        drum_config[targetSlot].vol_max = 100;
        drum_config[targetSlot].pan = 0;

    // Update name
    char name[20];
    char file_name[20];
    char filep[FULLPATH_MAX_LEN];

    snprintf(name, sizeof(name), "NM-%s-%02d", noisemaker_short_category_names[nm_params.category], noisemaker_custom_slot + 1);
    snprintf(file_name, sizeof(file_name), "NM-%s-%02d.wav", noisemaker_short_category_names[nm_params.category], noisemaker_custom_slot + 1);
    strncpy(drum_config[targetSlot].name, name, sizeof(drum_config[targetSlot].name) - 1);
    strncpy(drum_config[targetSlot].filename, file_name, sizeof(drum_config[targetSlot].filename) - 1);
    snprintf(filep, sizeof(filep), "/CUSTOM/%s", noisemaker_short_category_names[nm_params.category]);
    strncpy(customSamples[noisemaker_custom_slot].filepath, filep, sizeof(filep));

    // Update custom sample settings
    customSamples[noisemaker_custom_slot].start = 0;
    customSamples[noisemaker_custom_slot].end = 1000; //this is just for the trimming function, not lenght of the sample

    switch (nm_params.category) {
    case NM_BD:
      drum_config[targetSlot].drum_class = DRUM_BASS;
      break;
    case NM_SD:
      drum_config[targetSlot].drum_class = DRUM_SNARE;
      break;
    case NM_TOM:
      drum_config[targetSlot].drum_class = DRUM_MIDTOM;
      break;
    case NM_CONGA:
      drum_config[targetSlot].drum_class = DRUM_PERCUSSION;
      break;
    case NM_RIM:
      drum_config[targetSlot].drum_class = DRUM_PERCUSSION;
      break;
    case NM_COWBELL:
      drum_config[targetSlot].drum_class = DRUM_PERCUSSION;
      break;
    case NM_CLAVES:
      drum_config[targetSlot].drum_class = DRUM_PERCUSSION;
      break;
    case NM_MARACAS:
      drum_config[targetSlot].drum_class = DRUM_PERCUSSION;
      break;
    case NM_CLAP:
      drum_config[targetSlot].drum_class = DRUM_HANDCLAP;
      break;
    case NM_HH:
      drum_config[targetSlot].drum_class = DRUM_HIHAT;
      break;
    case NM_CRASH:
      drum_config[targetSlot].drum_class = DRUM_CRASH;
      break;
    case NM_RIDE:
      drum_config[targetSlot].drum_class = DRUM_RIDE;
      break;
    default:
      drum_config[targetSlot].drum_class = DRUM_NONE;
      break;
    }

    // Update waveform display
    drawNoiseMakerWaveform(true);
    saveNoiseToSdCard();
  }
}

// ==================================================
// Helpers
// ==================================================

inline uint32_t msToSamples(float ms) {
  // convert milliseconds -> samples, rounding to nearest sample
  const float samplesF = (ms / 1000.0f) * SAMPLE_RATE;
  return (uint32_t)lrintf(samplesF);
}

inline float clamp1(float x) {
  if (x < -1.f) return -1.f;
  if (x > 1.f) return  1.f;
  return x;
}

inline float nm_rand_white() {
  return (float)rand() / (float)RAND_MAX * 2.0f - 1.0f;
}

// ==================================================
// Envelopes
// ==================================================
inline float amplitudeEnvelopeADSR(uint32_t i, uint32_t n,
  uint16_t a_ms, uint16_t d_ms,
  uint16_t s_pct, uint16_t r_ms) {
  uint32_t a = msToSamples(a_ms);
  uint32_t d = msToSamples(d_ms);
  uint32_t r = msToSamples(r_ms);
  float s = s_pct / 100.0f;

  if (i < a) return (float)i / (float)a;
  else if (i < a + d) {
    float t = (float)(i - a) / (float)d;
    return 1.f + t * (s - 1.f);
  }
  else if (i < n - r) return s;
  else {
    float t = (float)(i - (n - r)) / (float)r;
    return s * (1.f - t);
  }
}

inline float expPitchEnv(uint32_t i, uint32_t n, float startRatio, float endRatio = 1.f) {
  float t = (float)i / (float)n;
  return startRatio * powf(endRatio / startRatio, t);
}

inline float softclip(float x) {
  return x / (1.f + fabsf(x));
}

// ---------------- Small helpers (branch-light where it matters) -------------
static inline float nm_clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float nm_mix(float a, float b, float t) { return a + (b - a) * t; }
static inline float nm_softclip(float x) { return tanhf(x); }
static inline float nm_clip1(float x) { return nm_clamp(x, -1.f, 1.f); }
static inline float nm_abs(float x) { return x < 0 ? -x : x; }

// Map 0..100 to [minV, maxV]
static inline float map01_100(uint8_t v, float minV, float maxV) {
  return minV + (maxV - minV) * (float)v * (1.0f / 100.0f);
}

// ----------------------------- Envelopes ------------------------------------
// Simple AR (attack, release) in milliseconds
static inline float envAR(uint32_t i, uint32_t n, float attackMs, float releaseMs) {
  const float aS = attackMs * 0.001f * SAMPLE_RATE + 1.f;
  const float rS = releaseMs * 0.001f * SAMPLE_RATE + 1.f;
  if (i < (uint32_t)aS) return (float)i / aS;
  const uint32_t r0 = (n > (uint32_t)rS) ? (n - (uint32_t)rS) : 0u;
  if (i < r0) return 1.f;
  const float t = (float)(i - r0) / rS;
  return 1.f - t;
}

// Exponential percussion envelope (fast attack ~0ms, mono-decay in ms)
static inline float envPercExp(uint32_t i, float decayMs) {
  const float lambda = 1.0f / (decayMs * 0.001f * SAMPLE_RATE + 1.f);
  // ~-60 dB by 'decayMs'
  const float c = 6.9f;
  return expf(-(float)i * lambda * c);
}

static inline float envPercExpMsCymbal(float tMs, float decayMs) {
  float x = tMs / decayMs;
  if (x > 1.f) x = 1.f;
  return expf(-5.0f * x); // adjust 5.0f for sharper or softer decay
}


// ADSR with times in ms and sustain 0..1
static inline float envADSR_ms(uint32_t i, uint32_t n, float aMs, float dMs, float sLvl, float rMs) {
  const float aS = aMs * 0.001f * SAMPLE_RATE + 1.f;
  const float dS = dMs * 0.001f * SAMPLE_RATE + 1.f;
  const float rS = rMs * 0.001f * SAMPLE_RATE + 1.f;

  if (i < (uint32_t)aS) return (float)i / aS; // attack
  const uint32_t d0 = (uint32_t)aS;
  if (i < d0 + (uint32_t)dS) {
    const float t = (float)(i - d0) / dS;
    return nm_mix(1.f, sLvl, t);
  }
  const uint32_t r0 = (n > (uint32_t)rS) ? (n - (uint32_t)rS) : n;
  if (i < r0) return sLvl;
  const float t = (float)(i - r0) / rS;
  return sLvl * (1.f - t);
}

// Exponential pitch envelope from startRatio -> 1.0 over N samples
static inline float pitchEnvExp(uint32_t i, uint32_t n, float startRatio) {
  const float t = (float)i / (float)std::max<uint32_t>(1, n - 1);
  return powf(startRatio, 1.0f - t);
}

// --------------------------- Filters (OnePole) -------------------------------
// Lightweight one-pole LP/HP (alpha in (0,1)), simple but musical
struct OnePole {
  float z = 0.f;
  float a = 0.2f;
  bool  hp = false; // false=LP(true)=HP

  void setLP(float alpha) { a = nm_clamp(alpha, 0.001f, 0.999f); hp = false; }
  void setHP(float alpha) { a = nm_clamp(alpha, 0.001f, 0.999f); hp = true; }

  inline float process(float x) {
    z += a * (x - z);
    return hp ? (x - z) : z;
  }
};

// ------------------------- State Variable Filter (SVF) -----------------------
// TPT-SVF (Zavalishin) with LP/BP/HP outputs.
struct SVF {
  float g = 0.f, R = 0.5f;
  float ic1eq = 0.f, ic2eq = 0.f;

  void set(float cutHz, float Q) {
    cutHz = nm_clamp(cutHz, 10.f, 0.49f * SAMPLE_RATE);
    Q = nm_clamp(Q, 0.1f, 20.f);
    const float K = tanf((float)M_PI * cutHz / SAMPLE_RATE);
    g = K;
    R = 1.f / Q;
  }

  inline void process(float x, float& lp, float& bp, float& hp) {
    const float v1 = (x - ic2eq - R * ic1eq) / (1.f + R * g + g * g);
    const float v2 = ic1eq + g * v1;
    const float v3 = ic2eq + g * v2;
    ic1eq = 2.f * v2 - ic1eq;
    ic2eq = 2.f * v3 - ic2eq;
    lp = v3;
    bp = v2;
    hp = x - R * bp - lp;
  }

  inline float lpOut(float x) { float l, b, h; process(x, l, b, h); return l; }
  inline float bpOut(float x) { float l, b, h; process(x, l, b, h); return b; }
  inline float hpOut(float x) { float l, b, h; process(x, l, b, h); return h; }
};

// ------------------------ Defaults ------------------------------------------
FLASHMEM void initNoiseMakerParams(NoiseMakerParams& p) {

    p.reverb_room_size=100;  
     p.reverb_damping=30;   
     p.reverb_wet=100;       
     p.reverb_dry=95;     
       p.reverb_width=100; 

  setupNoiseReverb();
  //p.mode = NM_MODE_TR808;
 //p.randomizer=0;
 // p.category = NM_BD;
  p.accent = 90;


       
  // BD
  p.bd_pitch = 20;
  p.bd_pitch_env = 10;
  p.bd_pitch_decay = 30;
  p.bd_decay = 50;
  p.bd_attack = 0;
  p.bd_release = 40;
  p.bd_tone = 75;
  p.bd_click = 25;
  p.bd_noise = 5;
  p.bd_drive = 10;
  p.bd_comp = 12;

  // SN
  p.sn_tone1 = 180; p.sn_tone2 = 330;
  p.sn_tone_mix = 50;
  p.sn_pitch_env = 20;
  p.sn_decay = 65;
  p.sn_noise = 70;
  p.sn_bp_freq = 4800;
  p.sn_bp_q = 25;
  p.sn_snap = 35;
  p.sn_drive = 8;

  // Toms
  p.tom_pitch = 140;
  p.tom_decay = 65;
  p.tom_pitch_env = 20;
  p.tom_tone = 70;
  p.tom_noise = 10;

  // Congas (808)
// Congas
  p.conga_pitch = 220;  // base pitch (~Low conga)
  p.conga_decay = 70;   // envelope length
  p.conga_tone = 60;   // sine ↔ woody balance
  p.conga_pitch_env = 20;   // pitch drop at onset
  p.conga_noise = 30;   // noise contribution


  // Rim
  p.rim_decay = 35;      // short woody hit
  p.rim_tone = 40;      // mostly resonant tone
  p.rim_pitch = 1800;    // ~1.8 kHz center
  p.rim_drive = 10;      // light saturation


  // Clap
  p.clap_repeats = 3;
  p.clap_spread = 50;
  p.clap_tail = 60;
  p.clap_bp_hz = 1200;
  p.clap_bp_q = 80;
  p.clap_decay = 65;
  p.clap_drive = 8;

  p.hh_tone = 40;   // Balance between metallic and noise
  p.hh_noise = 60;   // Emphasize noise for open hats
  p.hh_bp_hz = 3000;
  p.hh_bp_q = 5;
  p.hh_decay = 60;   // Base decay time
  p.hh_drive = 10;
  p.hh_detune = 4;



  // Cymbals
  p.crash_tone = 60;
  p.crash_bp_hz = 6500;
  p.crash_bp_q = 60;
  p.crash_decay = 85;
  p.ride_tone = 55;
  p.ride_bp_hz = 7000;
  p.ride_bp_q = 65;
  p.ride_decay = 90;

  // Cowbell
  p.cb_freq1 = 560;
  p.cb_freq2 = 840;
  p.cb_amp_attack = 0;
  p.cb_amp_decay = 55;
  p.cb_tone_balance = 50;
  p.cb_noise = 5;
  p.cb_drive = 20;

  // Claves
  p.claves_hz = 1700;
  p.claves_decay = 40;
  p.claves_drive = 5;

  // Maracas
  p.mar_bp_hz = 1800; // base BP center
  p.mar_bp_q = 70;   // resonance
  p.mar_decay = 50;   // ~70 ms total AR

p.zap_pitch_start = 6000;
p.zap_pitch_end = 200;
p.zap_decay = 50;


}

// --------------------------- Time helpers (ms) -------------------------------
static inline float u8_to_ms_fast(uint8_t v, float minMs, float maxMs) {
  return map01_100(v, minMs, maxMs);
}
static inline float u8_to_q(uint8_t v, float minQ = 0.4f, float maxQ = 12.f) {
  return map01_100(v, minQ, maxQ);
}

// ============================================================================
//                            SOUND GENERATORS
// ============================================================================

// ------------------------------- Bass Drum ----------------------------------
static inline void generateBassdrum(NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);
  float phase = 0.f;

  // CORRECTED: Lowered the base frequency range for 808 mode to prevent the perceived pitch from being too high.
  const float f0 = mode909 ? 20.0f + ((float)p.bd_pitch / 100.0f) * 50.0f : 5.0f + ((float)p.bd_pitch / 100.0f) * 25.0f;

  //const float attackMs = mode909 ? 0.0f : u8_to_ms_fast(p.bd_attack, 0.0f, 2.0f);
  const float decayMs = u8_to_ms_fast(p.bd_decay, 60.f, 800.f);
  const float releaseMs = mode909 ? 0.0f : u8_to_ms_fast(p.bd_release, 20.f, 600.f);

  const float pitchEnvFactor = mode909 ? (1.f + 0.05f * (p.bd_pitch_env)) : (1.f + 0.02f * (p.bd_pitch_env));
  const float startRatio = (mode909 ? 4.0f : 14.0f) * pitchEnvFactor;

  OnePole clickHP;
  clickHP.setHP(mode909 ? 0.35f : 0.2f);

  const float pitchEnvDurationMs = mode909 ? 10.0f + (p.bd_pitch_decay / 100.0f) * 90.0f : 50.0f + (p.bd_pitch_decay / 100.0f) * 450.0f;
  const uint32_t pitchEnvN = (uint32_t)(SAMPLE_RATE * (pitchEnvDurationMs / 1000.0f));
  const uint32_t clickSamples = (uint32_t)(SAMPLE_RATE * (mode909 ? 0.0015f : 0.002f));

  const float toneAmt = p.bd_tone * 0.01f;
  const float clickAmt = p.bd_click * 0.01f;
  const float noiseAmt = p.bd_noise * 0.001f;
  const float driveAmt = p.bd_drive * 0.01f;
  const float compAmt = p.bd_comp * 0.01f;
  const float accent = p.accent * 0.01f;

  const uint32_t fade_start_idx = (2 * n) / 3;

  for (uint32_t i = 0; i < n; ++i) {
    const float a = envPercExp(i, decayMs + releaseMs);

    const float pitch = pitchEnvExp(std::min(i, pitchEnvN), pitchEnvN, startRatio);
    const float freq = f0 * pitch;

    phase += 2.f * (float)M_PI * freq / SAMPLE_RATE;
    if (phase >= 2.f * (float)M_PI) phase -= 2.f * (float)M_PI;
    float s = sinf(phase);
    float body = nm_mix(s, nm_softclip(1.8f * s), toneAmt);

    float click = 0.f;
    if (i < clickSamples) {
      click = clickHP.process(nm_rand_white()) * (1.f - (float)i / (float)clickSamples);
    }

    float tailNoise = noiseAmt * nm_rand_white() * (1.0f - (float)i / (float)n);
    if (mode909) tailNoise *= 1.2f;

    float y = body + clickAmt * click + tailNoise;
    y = nm_mix(y, nm_softclip(3.0f * y), driveAmt);

    if (!mode909 && compAmt > 0.001f) {
      const float c = tanhf(y * (1.0f + compAmt * 2.2f));
      y = nm_mix(y, c, compAmt);
    }

    if (mode909) y *= 1.15f;

    y *= a * accent;

    if (i >= fade_start_idx) {
      float fade_factor = 1.0f - ((float)(i - fade_start_idx) / (float)(n - fade_start_idx));
      y *= fade_factor;
    }

    if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
    }
    else
      psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
  }
}

static inline float nm_rand_pink() {
    static float b0, b1, b2;
    float white = nm_rand_white();
    b0 = 0.99886f * b0 + white * 0.0555179f;
    b1 = 0.99476f * b1 + white * 0.0325429f;
    b2 = 0.96600f * b2 + white * 0.14329f;
    return b0 + b1 + b2;
}

// ------------------------------- Snare --------------------------------------
static inline void generateSnare(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
    const bool stereo = (channels == 2);

    float ph1 = 0.f, ph2 = 0.f, ph3 = 0.f;

    // Tonal Body Frequencies based on mode
    const float f1 = mode909 ? p.sn_tone1 : 170.f;
    const float f2 = mode909 ? p.sn_tone2 : 250.f;
    const float f3 = mode909 ? p.sn_tone1 * 0.5f : 170.f * 0.5f;

    const float decayMs = u8_to_ms_fast(p.sn_decay, 60.f, 900.f);
    const float snapAmt = p.sn_snap * 0.01f;
    const float noiseAmt = p.sn_noise * 0.001f;
    const float toneMix = p.sn_tone_mix * 0.01f;
    const float driveAmt = p.sn_drive * 0.01f;
    const float accent = p.accent * 0.01f;

    // Noise Generator and Filter based on mode
    SVF noiseFilter;
    float noiseFreq = (float)p.sn_bp_freq;
    float filterQ = 1.0f + ((float)p.sn_bp_q / 100.0f) * 9.0f;
    noiseFilter.set(noiseFreq, filterQ);

    // Pitch Envelope and Snap Transient based on mode
    const float startRatio = mode909 ? 1.0f + 0.01f * p.sn_pitch_env : 1.0f + 0.05f * p.sn_pitch_env;
    const uint32_t pitchEnvN = (uint32_t)(SAMPLE_RATE * (mode909 ? 0.005f : 0.008f));
    const uint32_t snapN = (uint32_t)(SAMPLE_RATE * (mode909 ? 0.004f : 0.002f));

    // Dynamic gain compensation for high Q values to prevent overdriving the filter
    float gainCompensation = 1.0f / (1.0f + (filterQ - 1.0f) * 0.75f);
    
    for (uint32_t i = 0; i < n; ++i) {
        const float a = envPercExp(i, decayMs);

        // Apply pitch envelope to the tonal frequencies.
        const float envPitch = pitchEnvExp(std::min(i, pitchEnvN), pitchEnvN, startRatio);
        const float ft1 = f1 * envPitch;
        const float ft2 = f2 * envPitch;
        const float ft3 = f3 * envPitch;

        ph1 += 2.f * (float)M_PI * ft1 / SAMPLE_RATE; if (ph1 > 2.f * (float)M_PI) ph1 -= 2.f * (float)M_PI;
        ph2 += 2.f * (float)M_PI * ft2 / SAMPLE_RATE; if (ph2 > 2.f * (float)M_PI) ph2 -= 2.f * (float)M_PI;
        ph3 += 2.f * (float)M_PI * ft3 / SAMPLE_RATE; if (ph3 > 2.f * (float)M_PI) ph3 -= 2.f * (float)M_PI;
        
        float tone = nm_mix(nm_mix(sinf(ph1), sinf(ph2), toneMix), sinf(ph3), 0.5f);
        
        // Generate and filter the noise.
        float lp, bpOut, hpOut;
        if (mode909) {
            // 909 uses pink noise and high-pass filtering
            noiseFilter.process(nm_rand_pink(), lp, bpOut, hpOut);
            bpOut = hpOut;
        } else {
            // 808 uses white noise and band-pass filtering
            noiseFilter.process(nm_rand_white(), lp, bpOut, hpOut);
        }

        // Apply gain compensation to the bandpass output for 808
        float nShape = bpOut * gainCompensation;

        // Generate the snap transient.
        float snap = 0.f;
        if (i < snapN) {
            float t = 1.f - (float)i / (float)snapN;
            snap = nm_rand_white() * t;
        }

        // Combine all three components: tone, noise, and snap.
        float y = (1.f - noiseAmt) * tone + noiseAmt * nShape + snapAmt * snap;
        
        // Apply clipping and drive.
        y = nm_mix(y, nm_softclip(3.0f * y), driveAmt);

        // Final gain and accent.
        y *= a * accent;
        if (stereo) {
            psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
            psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
        } else {
            psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
        }
    }
}

// ------------------------------- Toms ---------------------------------------
static inline void generateTom(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  // 909 toms have a very low, almost inaudible pitch. Let's make it more configurable.
  const bool stereo = (channels == 2);

  const float f0 = mode909 ? (float)p.tom_pitch * 0.5f : (float)p.tom_pitch;
  float ph = 0.f;

  const float decayMs = u8_to_ms_fast(p.tom_decay, 80.f, 1200.f);
  const float toneAmt = p.tom_tone * 0.01f;
  const float noiseAmt = p.tom_noise * 0.01f;
  const float accent = p.accent * 0.01f;

  const float startRatio = 1.f + (mode909 ? 0.015f : 0.02f) * p.tom_pitch_env;

  // 909 has a different type of transient. Let's use a very short burst of noise.
  OnePole hp; hp.setHP(0.2f);
  const uint32_t tickSamples = (uint32_t)(SAMPLE_RATE * (mode909 ? 0.0015f : 0.002f));

  for (uint32_t i = 0; i < n; ++i) {
    const float a = envPercExp(i, decayMs);
    const float freq = f0 * pitchEnvExp(std::min(i, (uint32_t)(0.3f * n)), (uint32_t)(0.3f * n), startRatio);

    ph += 2.f * (float)M_PI * freq / SAMPLE_RATE;
    if (ph > 2.f * (float)M_PI) ph -= 2.f * (float)M_PI;

    float s = nm_mix(sinf(ph), nm_softclip(1.7f * sinf(ph)), toneAmt);
    float tick = 0.0f;
    if (i < tickSamples) {
      tick = noiseAmt * hp.process(nm_rand_white()) * (1.0f - (float)i / (float)tickSamples);
    }
    s += tick;

    if (mode909) s = nm_softclip(1.6f * s);

    s *= a * accent;
     if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
    }
    else

    psram_buf[i] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
  }
}


// ------------------------------- Congas (808/909 improved v3) --------------------------------
static inline void generateConga(NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  // Conga is an 808 sound. 909 doesn't have it. So, mode909 should probably be ignored here.
  const bool stereo = (channels == 2);

  float basePitch = (float)p.conga_pitch;
  float f0 = basePitch;
  f0 *= 1.3f;   // Mid conga (≈ +30%)

  float ph1 = 0.f, ph2 = 0.f;

  // Envelope
  const float decayMs = u8_to_ms_fast(p.conga_decay, 80.f, 1000.f);
  const float toneAmt = p.conga_tone * 0.01f;
  const float accent = p.accent * 0.01f;
  const float noiseAmt = p.conga_noise * 0.01f;
  const float pitchEnvAmt = p.conga_pitch_env * 0.001f; // scaled for subtle use

  const float detune = 1.012f;

  // Bandpass filter for noise
  float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
  float b0, b1, b2, a1, a2;
  {
    float w0 = 2.f * (float)M_PI * f0 / SAMPLE_RATE;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2.f * 4.0f); // Q ≈ 4
    b0 = alpha; b1 = 0.f; b2 = -alpha;
    float a0 = 1.f + alpha;
    a1 = -2.f * cosw0;
    a2 = 1.f - alpha;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  }

  for (uint32_t i = 0;i < n;++i) {
    float a = envPercExp(i, decayMs);

    // Pitch envelope: slight downward bend
    float pitchMod = 1.0f + pitchEnvAmt * expf(-0.0008f * (float)i * (1000.f / SAMPLE_RATE));

    // Oscillators
    ph1 += 2.f * (float)M_PI * f0 * pitchMod / SAMPLE_RATE;
    if (ph1 > 2.f * (float)M_PI) ph1 -= 2.f * (float)M_PI;
    ph2 += 2.f * (float)M_PI * f0 * detune * pitchMod / SAMPLE_RATE;
    if (ph2 > 2.f * (float)M_PI) ph2 -= 2.f * (float)M_PI;

    float osc = 0.65f * sinf(ph1) + 0.35f * sinf(ph2);

    // Noise -> bandpass filter
    float x0 = ((rand() & 0x7fff) / 16384.f - 1.f);
    float yn = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x0; y2 = y1; y1 = yn;

    // Mix
    float s = nm_mix(osc, osc * (1.0f - noiseAmt) + yn * noiseAmt, toneAmt);

    // Envelope + accent
    s *= a * accent;
 if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
    }
    psram_buf[i] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
  }
}

// ------------------------------- Universal TR-808/TR-909 Rimshot Generator -------------------------------
static inline void generateRimshot(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);

  const float accent = p.accent * 0.01f;
  const float driveAmt = p.rim_drive * 0.01f;
  const float toneBal = p.rim_tone * 0.01f;
  const float baseFreq = (float)p.rim_pitch;  // center frequency of resonant ping

  // Decay envelope: longer for 808, shorter for 909
  const float decayMs = u8_to_ms_fast(p.rim_decay, 40.f, mode909 ? 150.f : 250.f);

  // High-Q resonant bandpass filter for tonal body
  float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
  float b0, b1, b2, a1, a2;
  {
    float w0 = 2.f * (float)M_PI * baseFreq / SAMPLE_RATE;
    float cosw0 = cosf(w0);
    float Q = mode909 ? 2.5f : 6.0f;   // 808 more resonant, 909 slightly looser
    float alpha = sinf(w0) / (2.f * Q);
    b0 = alpha; b1 = 0.f; b2 = -alpha;
    float a0 = 1.f + alpha;
    a1 = -2.f * cosw0;
    a2 = 1.f - alpha;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  }

  for (uint32_t i = 0;i < n;++i) {
    float a = envPercExp(i, decayMs);

    // Excitation: short pulse (~1 ms) dominates tonal ping
    float exc = (i < (uint32_t)(SAMPLE_RATE * 0.001f)) ? 1.f : 0.f;

    // Noise burst: only for 909, very short (~3 ms)
    float noise = 0.f;
    if (mode909 && i < (uint32_t)(SAMPLE_RATE * 0.003f))
      noise = ((rand() & 0x7fff) / 16384.f - 1.f) * 0.2f;

    // Bandpass filter the excitation to create resonant ping
    float in = exc + noise;
    float yn = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = in; y2 = y1; y1 = yn;

    // Mix tonal ping vs noise with weakened tone parameter
    float y = nm_mix(yn, noise, powf(toneBal, 2.0f) * 0.3f);

    // Apply softclip drive
    y = nm_mix(y, nm_softclip(3.0f * y), driveAmt);

    // Apply decay envelope and accent
    y *= a * accent;
 if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
    }
    psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
  }
}
// ------------------------------- Clap ---------------------------------------
static inline void generateClap(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);

  // 808 has a specific burst count, 909 uses the Clap parameter for variations.
  const int bursts = mode909 ? p.clap_repeats : 3;
  const float spread = map01_100(p.clap_spread, 8.f, 24.f) * 0.001f; // seconds total spread
  const int burstGap = (int)((spread / (float)bursts) * SAMPLE_RATE);

  const float tailAmt = p.clap_tail * 0.01f;
  const float decayMs = u8_to_ms_fast(p.clap_decay, 120.f, 900.f);
  const float driveAmt = p.clap_drive * 0.01f;

  // 909 clap uses a different filter
  float bp_q = mode909 ? u8_to_q(p.clap_bp_q) * 0.6f : u8_to_q(p.clap_bp_q);
  SVF bp; bp.set((float)p.clap_bp_hz, bp_q);

  for (uint32_t i = 0;i < n;++i) {
    float y = 0.f;
    // burst cluster
    for (int b = 0; b < bursts; ++b) {
      int idx = (int)i - b * burstGap;
      if (idx >= 0) {
        float w = 1.f - (float)b * (1.f / std::max(1, bursts));
        float lp, bpOut, hp; bp.process(nm_rand_white(), lp, bpOut, hp);
        float envB = 1.f - (float)idx / (float)std::max(1, (int)(0.006f * SAMPLE_RATE));
        if (envB < 0.f) envB = 0.f;
        y += w * bpOut * envB;
      }
    }
    // tail (smoother, longer filtered noise)
    float lp, bpOut, hp; bp.process(nm_rand_white(), lp, bpOut, hp);
    float tail = bpOut * envPercExp(i, decayMs);

    y = nm_mix(y, tail, tailAmt);
    if (mode909) y = nm_softclip(1.8f * y);
    y = nm_mix(y, nm_softclip(2.5f * y), driveAmt);
    y *= (p.accent * 0.01f);
 if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
    }
    psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
  }
}

// ------------------------------- Hi-Hats ------------------------------------
static inline void generateHiHat(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);

  const float freqs808[6] = { 4000, 4800, 5600, 6400, 7200, 8000 };
  const float freqs909[6] = { 5000, 6000, 7000, 8000, 9000, 10000 };
  const float* ff = mode909 ? freqs909 : freqs808;

  float ph[6] = { 0,0,0,0,0,0 };

  float decayMetal = (p.hh_mode == 1) ? u8_to_ms_fast(p.hh_decay, 500.f, 1000.f)
    : u8_to_ms_fast(p.hh_decay, 80.f, 250.f);
  float decayNoise = (p.hh_mode == 1) ? u8_to_ms_fast(p.hh_decay, 600.f, 1200.f)
    : u8_to_ms_fast(p.hh_decay, 80.f, 250.f);

  SVF bp;
  float scaled_q_hh = p.hh_bp_q * (mode909 ? 0.7f : 1.0f);
  bp.set((float)p.hh_bp_hz, u8_to_q((uint8_t)scaled_q_hh));
  OnePole hp; hp.setHP(0.12f);

  for (uint32_t i = 0;i < n;++i) {
    float aMetal = envPercExp(i, decayMetal);
    float aNoise = envPercExp(i, decayNoise);

    float metallic = 0.f;
    for (int k = 0;k < 6;++k) {
      // random phase per note
      if (i == 0) ph[k] = ((rand() & 0xffff) / 65535.f) * 2.f * (float)M_PI;

      // small detune scaled by base frequency
      float freqDetune = ff[k] * 0.005f * (((rand() & 0x3fff) / 16384.f) - 0.5f); // ±0.25% of freq
      float freq = ff[k] + freqDetune;

      ph[k] += 2.f * (float)M_PI * freq / SAMPLE_RATE;
      if (ph[k] > 2.f * (float)M_PI) ph[k] -= 2.f * (float)M_PI;
      float amp = (p.hh_mode == 1) ? 0.6f : 0.55f;
      metallic += amp * (sinf(ph[k]) >= 0.f ? 1.f : -1.f);
    }
    metallic /= 6.f;

    float mlp, mbp, mhp; bp.process(metallic, mlp, mbp, mhp);
    metallic = nm_mix(metallic, mbp, 0.5f) * aMetal;

    float noise = hp.process(nm_rand_white()) * aNoise * ((p.hh_mode == 1) ? 0.25f : 0.4f);
    float mixFactor = (p.hh_mode == 1) ? 0.25f : 0.35f;
    float y = nm_mix(metallic, noise, mixFactor);

    y = nm_mix(y, nm_softclip(2.0f * y), p.hh_drive * 0.01f);
    y *= p.accent * 0.01f;
 if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
    }
    psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
  }
}

// ------------------------------- Crash --------------------------------------
// ------------------------------- Crash --------------------------------------
static inline void generateCrash(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);

  const float freqs808[6] = { 450, 630, 810, 1200, 1800, 2700 };
  const float freqs909[7] = { 450, 630, 810, 1200, 1800, 2700, 4100 };
  const float* ff = mode909 ? freqs909 : freqs808;
  const int nPartials = mode909 ? 7 : 6;

  float ph[7] = { 0 };

  float decayMetal = mode909
    ? u8_to_ms_fast(p.crash_decay, 1800.f, 5000.f)
    : u8_to_ms_fast(p.crash_decay, 1200.f, 4000.f);

  float decayNoise = mode909
    ? u8_to_ms_fast(p.crash_decay, 2200.f, 5500.f)
    : u8_to_ms_fast(p.crash_decay, 1500.f, 4500.f);

  SVF bp;

  float crashQ = 1.0f + ((float)p.crash_bp_q / 100.0f) * 0.9f;
  bp.set((float)p.crash_bp_hz, crashQ);
  OnePole hp; hp.setHP(0.1f);

  // Gain compensation to prevent filter overloading
  float gainComp = 1.0f / crashQ;

  for (uint32_t i = 0; i < n; ++i) {
    float aMetal = envPercExp(i, decayMetal);
    float aNoise = envPercExp(i, decayNoise);

    float metallic = 0.f;
    for (int k = 0; k < nPartials; ++k) {
      if (i == 0) ph[k] = ((rand() & 0xffff) / 65535.f) * 2.f * (float)M_PI;

      float det = ff[k] * 0.003f * (((rand() & 0x3fff) / 16384.f) - 0.5f);
      float freq = ff[k] + det;

      ph[k] += 2.f * (float)M_PI * freq / SAMPLE_RATE;
      if (ph[k] > 2.f * (float)M_PI) ph[k] -= 2.f * (float)M_PI;

      metallic += (sinf(ph[k]) >= 0.f ? 1.f : -1.f);
    }
    metallic /= (float)nPartials;
    metallic *= aMetal;

    float mlp, mbp, mhp;
    bp.process(nm_rand_white(), mlp, mbp, mhp);
    // Apply gain compensation to filtered noise
    float noise = hp.process(mbp) * aNoise * (mode909 ? 0.35f : 0.4f) * gainComp;

    float y = nm_mix(metallic, noise, p.crash_tone * 0.01f);

    if (mode909) {
      y = nm_softclip(1.5f * y);
    }

    y *= p.accent * 0.01f;
     if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
    }
    else
    psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
  }
}


// ------------------------------- Ride ---------------------------------------
static inline void generateRide(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);

  const float freqs808[6] = { 300, 450, 900, 1350, 2025, 4050 };
  const float freqs909[7] = { 320, 480, 960, 1440, 2160, 4320, 6400 };
  const float* ff = mode909 ? freqs909 : freqs808;
  const int nPartials = mode909 ? 7 : 6;

  float ph[7] = { 0 };

  float decayMetal = mode909
    ? u8_to_ms_fast(p.ride_decay, 2500.f, 6000.f)
    : u8_to_ms_fast(p.ride_decay, 2000.f, 5000.f);

  float decayNoise = mode909
    ? u8_to_ms_fast(p.ride_decay, 2800.f, 6500.f)
    : u8_to_ms_fast(p.ride_decay, 2200.f, 5500.f);

  SVF bp;
  float rideQ = 1.0f + ((float)p.ride_bp_q / 100.0f) * 0.10f;
  bp.set((float)p.ride_bp_hz, rideQ);
  OnePole hp; hp.setHP(0.1f);

  // Gain compensation to prevent filter overloading
  float gainComp = 1.0f / rideQ;

  for (uint32_t i = 0; i < n; ++i) {
    float aMetal = envPercExp(i, decayMetal);
    float aNoise = envPercExp(i, decayNoise);

    float metallic = 0.f;
    for (int k = 0; k < nPartials; ++k) {
      if (i == 0) ph[k] = ((rand() & 0xffff) / 65535.f) * 2.f * (float)M_PI;

      float det = ff[k] * 0.002f * (((rand() & 0x3fff) / 16384.f) - 0.5f);
      float freq = ff[k] + det;

      ph[k] += 2.f * (float)M_PI * freq / SAMPLE_RATE;
      if (ph[k] > 2.f * (float)M_PI) ph[k] -= 2.f * (float)M_PI;

      metallic += (sinf(ph[k]) >= 0.f ? 1.f : -1.f);
    }
    metallic /= (float)nPartials;
    metallic *= aMetal;

    float mlp, mbp, mhp;
    bp.process(nm_rand_white(), mlp, mbp, mhp);
    // Apply gain compensation to filtered noise
    float noise = hp.process(mbp) * aNoise * (mode909 ? 0.3f : 0.35f) * gainComp;

    float y = nm_mix(metallic, noise, p.ride_tone * 0.01f);

    if (mode909) {
      y = nm_softclip(1.4f * y);
    }

    y *= p.accent * 0.01f;
     if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
    }
    else
    psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
  }
}

FLASHMEM void generateCowbell(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909,uint8_t channels) {
  const bool stereo = (channels == 2);

  float phase1 = 0.f, phase2 = 0.f;
  const float f1 = (float)p.cb_freq1;
  const float f2 = (float)p.cb_freq2;
  const float balance = p.cb_tone_balance * 0.01f;
  const float noiseAmt = p.cb_noise * 0.01f;
  const float driveAmt = p.cb_drive * 0.01f;

  // Very short envelope for the characteristic 808 cowbell punch
  for (uint32_t i = 0; i < n; ++i) {
    // Simple decay envelope (no attack, sustain, or release)
    float env = expf(-15.0f * (float)i / (float)n); // Fast exponential decay

    // Generate the two tone components
    phase1 += 2.f * PI * f1 / SAMPLE_RATE;
    if (phase1 >= 2.f * PI) phase1 -= 2.f * PI;

    phase2 += 2.f * PI * f2 / SAMPLE_RATE;
    if (phase2 >= 2.f * PI) phase2 -= 2.f * PI;

    float tone1 = sinf(phase1);
    float tone2 = sinf(phase2);

    // Mix the two tones according to balance
    float tone = (1.0f - balance) * tone1 + balance * tone2;

    // Add a small amount of noise
    float noise = nm_rand_white() * noiseAmt;

    // Combine and apply envelope
    float s = (tone + noise) * env;

    // Apply gentle saturation
    s = (1.f - driveAmt) * s + driveAmt * tanhf(2.0f * s);

 if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(clamp1(s) * 32767.f);
      psram_buf[i * channels + 1] =(int16_t)lrintf(clamp1(s) * 32767.f);
    }
    else
    psram_buf[i] = (int16_t)lrintf(clamp1(s) * 32767.f);
  }
}

// ------------------------------- Claves (808) --------------------------------
static inline void generateClaves(NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);

  float ph = 0.f;
  const float decayMs = u8_to_ms_fast(p.claves_decay, 20.f, 300.f);
  const float drive = p.claves_drive * 0.01f;
  for (uint32_t i = 0;i < n;++i) {
    float a = envPercExp(i, decayMs);
    ph += 2.f * (float)M_PI * p.claves_hz / SAMPLE_RATE; if (ph > 2.f * (float)M_PI) ph -= 2.f * (float)M_PI;
    float s = sinf(ph);
    s = nm_mix(s, nm_softclip(2.0f * s), drive);
    s *= a * (p.accent * 0.01f);

    if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
    }
    else
      psram_buf[i] = (int16_t)lrintf(nm_clip1(s) * 32767.f);
  }
}

// Simple random float helper: uniform range [min, max]
auto randf_range = [](float minVal, float maxVal) {
  return minVal + (maxVal - minVal) * (random(0, 10001) / 10000.0f);
  };


// ------------------------------- Maracas (Authentic 808) -------------------------------
// ------------------------------- Maracas (808-style, Multi-Grain) -------------------------------
static inline void generateMaracas(NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
  const bool stereo = (channels == 2);

  const float sr = SAMPLE_RATE;
  const float accentGain = (p.accent * 0.01f);

  // --- Envelope settings ---
  const float totalMs = u8_to_ms_fast(p.mar_decay, 25.f, 120.f); // total AR
  const float attackMs = totalMs * 0.7f;
  const float releaseMs = totalMs * 0.3f;
  const uint32_t atkSamples = (uint32_t)(attackMs * sr / 1000.f);
  const uint32_t relSamples = (uint32_t)(releaseMs * sr / 1000.f);
  const float attackSlope = 1.0f / max(1u, atkSamples);
  const float releaseSlope = 1.0f / max(1u, relSamples);

  // --- Band-pass settings ---
  float cutoffHz = float(p.mar_bp_hz);                  // 1–6 kHz
  float q = 0.7f + (float(p.mar_bp_q) / 100.0f) * 0.5f; // maps 50–100% → 0.95–1.2

  SVF bp;
  bp.set(cutoffHz, q); // set once per buffer

  // --- Multi-grain offsets (simulates multiple shakers) ---
  const int numGrains = 3;
  int grainOffsets[numGrains] = { 0, 3, 7 };

  for (uint32_t i = 0; i < n; ++i) {
    float env = 0.0f;
    // Sum multiple grains
    for (int g = 0; g < numGrains; ++g) {
     // uint32_t idx = (i >= grainOffsets[g]) ? i - grainOffsets[g] : 0;
       uint32_t offset = static_cast<uint32_t>(grainOffsets[g]);
        uint32_t idx = (i >= offset) ? i - offset : 0;
      float e = 0.0f;
      if (idx < atkSamples) e = float(idx) * attackSlope;
      else if (idx < atkSamples + relSamples) e = 1.0f - (float(idx - atkSamples) * releaseSlope);
      env += e;
    }
    env = env / numGrains; // normalize

    // --- Noise source ---
    float noise = nm_rand_white();
    float shaped = noise * env * accentGain;

    // --- Band-pass filter ---
    float lp, bpOut, hp;
    bp.process(shaped, lp, bpOut, hp);

    if (stereo) {
      psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(bpOut) * 32767.f);
      psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(bpOut) * 32767.f);
    }
    else
      psram_buf[i] = (int16_t)lrintf(nm_clip1(bpOut) * 32767.f);

  }
}

static inline void generateZap(const NoiseMakerParams& p, int16_t* psram_buf, uint32_t n, bool mode909, uint8_t channels) {
    const bool stereo = (channels == 2);

    // Assuming these parameters exist in your NoiseMakerParams struct
    const float startHz = (float)p.zap_pitch_start;
    const float endHz = (float)p.zap_pitch_end;
    const float decayMs = u8_to_ms_fast(p.zap_decay, 10.0f, 100.0f);
    const float accent = p.accent * 0.01f;

    float phase = 0.0f;
    const float totalSamples = SAMPLE_RATE * decayMs / 1000.0f;

    for (uint32_t i = 0; i < n; ++i) {
        // Linear interpolation for pitch sweep
        const float pitch_t = (float)i / totalSamples;
        const float currentHz = startHz + (endHz - startHz) * pitch_t;
        
        // Update phase based on the current frequency
        phase += 2.0f * (float)M_PI * currentHz / SAMPLE_RATE;
        if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        
        // Generate sine wave
        float sine_wave = sinf(phase);

        // Apply a short decay envelope
        const float env = envPercExp(i, decayMs);
        
        float y = sine_wave * env * accent;

        // Clip and convert to 16-bit
        if (stereo) {
            psram_buf[i * channels] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
            psram_buf[i * channels + 1] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
        } else {
            psram_buf[i] = (int16_t)lrintf(nm_clip1(y) * 32767.f);
        }
    }
}


static inline uint32_t getSampleLength(const NoiseMakerParams& p) {
  float lenMs = 0.0f;
  switch (p.category) {
  case NM_BD: {
    float decayMs = u8_to_ms_fast(p.bd_decay, 60.f, 800.f);
    float releaseMs = u8_to_ms_fast(p.bd_release, 20.f, 600.f);
    float pitchTail = (float)p.bd_pitch_decay * (p.mode == NM_MODE_TR909 ? 0.5f : 1.0f);
    lenMs = decayMs + releaseMs + pitchTail;
    lenMs = std::max(120.0f, lenMs);
  } break;

  case NM_SD: {
    float d = u8_to_ms_fast(p.sn_decay, 60.f, 900.f);
    lenMs = d + 80.0f;
  } break;

  case NM_TOM: {
    float d = u8_to_ms_fast(p.tom_decay, 80.f, 1200.f);
    lenMs = d + 40.0f;
  } break;

  case NM_CONGA: {
    float d = u8_to_ms_fast(p.conga_decay, 80.f, 1000.f);
    lenMs = d + 20.0f;
  } break;

  case NM_RIM: {
    float d = u8_to_ms_fast(p.rim_decay, 10.f, 200.f);
    lenMs = d + 10.0f;
  } break;

  case NM_CLAP: {
    float d = u8_to_ms_fast(p.clap_decay, 120.f, 900.f);
    float spreadMs = map01_100(p.clap_spread, 8.f, 24.f);
    lenMs = d + spreadMs + 40.0f;
  } break;

  case NM_HH: {
    if (p.hh_mode == 1) { // open hat
      float d = u8_to_ms_fast(p.hh_decay, 500.f, 1200.f);
      lenMs = d + 40.0f;
    }
    else {
      float d = u8_to_ms_fast(p.hh_decay, 80.f, 250.f);
      lenMs = d + 20.0f;
    }
  } break;

  case NM_CRASH: {
    lenMs = u8_to_ms_fast(p.crash_decay, 1000.f, 4000.f);
    lenMs = std::max(lenMs, 1000.f);
    lenMs += 60.f;

  }break;
  case NM_RIDE: {
    lenMs = u8_to_ms_fast(p.ride_decay, 1500.f, 5000.f);
    lenMs = std::max(lenMs, 1500.f);
    lenMs += 60.f;

  }break;

  case NM_COWBELL: {
    float d = u8_to_ms_fast(p.cb_amp_decay, 100.f, 500.f);
    lenMs = d + 10.0f;
  } break;

  case NM_CLAVES: {
    float d = u8_to_ms_fast(p.claves_decay, 20.f, 300.f);
    lenMs = d + 10.0f;
  } break;

  case NM_MARACAS: {
    float d = u8_to_ms_fast(p.mar_decay, 40.f, 160.f);  // 808 is short & crisp
    lenMs = d + 10.0f;
  } break;

  default: lenMs = 500.0f; break;
  }

  // Safety clamp in ms (float)
  lenMs = std::min<float>(lenMs, 5000.0f); // max 5s
  lenMs = std::max<float>(lenMs, 32.0f);   // min 32ms

  // Convert to samples (rounded)
  return msToSamples(lenMs);
}



// ============================================================================
// DISPATCHER (call with precomputed n)
// ============================================================================
static inline void generateDrum(NoiseMakerParams& p, int16_t* psram_buf, uint32_t n,uint8_t channels) {
  switch (p.category) {
  case NM_BD:      generateBassdrum(p, psram_buf, n, p.mode, channels); break;
  case NM_SD:      generateSnare(p, psram_buf, n, p.mode, channels); break;
  case NM_TOM:      generateTom(p, psram_buf, n, p.mode, channels); break;
  case NM_CONGA:      generateConga(p, psram_buf, n, p.mode, channels); break;
  case NM_RIM:     generateRimshot(p, psram_buf, n, p.mode, channels); break;
  case NM_CLAP:    generateClap(p, psram_buf, n, p.mode, channels); break;
  case NM_HH:      generateHiHat(p, psram_buf, n, p.mode, channels); break;
  case NM_CRASH:   generateCrash(p, psram_buf, n, p.mode, channels); break;
  case NM_RIDE:    generateRide(p, psram_buf, n, p.mode, channels); break;
  case NM_COWBELL: generateCowbell(p, psram_buf, n, p.mode, channels); break;
  case NM_CLAVES:  generateClaves(p, psram_buf, n, p.mode, channels); break;
  case NM_MARACAS: generateMaracas(p, psram_buf, n, p.mode, channels); break;
  case NM_ZAP: generateZap(p, psram_buf, n, p.mode, channels); break;
  default: // silence
    for (uint32_t i = 0;i < n;++i) psram_buf[i] = 0;
    break;
  }
}

// Update generateNoisePreview to handle stereo
FLASHMEM void generateNoisePreview() {

    // Compute how many samples we need for the current instrument
    uint32_t length = getSampleLength(nm_params);
    if (length == 0) return;

    // Determine channel count
    uint8_t channels = (nm_params.stereo_mode == 0) ? 1 : 2;
    // Allocate or reallocate the PSRAM preview buffer
    if (!nm_preview_alloc(length, channels)) return;

    // Generate the waveform directly into the PSRAM buffer
    generateDrum(nm_params, nm_preview_data, length, channels);
}

extern  void print_nm_style();

void randomizeNoiseMakerParams() {


  if (nm_params.randomizer)
  {
    // Randomize mode (808 or 909)
    nm_params.mode = random(0, 2);
  }
  print_nm_style();
  // Randomize accent level
  //nm_params.accent = 40;

  // Bass Drum parameters
  if (nm_params.mode == NM_MODE_TR909)
    nm_params.bd_pitch = random(10, 80);        // 30-79 Hz
  else
    nm_params.bd_pitch = random(10, 60);        // 30-79 Hz

  nm_params.bd_pitch_env = random(20, 95);    // 0-100%
  nm_params.bd_pitch_decay = random(5, 95); // 100-500 ms
  nm_params.bd_decay = random(10, 60);       // 30-100%
  nm_params.bd_attack = random(0, 1);        // 0-10%
  nm_params.bd_release = random(5, 70);     // 10-100%
  nm_params.bd_tone = random(20, 80);        // 50-100%
  nm_params.bd_click = random(0, 81);         // 0-50%
  nm_params.bd_noise = random(0, 31);         // 0-20%
  nm_params.bd_drive = random(0, 31);         // 0-30%
  nm_params.bd_comp = random(0, 21);          // 0-20%

  // Snare parameters
  // The first tone is the fundamental body, with a focused range for more musical sounds.
  nm_params.sn_tone1 = random(150, 350);

  // The second tone is the higher harmonic, with a range that complements the first.
  nm_params.sn_tone2 = random(250, 500);

  // The mix between the two tones should be broad to allow for various timbres.
  nm_params.sn_tone_mix = random(20, 101);

  // A noticeable pitch drop is key for a percussive attack.
  nm_params.sn_pitch_env = random(50, 101);

  // Decay provides a wide range from tight to ringing snares.
  nm_params.sn_decay = random(40, 101);

  // Noise level is crucial for the sizzle; a high range is suitable.
  nm_params.sn_noise = random(40, 99);

  // The noise filter frequency is now more focused on the high-end.
  nm_params.sn_bp_freq = random(1500, 6000);

  // The Q factor is set for a more resonant, musical sound.
  nm_params.sn_bp_q = random(0, 25);

  // The snap transient provides the initial punch.
  nm_params.sn_snap = random(0, 101);

  // Drive adds subtle soft clipping for grit.
  nm_params.sn_drive = random(0, 21);

  // Tom parameters
  nm_params.tom_pitch = random(60, 301);      // 80-300 Hz
  nm_params.tom_decay = random(10, 30);      // 40-100%
  nm_params.tom_pitch_env = random(0, 80);   // 0-100%
  nm_params.tom_tone = random(50, 101);       // 50-100%
  nm_params.tom_noise = random(0, 21);        // 0-20%

  // Conga parameters
  nm_params.conga_pitch = random(180, 501);  // 180–300 Hz base
  nm_params.conga_decay = random(10, 40);   // 40–100%
  nm_params.conga_tone = random(30, 100);   // 30–100%
  nm_params.conga_pitch_env = random(0, 51);     // 0–40% (subtle bend)
  nm_params.conga_noise = random(0, 81);    // 10–60% (woodiness)

  nm_params.rim_decay = random(20, 81);      // 20–80% decay
  nm_params.rim_tone = random(20, 101);     // 20–100% (tone vs noise)
  nm_params.rim_pitch = random(200, 2801);  // 1.2–2.8 kHz
  nm_params.rim_drive = random(0, 21);       // 0–20% drive


  // Clap parameters
  nm_params.clap_repeats = random(2, 8);      // 2-5 repeats
  nm_params.clap_spread = random(20, 101);    // 20-100%
  nm_params.clap_tail = random(40, 101);      // 40-100%
  nm_params.clap_bp_hz = random(0, 800);   // 200-2000 Hz
  nm_params.clap_bp_q = random(0, 40);      // 50-100%
  nm_params.clap_decay = random(50, 101);     // 50-100%
  nm_params.clap_drive = random(0, 21);       // 0-20%

  nm_params.hh_mode = random(0, 2);        // 0=closed, 1=open
  nm_params.hh_tone = random(30, 50);      // metallic %
  nm_params.hh_noise = random(40, 70);      // noise %
  nm_params.hh_bp_hz = random(2500, 8000);  // bandpass center Hz
  nm_params.hh_bp_q = random(6, 12);       // bandpass Q
  nm_params.hh_decay = random(50, 80);      // decay %
  nm_params.hh_drive = random(0, 20);       // drive %
  nm_params.hh_detune = random(2, 5);        // oscillator detune

  // Crash parameters
  nm_params.crash_tone = random(40, 101);     // 40-100%
  nm_params.crash_bp_hz = random(5000, 8001); // 5000-8000 Hz
  nm_params.crash_bp_q = random(0, 50);     // 40-100%
  nm_params.crash_decay = random(70, 101);    // 70-100%

  // Ride parameters
  nm_params.ride_tone = random(40, 101);      // 40-100%
  nm_params.ride_bp_hz = random(6000, 9001);  // 6000-9000 Hz
  nm_params.ride_bp_q = random(0, 50);      // 50-100%
  nm_params.ride_decay = random(80, 101);     // 80-100%

  // Cowbell parameters
  nm_params.cb_freq1 = random(440, 680);
  nm_params.cb_freq2 = random(700, 1040);
  nm_params.cb_amp_attack = 0;
  nm_params.cb_amp_decay = random(25, 245);
  nm_params.cb_tone_balance = random(20, 80);
  nm_params.cb_noise = random(0, 10);
  nm_params.cb_drive = random(0, 60);

nm_params.zap_pitch_start = random(4000, 10000); // Varies the starting pitch of the sweep
nm_params.zap_pitch_end = random(100, 800);      // Changes the ending pitch, from a sharp zap to a bassy thud
nm_params.zap_decay = random(10, 100);       // Randomizes the length of the sound


  // Claves parameters
  nm_params.claves_hz = random(1500, 2001);   // 1500-2000 Hz
  nm_params.claves_decay = random(20, 101);   // 20-100%
  nm_params.claves_drive = random(0, 11);     // 0-10%

  // Maracas parameters
  nm_params.mar_bp_hz = random(2000, 6001);  // 1–6 kHz
  nm_params.mar_bp_q = random(50, 95);     // 50–100%
  nm_params.mar_decay = random(20, 71);      // 30–80%
}

#endif
