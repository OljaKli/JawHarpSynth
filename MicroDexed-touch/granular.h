#ifdef GRANULAR

#ifndef GRANULAR_H
#define GRANULAR_H

#include <Audio.h>
#include <Arduino.h>

// Granular parameters with normalized values (0-100)
typedef struct {
  uint8_t sample_note;        // Sample index (0-based)
  uint8_t grain_size;         // Grain size (0-100 maps to 10-1000ms)
  uint8_t grain_position;     // Position in sample (0-100 maps to 0.0-1.0)
  int8_t semitones;           // Pitch shift in semitones (-36 to +36, ±3 octaves)
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
  void init();
  void startGrain(const int16_t* sampleData, uint32_t sampleLength, const granular_params_t& params);
  void update(int16_t* left, int16_t* right, uint16_t blockSize);
  int getActiveGrainCount();
  void stopAllGrains();
  void setParams(const granular_params_t& params);
  void triggerNote(uint8_t note, uint8_t velocity);
  void noteOff();
  void setSampleData(const int16_t* data, uint32_t length);
    bool isPlaying();  
     bool inRelease;   // true when the note is in the release phase

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

  static const int MAX_GRAINS = 32;
  Grain grains[MAX_GRAINS];
  granular_params_t currentParams;
  uint32_t lastGrainTime;
  uint32_t grainInterval;     // Time between grain spawns (based on density)
  bool noteOn;
  uint8_t currentNote;
  uint8_t currentVelocity;
  const int16_t* globalSampleData;
  uint32_t globalSampleLength;
  uint32_t timeCounter;       // For tracking when to spawn new grains

  float envelopeLevel;        
  bool envelopeActive;        
  uint32_t envelopeTimeCounter; // FIXED: Separate counter for envelope timing
  uint32_t releaseStartTime;  
  static const uint32_t RELEASE_TIME; // Declaration only

};




class AudioChromaticGranular : public AudioStream {
public:
  AudioChromaticGranular() : AudioStream(0, NULL) { 
    processor = nullptr;
  }
  void begin();
  void init(); 
  void startGrain(const int16_t* sampleData, uint32_t sampleLength, const granular_params_t& params);
  int getActiveGrainCount();
  virtual void update(void);
  void stopAllGrains();
  void setParams(const granular_params_t& params);
  void triggerNote(uint8_t note, uint8_t velocity);
  void noteOff();
  void setSampleData(const int16_t* data, uint32_t length);
  bool isPlaying();  // This declaration exists but was missing implementation

private:
  ChromaticGranularProcessor* processor;
};

// Global declarations
extern granular_params_t granular_params;
extern bool granular_initialized;
extern AudioChromaticGranular granular1;

// Function declarations
void initGranular();
void setGranularActive(bool active);
void updateGranularParams(granular_params_t newParams);

#endif

#endif