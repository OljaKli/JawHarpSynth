/*
 * AudioEffectDistortion.h
 * 
 * Simple distortion effect for Teensy Audio Library
 * Implements soft clipping with adjustable drive
 * 
 * Usage:
 *   AudioEffectDistortion distortion;
 *   distortion.setDrive(0.5);  // 0.0 = clean, 1.0 = heavy
 */

#ifndef AudioEffectDistortion_h
#define AudioEffectDistortion_h

#include <Arduino.h>
#include <AudioStream.h>

class AudioEffectDistortion : public AudioStream {
public:
  AudioEffectDistortion();
  
  virtual void update(void);
  
  // Set distortion amount (0.0 = clean, 1.0 = heavy distortion)
  void setDrive(float drive);
  
  // Manual output gain adjustment (0.0 to 2.0)
  void setOutputGain(float gain);
  
  // Get current settings
  float getDrive() const { return _drive; }
  float getOutputGain() const { return _outputGain; }

private:
  audio_block_t *inputQueueArray[1];
  float _drive;
  float _outputGain;
  
  // Internal processing function
  float processDistortion(float sample);
};

#endif