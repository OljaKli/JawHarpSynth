#ifndef PitchSamplePlayer_h
#define PitchSamplePlayer_h

#include "Arduino.h"
#include "AudioStream.h"

// Define a block size for audio processing
#define AUDIO_BLOCK_SAMPLES 128

typedef enum loop_type
{
    looptype_none,
    looptype_repeat,
    looptype_pingpong
} loop_type;

class PitchSamplePlayer : public AudioStream {
public:
    // Constructor
    PitchSamplePlayer();

    // Play a sample from a given memory address.
    // numFrames = number of frames (not samples!), so for stereo numFrames = totalSamples / 2
    void play(int16_t* data, uint32_t numFrames, uint8_t numChannels);

    // Play a raw sample (length = total number of samples, not frames)
    void playRaw(int16_t* data, uint32_t totalLength, uint8_t numChannels);

    // Stop playback
    void stop();

    // Check if the player is currently playing
    bool isPlaying();

    // Set the pitch multiplier (1.0 is original pitch, 2.0 is one octave up)
    void setPlaybackRate(float pitchMultiplier);

    bool enableInterpolation(bool enable);

    void setLoopType(loop_type t);

    void setLoopStart(uint32_t loop_start);

    void setLoopFinish(uint32_t loop_finish);

    // Enable or disable interpolation


    // Enable or disable looping
    void setLooping(bool enable);

    // Alt group helpers (used by voice allocation logic)
    void setVoiceGroup(uint8_t group);
    uint8_t getVoiceGroup() const;
    uint32_t getPlayStartTime() const;

    // This is the main audio processing function called by the audio library
    virtual void update(void);

private:
    // The data buffer
    int16_t* sampleData;

    // Total number of frames (frame = all channels at a given time)
    uint32_t totalFrames;

    // Number of channels in the sample (1 = mono, 2 = stereo)
    uint8_t numChannels;

    // The current playback phase, a floating-point frame index
    float phase;

    // The pitch multiplier
    float pitchMultiplier;

    // Flags
    bool playing;
    bool interpolationEnabled;
    bool loopingEnabled;

    // Voice allocation metadata (non-ISR critical)
    uint8_t voiceGroup;
    uint32_t playStartTime;
};

#endif
