#ifdef GRANULAR

#include "granular.h"
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

// Global variable definitions
granular_params_t granular_params;
bool granular_initialized = false;
AudioChromaticGranular granular1;

// ChromaticGranularProcessor Implementation
FLASHMEM void ChromaticGranularProcessor::init() {
    for (int i = 0; i < MAX_GRAINS; i++) {
        grains[i].active = false;
        grains[i].sampleData = nullptr;
        grains[i].sampleLength = 0;
    }

    timeCounter = 0;
    grainInterval = 44100;
    noteOn = false;
    globalSampleData = nullptr;
    globalSampleLength = 0;

    envelopeLevel = 0.0f;
    envelopeActive = false;
    envelopeTimeCounter = 0;
    releaseStartTime = 0;
}

void AudioChromaticGranular::init() {
    if (processor) {
        processor->init();
    }
}

FLASHMEM void ChromaticGranularProcessor::setParams(const granular_params_t& params) {
    currentParams = params;
    float grainsPerSecond = 1.0f + (params.density / 100.0f) * 99.0f;
    if (grainsPerSecond > 0) {
        grainInterval = (uint32_t)(AUDIO_SAMPLE_RATE_EXACT / grainsPerSecond);
    }
}

FLASHMEM void ChromaticGranularProcessor::triggerNote(uint8_t note, uint8_t velocity) {
    if (!globalSampleData || globalSampleLength == 0) return;

    noteOn = true;
    currentNote = note;
    currentVelocity = velocity;
    timeCounter = grainInterval;

    envelopeLevel = 1.0f;
    envelopeActive = true;
    envelopeTimeCounter = 0;
    releaseStartTime = 0;
}

bool inRelease = false;

FLASHMEM void ChromaticGranularProcessor::noteOff() {
    if (!noteOn) return;
    noteOn = false;
    envelopeActive = true;
    releaseStartTime = envelopeTimeCounter; 
    inRelease = true; // start release mode
}

FLASHMEM void ChromaticGranularProcessor::setSampleData(const int16_t* data, uint32_t length) {
    globalSampleData = data;
    globalSampleLength = length;
}

void ChromaticGranularProcessor::startGrain(const int16_t* sampleData, uint32_t sampleLength, const granular_params_t& params) {
    if (!sampleData || sampleLength == 0) return;

    for (int i = 0; i < MAX_GRAINS; i++) {
        if (!grains[i].active) {
            Grain& g = grains[i];
            g.sampleData = sampleData;
            g.sampleLength = sampleLength;

            // Grain size in samples
            float grainSizeMs = 10.0f + (params.grain_size / 100.0f) * 990.0f;
            g.size = (grainSizeMs / 1000.0f) * AUDIO_SAMPLE_RATE_EXACT;
            if (g.size < 1) g.size = 1;

            // Pitch
            float semitones = (params.semitones / 100.0f) * 48.0f - 24.0f;
            g.pitchRatio = powf(2.0f, semitones / 12.0f);

            // Position
            float positionRatio = params.grain_position / 100.0f;
            uint32_t maxStartPos = sampleLength - (uint32_t)(g.size * g.pitchRatio) - 2;
            if (maxStartPos < 1) maxStartPos = 1;

            float spreadPercent = 0.0f;
#ifdef GRANULAR_SPREAD
            spreadPercent = params.spread / 100.0f;
#endif

            int32_t maxOffset = (int32_t)(spreadPercent * maxStartPos);
            int32_t offset = 0;
            if (maxOffset > 0) offset = (rand() % (2 * maxOffset + 1)) - maxOffset;

            int32_t chosenPos = (int32_t)(positionRatio * maxStartPos) + offset;
            if (chosenPos < 0) chosenPos = 0;
            if ((uint32_t)chosenPos > maxStartPos) chosenPos = maxStartPos;
            g.position = (float)chosenPos;

            g.volume = (params.volume / 100.0f) * (currentVelocity / 127.0f);
            g.age = 0;
            g.active = true;

            break;
        }
    }
}

FLASHMEM bool AudioChromaticGranular::isPlaying() {
    return processor ? processor->isPlaying() : false;
}

bool ChromaticGranularProcessor::isPlaying() {
    if (noteOn) return true;
    for (int i = 0; i < MAX_GRAINS; i++)
        if (grains[i].active) return true;
    return false;
}

FLASHMEM void ChromaticGranularProcessor::update(int16_t* left, int16_t* right, uint16_t blockSize) {
    // Clear output buffers
    for (int i = 0; i < blockSize; i++) {
        left[i] = 0;
        right[i] = 0;
    }

    // Spawn new grains only if note is on
    timeCounter += blockSize;
    if (noteOn && envelopeLevel > 0.0f && globalSampleData && globalSampleLength > 0) {
        while (timeCounter >= grainInterval) {
            startGrain(globalSampleData, globalSampleLength, currentParams);
            timeCounter -= grainInterval;
        }
    }

    // Process each sample in the block
    for (int i = 0; i < blockSize; i++) {
        envelopeTimeCounter++;

        // --- Envelope Handling ---
        if (noteOn) {
            envelopeLevel = 1.0f;
            releaseStartTime = 0;
            inRelease = false;
        } else if (envelopeActive) {
            if (!inRelease) {
                releaseStartTime = envelopeTimeCounter;
                inRelease = true;
            }

            float releaseTimeSeconds = 5.0f; 
            float elapsed = (float)(envelopeTimeCounter - releaseStartTime) / AUDIO_SAMPLE_RATE_EXACT;

            if (elapsed >= releaseTimeSeconds) {
                envelopeLevel = 0.0f;
                envelopeActive = false;
                inRelease = false;
                stopAllGrains(); // stop grains when release ends
            } else {
                envelopeLevel = 1.0f - (elapsed / releaseTimeSeconds);
            }
        }

        float sampleOut = 0.0f;

        // --- Grain Processing ---
        for (int g = 0; g < MAX_GRAINS; g++) {
            Grain &grain = grains[g];
            if (!grain.active || !grain.sampleData || grain.sampleLength < 2) continue;

            // Clamp position within buffer
            if (grain.position < 0.0f) grain.position = 0.0f;
            if (grain.position > (float)(grain.sampleLength - 2)) {
                grain.position = (float)(grain.sampleLength - 2);
                grain.active = false;
                continue;
            }

         bool terminate = false;
if (grain.age >= grain.size) terminate = true;
uint32_t nextPos = (uint32_t)(grain.position + grain.pitchRatio);
if (nextPos >= grain.sampleLength - 1) terminate = true;

// ONLY terminate grains if envelope finished
if (terminate && !inRelease) {
    grain.active = false;
}

            // Grain envelope (fade in/out)
            float ageRatio = (float)grain.age / grain.size;
            float grainEnv = 1.0f;
            if (ageRatio < 0.1f) grainEnv = ageRatio / 0.1f;
            else if (ageRatio > 0.9f) grainEnv = (1.0f - ageRatio) / 0.1f;

            // Safe linear interpolation
            uint32_t intPos = (uint32_t)grain.position;
            int16_t s1 = grain.sampleData[intPos];
            int16_t s2 = grain.sampleData[intPos + 1];
            float frac = grain.position - intPos;
            float s = s1 + frac * (s2 - s1);

            sampleOut += s * grainEnv * grain.volume;

            // Advance grain
            grain.position += grain.pitchRatio;
            grain.age++;
        }

        // Apply global envelope
        sampleOut *= envelopeLevel;

        // Clamp output
        left[i] = constrain((int16_t)sampleOut, -32768, 32767);
        right[i] = left[i];
    }
}



int ChromaticGranularProcessor::getActiveGrainCount() {
    int count = 0;
    for (int i = 0; i < MAX_GRAINS; i++) if (grains[i].active) count++;
    return count;
}

void ChromaticGranularProcessor::stopAllGrains() {
    for (int i = 0; i < MAX_GRAINS; i++) grains[i].active = false;
    noteOn = false;
    envelopeActive = false;
    envelopeLevel = 0.0f;
    envelopeTimeCounter = 0;
    releaseStartTime = 0;
}

// AudioChromaticGranular Implementation
void AudioChromaticGranular::begin() {
    if (!processor) {
        processor = new ChromaticGranularProcessor();
        processor->init();
    }
}

void AudioChromaticGranular::startGrain(const int16_t* data, uint32_t length, const granular_params_t& params) {
    if (processor) processor->startGrain(data, length, params);
}

int AudioChromaticGranular::getActiveGrainCount() {
    return processor ? processor->getActiveGrainCount() : 0;
}

void AudioChromaticGranular::stopAllGrains() {
    if (processor) processor->stopAllGrains();
}

void AudioChromaticGranular::setParams(const granular_params_t& params) {
    if (processor) processor->setParams(params);
}

void AudioChromaticGranular::triggerNote(uint8_t note, uint8_t velocity) {
    if (processor) processor->triggerNote(note, velocity);
}

void AudioChromaticGranular::noteOff() {
    if (processor) processor->noteOff();
}

void AudioChromaticGranular::setSampleData(const int16_t* data, uint32_t length) {
    if (processor) processor->setSampleData(data, length);
}

void AudioChromaticGranular::update(void) {
    if (!processor) return;

    audio_block_t *leftBlock = allocate();
    if (!leftBlock) return;

    audio_block_t *rightBlock = allocate();
    if (!rightBlock) {
        release(leftBlock);
        return;
    }

    processor->update(leftBlock->data, rightBlock->data, AUDIO_BLOCK_SAMPLES);

    transmit(leftBlock, 0);
    transmit(rightBlock, 1);
    release(leftBlock);
    release(rightBlock);
}

// Global functions
void initGranular() {
    granular1.begin();
    granular1.init();
    granular_initialized = true;

    granular_params.grain_size = 50;
    granular_params.grain_position = 0;
    granular_params.semitones = 50;
    granular_params.volume = 100;
    granular_params.density = 50;
    granular_params.active = true;
}

void setGranularActive(bool active) {
    granular_params.active = active;
    if (!active) granular1.stopAllGrains();
}

void updateGranularParams(granular_params_t newParams) {
    granular_params = newParams;
    granular1.setParams(granular_params);
}

#endif
