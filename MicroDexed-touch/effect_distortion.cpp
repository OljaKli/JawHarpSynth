/*
 * AudioEffectDistortion.cpp
 * 
 * Implementation of distortion effect for Teensy Audio Library
 */

#include "effect_distortion.h"

AudioEffectDistortion::AudioEffectDistortion() : AudioStream(1, inputQueueArray) {
  _drive = 0.5f;
  _outputGain = 1.0f;
  _preEmph_z1 = 0.0f;
  _deEmph_z1 = 0.0f;
}

void AudioEffectDistortion::update(void) {
  audio_block_t *block;
  int16_t *p;
  
  block = receiveWritable();
  if (!block) return;
  
  p = block->data;
  
  // Pre-emphasis coefficient (emphasize mids like a guitar pedal)
  const float preCoef = 0.8f;
  // De-emphasis coefficient (smooth out harsh highs)
  const float deCoef = 0.7f;
  
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    // Convert to float -1.0 to 1.0
    float sample = (float)(*p) / 32768.0f;
    
    // Pre-emphasis filter (high-pass character - emphasizes pick attack)
    float preEmph = sample - preCoef * _preEmph_z1;
    _preEmph_z1 = sample;
    
    // Process distortion
    float distorted = processDistortion(preEmph);
    
    // De-emphasis filter (low-pass character - smooths harsh highs)
    float deEmph = deCoef * distorted + (1.0f - deCoef) * _deEmph_z1;
    _deEmph_z1 = deEmph;
    
    // Apply output gain compensation
    deEmph *= _outputGain;
    
    // Convert back to int16 with clipping
    int32_t val = (int32_t)(deEmph * 32767.0f);
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    
    *p++ = (int16_t)val;
  }
  
  transmit(block);
  release(block);
}

// Fast tanh approximation optimized for Teensy 4.1 FPU
float AudioEffectDistortion::fastTanh(float x) {
  // Rational approximation: very accurate for |x| < 3
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float AudioEffectDistortion::processDistortion(float sample) {
  // Apply drive (exponential scaling for more natural feel)
  float gain = 1.0f + _drive * _drive * 19.0f; // 1x to 20x with exponential curve
  float driven = sample * gain;
  
  // Asymmetric soft clipping (tube-like: compresses positive more than negative)
  // This gives the characteristic "sag" of tube amps
  float distorted;
  
  if (driven > 0.0f) {
    // Positive: softer clipping (compressed)
    distorted = fastTanh(driven * 0.9f) * 1.05f;
  } else {
    // Negative: slightly harder clipping (more edge)
    distorted = fastTanh(driven * 1.1f) * 0.95f;
  }
  
  // Add subtle second-harmonic character (even harmonics like tubes)
  float harmonic = distorted * distorted * (driven > 0.0f ? 0.05f : -0.05f);
  distorted += harmonic;
  
  // Soft limit the output
  if (distorted > 1.0f) distorted = 1.0f;
  if (distorted < -1.0f) distorted = -1.0f;
  
  return distorted;
}

void AudioEffectDistortion::setDrive(float drive) {
  if (drive < 0.0f) drive = 0.0f;
  if (drive > 1.0f) drive = 1.0f;
  _drive = drive;
  // Better gain compensation for exponential drive curve
  _outputGain = 1.0f / (1.0f + drive * drive * 1.2f);
}

void AudioEffectDistortion::setOutputGain(float gain) {
  if (gain < 0.0f) gain = 0.0f;
  if (gain > 2.0f) gain = 2.0f;
  _outputGain = gain;
}