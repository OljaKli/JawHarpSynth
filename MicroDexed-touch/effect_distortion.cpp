/*
 * AudioEffectDistortion.cpp
 * 
 * Implementation of distortion effect for Teensy Audio Library
 */

#include "effect_distortion.h"

AudioEffectDistortion::AudioEffectDistortion() : AudioStream(1, inputQueueArray) {
  _drive = 0.5f;
  _outputGain = 1.0f;
}

void AudioEffectDistortion::update(void) {
  audio_block_t *block;
  int16_t *p;
  
  block = receiveWritable();
  if (!block) return;
  
  p = block->data;
  
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    // Convert to float -1.0 to 1.0
    float sample = (float)(*p) / 32768.0f;
    
    // Process distortion
    float distorted = processDistortion(sample);
    
    // Apply output gain compensation
    distorted *= _outputGain;
    
    // Convert back to int16 with clipping
    int32_t val = (int32_t)(distorted * 32767.0f);
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    
    *p++ = (int16_t)val;
  }
  
  transmit(block);
  release(block);
}

float AudioEffectDistortion::processDistortion(float sample) {
  // Apply distortion (soft clipping with adjustable drive)
  float driven = sample * (1.0f + _drive * 9.0f); // drive range: 1x to 10x
  
  float distorted;
  
  // Soft clipping using tanh-like approximation (fast)
  if (driven > 1.0f) {
    distorted = 1.0f - (1.0f / (driven * driven + 1.0f));
  } else if (driven < -1.0f) {
    distorted = -1.0f + (1.0f / (driven * driven + 1.0f));
  } else {
    // Cubic soft clip for moderate levels
    distorted = driven - (driven * driven * driven) / 3.0f;
  }
  
  return distorted;
}

void AudioEffectDistortion::setDrive(float drive) {
  if (drive < 0.0f) drive = 0.0f;
  if (drive > 1.0f) drive = 1.0f;
  _drive = drive;
  // Compensate output level as drive increases
  _outputGain = 1.0f / (1.0f + drive * 0.5f);
}

void AudioEffectDistortion::setOutputGain(float gain) {
  if (gain < 0.0f) gain = 0.0f;
  if (gain > 2.0f) gain = 2.0f;
  _outputGain = gain;
}