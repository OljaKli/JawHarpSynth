#ifdef GRANULAR

#ifndef GRANULAR_H
#define GRANULAR_H

#include <Audio.h>
#include <Arduino.h>

// Granular parameters with normalized values (0-100)
typedef struct {
  uint8_t sample_note;        // MIDI note that selects the sample (0-127)
  uint8_t grain_size;         // Grain size (0-100 maps to 10-1000ms)
  uint8_t grain_position;     // Position in sample (0-100 maps to 0.0-1.0)
  uint8_t semitones;          // Pitch shift (0-100 maps to -24 to +24 semitones)
  uint8_t volume;             // Volume (0-100)
  uint8_t density;            // Grains per second (0-100 maps to 1-100)
  uint8_t spread;             // Position randomization (0-100 maps to 0.0-1.0)
  uint8_t pitch_random;       // Pitch randomization (0-100 maps to 0-12 semitones)
  uint8_t filter_mode;        // Filter mode (0-3)
  uint8_t filter_freq;        // Filter frequency (0-100 maps to 50-10000Hz)
  uint8_t filter_resonance;   // Filter resonance (0-100)
  uint8_t rev_send;           // Reverb send level (0-100)
  uint8_t delay_send_1;       // Delay 1 send level (0-100)
  uint8_t delay_send_2;       // Delay 2 send level (0-100)
  uint8_t pan;                // Panorama (0-100 maps to -99 to +99)
  uint8_t midi_channel;       // MIDI channel (0-16, 17=Omni)
  uint8_t active;             // Whether granular synth is active
} granular_params_t;

// Chromatic granular processor with pitch shifting
class ChromaticGranularProcessor {
public:
  void init(uint32_t sampleRate = 44100);
  void startGrain(const int16_t* sampleData, uint32_t sampleLength, const granular_params_t& params);
  void process(int16_t* left, int16_t* right, uint16_t blockSize);
  int getActiveGrainCount();

private:
  struct Grain {
    const int16_t* sampleData;
    uint32_t sampleLength;
    float position;
    float size;
    float pitchRatio;
    float volume;
    uint32_t age;
    bool active;
  };

  static const int MAX_GRAINS = 128;
  static const int WINDOW_SIZE = 1024;
  Grain grains[MAX_GRAINS];
  float window[WINDOW_SIZE];
  uint32_t sampleRate;

  void processGrain(Grain& grain, int16_t* left, int16_t* right, uint16_t blockSize);
  
  // Mapping functions
  float mapGrainSize(uint8_t value) { return 10.0f + (value / 100.0f) * 990.0f; } // 10-1000ms
  float mapGrainPosition(uint8_t value) { return value / 100.0f; } // 0.0-1.0
  int mapSemitones(uint8_t value) { return (value / 100.0f) * 48 - 24; } // -24 to +24
  float mapVolume(uint8_t value) { return value / 100.0f; } // 0.0-1.0
  uint8_t mapDensity(uint8_t value) { return 1 + (value / 100.0f) * 99; } // 1-100 grains/sec
  float mapSpread(uint8_t value) { return value / 100.0f; } // 0.0-1.0
  uint8_t mapPitchRandom(uint8_t value) { return (value / 100.0f) * 12; } // 0-12 semitones
  uint16_t mapFilterFreq(uint8_t value) { return 50 + (value / 100.0f) * 9950; } // 50-10000Hz
  int8_t mapPan(uint8_t value) { return (value / 100.0f) * 198 - 99; } // -99 to +99
};

// Audio adapter to integrate with Teensy Audio library
class AudioChromaticGranular : public AudioStream {
public:
  AudioChromaticGranular();
  void init(uint32_t sampleRate = 44100);
  void startGrain(const int16_t* sampleData, uint32_t sampleLength, const granular_params_t& params);
  int getActiveGrainCount();
  virtual void update(void);

private:
  ChromaticGranularProcessor* processor;
};

#endif

#endif
