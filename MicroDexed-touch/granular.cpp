#ifdef GRANULAR

#include "granular.h"

void ChromaticGranularProcessor::init(uint32_t sampleRate) {
  this->sampleRate = sampleRate;
  memset(grains, 0, sizeof(grains));
  
  // Pre-calculate window function for efficiency
  for (int i = 0; i < WINDOW_SIZE; i++) {
    window[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (WINDOW_SIZE - 1));
  }
}

void ChromaticGranularProcessor::startGrain(const int16_t* sampleData, uint32_t sampleLength, const granular_params_t& params) {
  // Find an available grain slot
  for (int i = 0; i < MAX_GRAINS; i++) {
    if (!grains[i].active) {
      grains[i].sampleData = sampleData;
      grains[i].sampleLength = sampleLength;
      grains[i].position = mapGrainPosition(params.grain_position) * sampleLength;
      grains[i].size = (mapGrainSize(params.grain_size) / 1000.0f) * sampleRate;
      
      // Calculate base pitch with randomization
      float randomPitch = (random(0, mapPitchRandom(params.pitch_random) * 200) - mapPitchRandom(params.pitch_random) * 100) / 100.0f;
      grains[i].pitchRatio = pow(2.0, (mapSemitones(params.semitones) + randomPitch) / 12.0);
      
      grains[i].volume = mapVolume(params.volume);
      grains[i].age = 0;
      grains[i].active = true;
      break;
    }
  }
}

void ChromaticGranularProcessor::process(int16_t* left, int16_t* right, uint16_t blockSize) {
  memset(left, 0, blockSize * sizeof(int16_t));
  memset(right, 0, blockSize * sizeof(int16_t));
  
  for (int i = 0; i < MAX_GRAINS; i++) {
    if (grains[i].active) {
      processGrain(grains[i], left, right, blockSize);
      
      // Deactivate grain if it's finished
      if (grains[i].age >= grains[i].size) {
        grains[i].active = false;
      }
    }
  }
}

int ChromaticGranularProcessor::getActiveGrainCount() {
  int count = 0;
  for (int i = 0; i < MAX_GRAINS; i++) {
    if (grains[i].active) count++;
  }
  return count;
}

void ChromaticGranularProcessor::processGrain(Grain& grain, int16_t* left, int16_t* right, uint16_t blockSize) {
  for (int i = 0; i < blockSize; i++) {
    if (grain.age < grain.size) {
      // Calculate window position
      int windowIndex = (int)((grain.age / grain.size) * (WINDOW_SIZE - 1));
      float win = window[windowIndex];
      
      // Get sample with linear interpolation
      int idx = (int)grain.position;
      float frac = grain.position - idx;
      
      // Handle wrap-around for sample data
      int16_t s1 = grain.sampleData[idx % grain.sampleLength];
      int16_t s2 = grain.sampleData[(idx + 1) % grain.sampleLength];
      int16_t s = s1 + frac * (s2 - s1);
      
      // Apply window and volume
      float sample = s * win * grain.volume;
      
      // Add to output
      left[i] += sample;
      right[i] += sample;
      
      // Advance position based on pitch ratio
      grain.position += grain.pitchRatio;
      if (grain.position >= grain.sampleLength) grain.position -= grain.sampleLength;
      
      // Increment age
      grain.age++;
    }
  }
}

AudioChromaticGranular::AudioChromaticGranular() : AudioStream(0, NULL) {
  processor = nullptr;
}

void AudioChromaticGranular::init(uint32_t sampleRate) {
  processor = new ChromaticGranularProcessor();
  processor->init(sampleRate);
}

void AudioChromaticGranular::startGrain(const int16_t* sampleData, uint32_t sampleLength, const granular_params_t& params) {
  if (processor) {
    processor->startGrain(sampleData, sampleLength, params);
  }
}

int AudioChromaticGranular::getActiveGrainCount() {
  if (processor) {
    return processor->getActiveGrainCount();
  }
  return 0;
}

void AudioChromaticGranular::update(void) {
  if (!processor) return;

  audio_block_t* block;
  block = allocate();
  if (!block) return;

  processor->process(block->data, block->data, AUDIO_BLOCK_SAMPLES);
  
  transmit(block, 0);
  release(block);
}

#endif
