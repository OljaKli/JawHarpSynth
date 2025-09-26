#include "granular.h"
#ifdef GRANULAR

#include <math.h>
#include <string.h>

extern granular_params_t granular_params;

// ---------------- ChromaticGranularVoice ----------------

FLASHMEM ChromaticGranularVoice::ChromaticGranularVoice() {
    sampleData = nullptr;
    sampleLength = 0;
    noteOn = false;
    currentNote = 0;
    currentVelocity = 0;
    envelopeActive = false;
    envelopeLevel = 0.0f;
    envelopeTimeCounter = 0;
    releaseStartTime = 0;
    inRelease = false;
    movingPosition = 0.0f;
    perNotePitchShift = 1.0f;
    memset(&currentParams, 0, sizeof(currentParams));
    
    // Initialize organic variation parameters
    uint32_t voiceSeed = (uint32_t)this + micros();
    randState = (uint16_t)voiceSeed;
    voiceTimeOffset = randState % 10000; // Random time offset
    
    // Generate unique amp contour per voice (subtle variations)
    for (int i = 0; i < 4; i++) {
        randState = (uint16_t)((randState * 31337u) + 12345u);
        voiceAmpContour[i] = 0.8f + (float)(randState % 400) / 1000.0f; // 0.8 to 1.2 range
    }
    
    // Generate grain selection biases
    for (int i = 0; i < 3; i++) {
        randState = (uint16_t)((randState * 31337u) + 12345u);
        voiceGrainBias[i] = -0.1f + (float)(randState % 200) / 1000.0f; // -0.1 to +0.1
    }
    
    // Initialize pitch drift parameters
    randState = (uint16_t)((randState * 31337u) + 12345u);
    voicePitchDrift = 0.001f + (float)(randState % 50) / 100000.0f; // Very subtle drift
    pitchDriftCounter = randState % 1000; // Random starting phase
    currentPitchDrift = 0.0f;
    
    // Timbral character variation
    randState = (uint16_t)((randState * 31337u) + 12345u);
    voiceTimbreShift = -0.05f + (float)(randState % 100) / 1000.0f; // -0.05 to +0.05
    
    grainSpawnJitter = randState % 100; // Variable spawn timing
    
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) {
        grains[i].active = false;
        grains[i].sampleData = nullptr;
        grains[i].sampleLength = 0;
    }
}

ChromaticGranularVoice::~ChromaticGranularVoice() {}

FLASHMEM void ChromaticGranularVoice::init() {
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) {
        grains[i].active = false;
    }
    noteOn = false;
    envelopeActive = false;
    envelopeLevel = 0.0f;
    envelopeTimeCounter = voiceTimeOffset; // Start with offset
    releaseStartTime = 0;
    inRelease = false;
    movingPosition = 0.0f;
    pitchDriftCounter = voiceTimeOffset;
    currentPitchDrift = 0.0f;
}

FLASHMEM float ChromaticGranularVoice::calculateOrganicEnvelope(float baseLevel, float timeRatio, bool isRelease) {
    if (!isRelease) {
        // Attack phase with organic curve
        if (timeRatio < 0.1f) {
            float attackCurve = voiceAmpContour[0]; // Per-voice attack curve
            float normalizedTime = timeRatio / 0.1f;
            return baseLevel * powf(normalizedTime, 1.0f / attackCurve);
        }
        return baseLevel; // Sustain
    } else {
        // Organic release with per-voice curve variation
        float releaseCurve = voiceAmpContour[3];
        float organicRelease = powf(1.0f - timeRatio, releaseCurve);
        
        // Add subtle LFO modulation during release
        float lfoMod = generateVoiceLFO(envelopeTimeCounter, 0.1f, 0.05f);
        return baseLevel * organicRelease * (1.0f + lfoMod);
    }
}

FLASHMEM float ChromaticGranularVoice::generateVoiceLFO(uint32_t counter, float rate, float depth) {
    // Simple sine-based LFO with per-voice offset
    float phase = ((float)(counter + voiceTimeOffset) * rate * 2.0f * M_PI) / AUDIO_SAMPLE_RATE_EXACT;
    return sinf(phase) * depth;
}

FLASHMEM void ChromaticGranularVoice::setParams(const granular_params_t& params) {
    currentParams = params;
}

FLASHMEM void ChromaticGranularVoice::setSampleData(const int16_t* data, uint32_t sampleFrames) {
    sampleData = data;
    sampleLength = sampleFrames;
}

FLASHMEM void ChromaticGranularVoice::setSampleDataBytes(const int16_t* data, uint32_t bytes, uint8_t numChannels) {
    if (!data) {
        sampleData = nullptr;
        sampleLength = 0;
        return;
    }
    uint32_t frames = bytes / (sizeof(int16_t) * numChannels);
    setSampleData(data, frames);
}

FLASHMEM void ChromaticGranularVoice::triggerNote(uint8_t note, uint8_t velocity) {
    if (!sampleData || sampleLength < 2) return;

    float semitones_shift = (float)note - 60.0f - 12.0f;
    perNotePitchShift = powf(2.0f, semitones_shift / 12.0f);

    noteOn = true;
    currentNote = note;
    currentVelocity = velocity;
    envelopeActive = true;
    envelopeLevel = 1.0f;
    envelopeTimeCounter = voiceTimeOffset; // Start with offset
    releaseStartTime = 0;
    inRelease = false;
    
    // Use voice-specific random generator instead of global rand()
    randState = (uint16_t)((randState * 31337u) + 12345u);
    movingPosition = (float)(randState & 0xFFFF) / 65536.0f;
    
    startGrain(currentParams);
}

FLASHMEM void ChromaticGranularVoice::noteOff() {
    if (!noteOn) return;
    noteOn = false;
    if (!envelopeActive) envelopeActive = true;
    releaseStartTime = envelopeTimeCounter;
    inRelease = true;
}

FLASHMEM bool ChromaticGranularVoice::isPlaying() const {
    if (noteOn) return true;
    if (envelopeActive) return true;
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) {
        if (grains[i].active) return true;
    }
    return false;
}

FLASHMEM bool ChromaticGranularVoice::isActiveFor(uint8_t note) const {
    return (noteOn || envelopeActive) && (currentNote == note);
}

FLASHMEM int ChromaticGranularVoice::getActiveGrainCount() const {
    int c = 0;
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) if (grains[i].active) c++;
    return c;
}

FLASHMEM void ChromaticGranularVoice::stopAllGrains() {
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) grains[i].active = false;
    noteOn = false;
    envelopeActive = false;
    envelopeLevel = 0.0f;
    envelopeTimeCounter = voiceTimeOffset; // Reset to offset
    releaseStartTime = 0;
    inRelease = false;
    pitchDriftCounter = voiceTimeOffset;
    currentPitchDrift = 0.0f;
}

FLASHMEM void ChromaticGranularVoice::startGrain(const granular_params_t& params) {
    if (!sampleData || sampleLength < 2) return;

    int idx = -1;
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) {
        if (!grains[i].active) { idx = i; break; }
    }
    if (idx < 0) return;

    GranularGrain &g = grains[idx];
    g.sampleData = sampleData;
    g.sampleLength = sampleLength;

    // Apply voice-specific grain size bias
    float sizeVariation = 1.0f + voiceGrainBias[1] + (sinf((float)envelopeTimeCounter * 0.01f) * 0.1f);
    float grainSizeMs = (10.0f + (params.grain_size / 100.0f) * 990.0f) * sizeVariation;
    g.size = (uint32_t)((grainSizeMs / 1000.0f) * AUDIO_SAMPLE_RATE_EXACT);
    if (g.size < 1) g.size = 1;

    // Enhanced pitch variation with drift and voice character
    float baseSemitones = (params.semitones / 100.0f) * 48.0f - 24.0f;
    float pitchVariation = baseSemitones + voiceTimbreShift + currentPitchDrift;
    
    // Add per-grain random pitch variation
    randState = (uint16_t)((randState * 31337u) + 12345u);
    float grainPitchJitter = ((float)(randState % 200) - 100.0f) / 10000.0f; // ±0.01 semitones
    
    g.pitchRatio = powf(2.0f, (pitchVariation + grainPitchJitter) / 12.0f) * perNotePitchShift;

    // Organic position selection with voice bias
    uint32_t maxStartPos = (g.sampleLength > 2) ? (g.sampleLength - (uint32_t)(g.size * g.pitchRatio) - 2) : 1;
    if (maxStartPos < 1) maxStartPos = 1;

    // Apply spread with voice-specific bias
    float spreadPercent = (params.spread / 100.0f) * (1.0f + voiceGrainBias[0]);
    int32_t maxOffset = (int32_t)(spreadPercent * maxStartPos);

    // More complex offset calculation
    int32_t offset = 0;
    if (maxOffset > 0) {
        randState = (uint16_t)((randState * 31337u) + 12345u);
        float offsetBias = sinf((float)envelopeTimeCounter * 0.005f) * 0.3f; // Slow position drift
        offset = (int32_t)((randState % (2 * (uint32_t)maxOffset + 1)) - (uint32_t)maxOffset);
        offset += (int32_t)(offsetBias * maxOffset);
    }

    // Enhanced moving position with organic movement
    randState = (uint16_t)((randState * 31337u) + 12345u);
    float moveRate = 0.005f + (float)(randState % 1000) / 50000.0f; // Slower, more varied
    float moveLFO = generateVoiceLFO(envelopeTimeCounter, 0.02f, 0.1f);
    movingPosition += moveRate * (1.0f + moveLFO);
    if (movingPosition > 1.0f) movingPosition -= 1.0f;

    // Blend position with organic weighting
    float positionWeight = 0.6f + 0.4f * sinf((float)envelopeTimeCounter * 0.003f);
    float effectivePosRatio = positionWeight * (params.grain_position / 100.0f) + 
                              (1.0f - positionWeight) * movingPosition;
    effectivePosRatio += voiceGrainBias[0]; // Add voice bias
    effectivePosRatio = fmaxf(0.0f, fminf(1.0f, effectivePosRatio)); // Clamp

    int32_t chosenPos = (int32_t)(effectivePosRatio * maxStartPos) + offset;
    if (chosenPos < 0) chosenPos = 0;
    if ((uint32_t)chosenPos > maxStartPos) chosenPos = maxStartPos;

    g.position = (float)chosenPos;

    // Organic volume with voice character and subtle modulation
    const float perGrainHeadroom = 0.8f;
    float velGain = (currentVelocity > 0) ? ((float)currentVelocity / 127.0f) : 1.0f;
    
    // Enhanced volume variation with voice character
    randState = (uint16_t)((randState * 31337u) + 12345u);
    float volumeVariation = 0.85f + (float)(randState % 300) / 1000.0f; // 0.85 to 1.15
    float voiceVolumeChar = 1.0f + voiceAmpContour[2] * 0.1f; // Sustain level affects grain volume
    float dynamicMod = 1.0f + generateVoiceLFO(envelopeTimeCounter, 0.15f, 0.08f);
    
    g.volume = (params.volume / 100.0f) * velGain * perGrainHeadroom * 
               volumeVariation * voiceVolumeChar * dynamicMod;
    
    g.age = 0;
    g.active = true;
}

FLASHMEM void ChromaticGranularVoice::mixInto(float* leftFloat, float* rightFloat, uint16_t blockSize) {
    if (!sampleData || sampleLength < 2) return;

    // Apply density bias per voice
    float densityMod = 1.0f + voiceGrainBias[2] + generateVoiceLFO(envelopeTimeCounter, 0.05f, 0.2f);
    float grainsPerSecond = (1.0f + (currentParams.density / 100.0f) * 99.0f) * densityMod;
    uint32_t grainInterval = (grainsPerSecond > 0.0f) ? (uint32_t)(AUDIO_SAMPLE_RATE_EXACT / grainsPerSecond) : 1;

    for (int i = 0; i < blockSize; i++) {
        envelopeTimeCounter++;
        pitchDriftCounter++;
        
        // Update pitch drift slowly (every ~0.1 seconds)
        if ((pitchDriftCounter % (uint32_t)(AUDIO_SAMPLE_RATE_EXACT * 0.1f)) == 0) {
            float driftLFO = generateVoiceLFO(pitchDriftCounter, voicePitchDrift, 0.02f);
            currentPitchDrift = driftLFO;
        }
        
        // Organic grain spawning with jitter
        grainSpawnJitter++;
        uint32_t jitteredInterval = grainInterval + (grainSpawnJitter % 20) - 10; // ±10 samples jitter
        
        if (noteOn && envelopeLevel > 0.0f) {
            if (jitteredInterval > 0 && (envelopeTimeCounter % jitteredInterval) == 0) {
                // Sometimes skip grain spawning for more organic feel
                randState = (uint16_t)((randState * 31337u) + 12345u);
                if ((randState % 100) > 5) { // 95% spawn probability
                    startGrain(currentParams);
                }
            }
        }

        // Organic envelope calculation
        float baseEnvelope;
        if (noteOn) {
            baseEnvelope = 1.0f;
        } else if (envelopeActive) {
            float releaseTimeSeconds = currentParams.release_seconds > 0.0f ? currentParams.release_seconds : 1.0f;
            float elapsed = (float)(envelopeTimeCounter - releaseStartTime) / AUDIO_SAMPLE_RATE_EXACT;
            if (elapsed >= releaseTimeSeconds) {
                envelopeLevel = 0.0f;
                envelopeActive = false;
                inRelease = false;
                stopAllGrains();
                continue;
            } else {
                float timeRatio = elapsed / releaseTimeSeconds;
                baseEnvelope = calculateOrganicEnvelope(1.0f, timeRatio, true);
            }
        } else {
            baseEnvelope = 0.0f;
        }

        envelopeLevel = baseEnvelope;
        float sampleOut = 0.0f;

        // Process grains with organic envelope curves
        for (int gidx = 0; gidx < MAX_GRAINS_PER_VOICE; gidx++) {
            GranularGrain &g = grains[gidx];
            if (!g.active || !g.sampleData || g.sampleLength < 2) continue;

            if (g.age >= g.size || (uint32_t)(g.position + g.pitchRatio) >= g.sampleLength - 1) {
                g.active = false;
                continue;
            }

            float ageRatio = (float)g.age / (float)g.size;
            
            // Organic grain envelope with voice-specific curve
            float grainEnv = 1.0f;
            float attackCurve = voiceAmpContour[0];
            float releaseCurve = voiceAmpContour[3];
            
            if (ageRatio < 0.15f) {
                // Variable attack curve per voice
                grainEnv = powf(ageRatio / 0.15f, 1.0f / attackCurve);
            } else if (ageRatio > 0.85f) {
                // Variable release curve per voice  
                grainEnv = powf((1.0f - ageRatio) / 0.15f, releaseCurve);
            }

            // Sample interpolation (unchanged)
            uint32_t intPos = (uint32_t)g.position;
            int16_t s1 = g.sampleData[intPos];
            int16_t s2 = g.sampleData[intPos + 1];
            float frac = g.position - intPos;
            float s = ((float)s1 + frac * ((float)s2 - (float)s1)) / 32768.0f;

            sampleOut += s * grainEnv * g.volume;
            g.position += g.pitchRatio;
            g.age++;
        }

        sampleOut *= envelopeLevel;
        leftFloat[i] += sampleOut;
        rightFloat[i] += sampleOut;
    }
}

// ---------------- AudioChromaticGranularPoly ----------------

FLASHMEM AudioChromaticGranularPoly::AudioChromaticGranularPoly() : AudioStream(0, NULL) {
    sharedParams.grain_size = 50;
    sharedParams.grain_position = 50;
    sharedParams.semitones = 50;
    sharedParams.volume = 100;
    sharedParams.density = 50;
    sharedParams.spread = 50;
    sharedParams.release_seconds = 2.0f;
    sharedParams.active = true;

    polyRandSeed = micros();

    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        voices[i] = new ChromaticGranularVoice();
        voices[i]->init();
        voices[i]->setParams(sharedParams);
    }
}

FLASHMEM AudioChromaticGranularPoly::~AudioChromaticGranularPoly() {
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        if (voices[i]) {
            delete voices[i];
            voices[i] = nullptr;
        }
    }
}

FLASHMEM void AudioChromaticGranularPoly::begin() {
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        if (voices[i]) voices[i]->init();
    }
}

FLASHMEM void AudioChromaticGranularPoly::setParams(const granular_params_t& params) {
    sharedParams = params;
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        voices[i]->setParams(sharedParams);
    }
}

FLASHMEM void AudioChromaticGranularPoly::setSampleData(const int16_t* data, uint32_t sampleFrames) {
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        voices[i]->setSampleData(data, sampleFrames);
    }
}

FLASHMEM void AudioChromaticGranularPoly::setSampleDataBytes(const int16_t* data, uint32_t bytes, uint8_t numChannels) {
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        voices[i]->setSampleDataBytes(data, bytes, numChannels);
    }
}

FLASHMEM void AudioChromaticGranularPoly::triggerNote(uint8_t note, uint8_t velocity) {
    ChromaticGranularVoice* target = nullptr;
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        if (!voices[i]->isPlaying()) { target = voices[i]; break; }
    }
    if (!target) {
        int sel = 0;
        int minGrains = voices[0]->getActiveGrainCount();
        for (int i = 1; i < MAX_POLY_VOICES; i++) {
            int g = voices[i]->getActiveGrainCount();
            if (g < minGrains) { minGrains = g; sel = i; }
        }
        target = voices[sel];
        target->stopAllGrains();
    }
    if (target) {
        target->setParams(sharedParams);
        target->triggerNote(note, velocity);
    }
}

FLASHMEM void AudioChromaticGranularPoly::noteOff(uint8_t note) {
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        if (voices[i]->isActiveFor(note)) voices[i]->noteOff();
    }
}

FLASHMEM void AudioChromaticGranularPoly::stopAllVoices() {
    for (int i = 0; i < MAX_POLY_VOICES; i++) voices[i]->stopAllGrains();
}

FLASHMEM int AudioChromaticGranularPoly::getActiveVoiceCount() const {
    int c = 0;
    for (int i = 0; i < MAX_POLY_VOICES; i++) if (voices[i]->isPlaying()) c++;
    return c;
}

FLASHMEM int AudioChromaticGranularPoly::getActiveGrainCount() const {
    int s = 0;
    for (int i = 0; i < MAX_POLY_VOICES; i++) s += voices[i]->getActiveGrainCount();
    return s;
}

FLASHMEM void AudioChromaticGranularPoly::update(void) {
    audio_block_t *leftBlock = allocate();
    if (!leftBlock) return;
    audio_block_t *rightBlock = allocate();
    if (!rightBlock) { release(leftBlock); return; }

    static float mixL[AUDIO_BLOCK_SAMPLES];
    static float mixR[AUDIO_BLOCK_SAMPLES];

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        mixL[i] = 0.0f;
        mixR[i] = 0.0f;
    }

    for (int v = 0; v < MAX_POLY_VOICES; v++) {
        voices[v]->mixInto(mixL, mixR, AUDIO_BLOCK_SAMPLES);
    }

    int activeVoices = getActiveVoiceCount();
    float baseMasterGain = 0.3f;
    float dynamicScale = 1.0f / (1.0f + 0.25f * (float)activeVoices);
    float masterGain = baseMasterGain * dynamicScale;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        float lf = mixL[i] * masterGain;
        float rf = mixR[i] * masterGain;
        if (lf > 1.0f) lf = 1.0f; else if (lf < -1.0f) lf = -1.0f;
        if (rf > 1.0f) rf = 1.0f; else if (rf < -1.0f) rf = -1.0f;
        leftBlock->data[i] = (int16_t)(lf * 32767.0f);
        rightBlock->data[i] = (int16_t)(rf * 32767.0f);
    }

    transmit(leftBlock, 0);
    transmit(rightBlock, 1);
    release(leftBlock);
    release(rightBlock);
}

#endif // GRANULAR