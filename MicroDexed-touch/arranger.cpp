// arranger.cpp - Fixed with proper includes

#include "arranger.h"
#include "config.h"
#include "sequencer.h"  // <-- Add this line - defines sequencer_t and seq

// seq is defined in sequencer.cpp as a global variable
extern sequencer_t seq;

ChordInfo currentChord = { 0 };

// Simple sort function (bubble sort)
FLASHMEM void sortArray(uint8_t arr[], uint8_t size) {
    for (uint8_t i = 0; i < size - 1; ++i) {
        for (uint8_t j = i + 1; j < size; ++j) {
            if (arr[i] > arr[j]) {
                uint8_t temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
    }
}

FLASHMEM const char* midiToNote(uint8_t midiNote) {
    static const char* notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return notes[midiNote % 12];
}

FLASHMEM bool containsInterval(const uint8_t intervals[], uint8_t size, uint8_t interval) {
    for (uint8_t i = 0; i < size; i++) {
        if (intervals[i] == interval) return true;
    }
    return false;
}

FLASHMEM void getIntervals(const uint8_t notes[], uint8_t size, uint8_t root, uint8_t intervals[]) {
    for (uint8_t i = 0; i < size; ++i) {
        intervals[i] = (notes[i] - root + 12) % 12;
    }
}

FLASHMEM const char* detectChordType(const uint8_t intervals[], uint8_t size) {
    if (size < 3) return NULL;

    bool hasMinorThird = containsInterval(intervals, size, 3);
    bool hasMajorThird = containsInterval(intervals, size, 4);
    bool hasDimFifth = containsInterval(intervals, size, 6);
    bool hasPerfFifth = containsInterval(intervals, size, 7);
    bool hasAugFifth = containsInterval(intervals, size, 8);
    bool hasMinorSeventh = containsInterval(intervals, size, 10);
    bool hasMajorSixth = containsInterval(intervals, size, 9);
    bool hasMajorSeventh = containsInterval(intervals, size, 11);

    if (size >= 4) {
        if (hasMajorThird && hasPerfFifth && hasMinorSeventh) return "7th";
        if (hasMajorThird && hasPerfFifth && hasMajorSeventh) return "Maj7";
        if (hasMinorThird && hasPerfFifth && hasMinorSeventh) return "m7";
        if (hasMinorThird && hasPerfFifth && hasMajorSeventh) return "mMaj7";
        if (hasMinorThird && hasDimFifth && containsInterval(intervals, size, 9)) return "dim7";
        if (hasMinorThird && hasDimFifth && hasMinorSeventh) return "m7b5";
        if (hasMajorThird && hasAugFifth && hasMinorSeventh) return "aug7";
        if (hasMajorThird && hasPerfFifth && hasMajorSixth) return "6";
        if (hasMinorThird && hasPerfFifth && hasMajorSixth) return "m6";
    }

    if (hasMajorThird && hasPerfFifth) return "Major";
    if (hasMinorThird && hasPerfFifth) return "Minor";
    if (hasMinorThird && hasDimFifth) return "Diminished";
    if (hasMajorThird && hasAugFifth) return "Augmented";

    return NULL;
}

FLASHMEM void updateCurrentChord(const uint8_t midiNotes[], uint8_t size) {
    if (size < 3) {
        currentChord.active = false;
        return;
    }

    // Convert to pitch-class only (ignore octave)
    uint8_t pitchClasses[MAX_CHORD_NOTES];
    for (uint8_t i = 0; i < size; i++) {
        pitchClasses[i] = midiNotes[i] % 12;
    }

    // Sort pitch classes
    uint8_t sortedNotes[MAX_CHORD_NOTES];
    memcpy(sortedNotes, pitchClasses, size * sizeof(uint8_t));
    sortArray(sortedNotes, size);

    uint8_t intervals[MAX_CHORD_NOTES];
    const char* chordType = NULL;
    uint8_t root = 0;
    uint8_t inversion = 0;
    bool hasSeventh = false;

    // Try each note as potential root
    for (uint8_t i = 0; i < size; i++) {
        root = sortedNotes[i];
        getIntervals(sortedNotes, size, root, intervals);
        chordType = detectChordType(intervals, size);
        if (chordType != NULL) {
            inversion = i;
            hasSeventh = (strstr(chordType, "7") != NULL) || (size >= 4);
            break;
        }
    }

    if (chordType == NULL && size == 3) {
        root = sortedNotes[0];
        chordType = "Major";
        inversion = 0;
        hasSeventh = false;
    }

    if (chordType == NULL) {
        currentChord.active = false;
        return;
    }

    currentChord.active = true;
    currentChord.type = chordType;
    currentChord.root = root;  // Already pitch-class (0-11)
    currentChord.inversion = inversion;
    currentChord.hasSeventh = hasSeventh;

    // Set chord tones (all as pitch-classes 0-11)
    currentChord.third = 0;
    currentChord.fifth = 0;
    currentChord.seventh = 0;

    if (strstr(chordType, "Major") || strstr(chordType, "6")) {
        currentChord.third = (root + 4) % 12;
        currentChord.fifth = (root + 7) % 12;
        if (strstr(chordType, "Maj7")) {
            currentChord.seventh = (root + 11) % 12;
            currentChord.hasSeventh = true;
        }
    }
    else if (strstr(chordType, "Minor") || strstr(chordType, "m")) {
        currentChord.third = (root + 3) % 12;
        currentChord.fifth = (root + 7) % 12;
        if (strstr(chordType, "mMaj7")) {
            currentChord.seventh = (root + 11) % 12;
            currentChord.hasSeventh = true;
        }
        else if (strstr(chordType, "m7")) {
            currentChord.seventh = (root + 10) % 12;
            currentChord.hasSeventh = true;
        }
    }
    else if (strstr(chordType, "Diminished")) {
        currentChord.third = (root + 3) % 12;
        currentChord.fifth = (root + 6) % 12;
        if (strstr(chordType, "dim7")) {
            currentChord.seventh = (root + 9) % 12;
            currentChord.hasSeventh = true;
        }
    }
    else if (strstr(chordType, "Augmented")) {
        currentChord.third = (root + 4) % 12;
        currentChord.fifth = (root + 8) % 12;
        if (strstr(chordType, "aug7")) {
            currentChord.seventh = (root + 10) % 12;
            currentChord.hasSeventh = true;
        }
    }
    else if (strstr(chordType, "7")) {
        currentChord.third = (root + 4) % 12;
        currentChord.fifth = (root + 7) % 12;
        currentChord.seventh = (root + 10) % 12;
        currentChord.hasSeventh = true;
    }
}

// Separate voice indices for each mode
static uint8_t chordVoiceIndex = 0;
static uint8_t melodyVoiceIndex = 0;
static uint8_t bassVoiceIndex = 0;
static uint8_t rootVoiceIndex = 0;

FLASHMEM void resetChordTransformation() {
    chordVoiceIndex = 0;
    melodyVoiceIndex = 0;
    bassVoiceIndex = 0;
    rootVoiceIndex = 0;
}

// ============================================================================
// Helper function: Get the best octave for a pitch class relative to input note
// ============================================================================
static FLASHMEM int16_t getNoteInOctave(uint8_t pitchClass, uint8_t inputNote) {
    // pitchClass: 0-11
    // inputNote: MIDI note number (0-127)
    
    uint8_t inputOctave = inputNote / 12;
    int16_t candidate;
    int16_t bestCandidate = pitchClass + inputOctave * 12; // Default to same octave
    int16_t bestDist = abs(bestCandidate - inputNote);
    
    // Try three octave candidates: same octave, one below, one above
    for (int8_t offset = -1; offset <= 1; offset++) {
        int8_t octave = inputOctave + offset;
        if (octave < 0 || octave > 10) continue;
        
        candidate = pitchClass + octave * 12;
        
        // Prefer notes close to input
        int16_t dist = abs(candidate - inputNote);
        if (dist < bestDist) {
            bestDist = dist;
            bestCandidate = candidate;
        }
    }
    
    return bestCandidate;
}

// ============================================================================
// MODE 1: CHORD MODE (seq.arr_type[x] == 1) - Play full chord for each note
// ============================================================================
FLASHMEM uint8_t transformNoteToChord(uint8_t inputNote) {
    if (!currentChord.active) return inputNote;

    uint8_t chordSize = currentChord.hasSeventh ? 4 : 3;
    uint8_t chordTones[4] = {
        (uint8_t)(currentChord.root % 12),
        (uint8_t)(currentChord.third % 12),
        (uint8_t)(currentChord.fifth % 12),
        (uint8_t)(currentChord.seventh % 12)
    };

    // Cycle through chord tones
    uint8_t toneIndex = chordVoiceIndex % chordSize;
    chordVoiceIndex = (chordVoiceIndex + 1) % chordSize;

    // Use input note's octave for the output
    int16_t outputNote = getNoteInOctave(chordTones[toneIndex], inputNote);
    
    // Ensure note is in reasonable range (C2 to C7)
    while (outputNote < 36) outputNote += 12;
    while (outputNote > 91) outputNote -= 12;

    return (uint8_t)outputNote;
}

// ============================================================================
// MODE 2: MELODY MODE (seq.arr_type[x] == 2) - Map to closest chord tone
// ============================================================================
FLASHMEM uint8_t transformNoteToChordMelody(uint8_t inputNote) {
    if (!currentChord.active) return inputNote;

    uint8_t chordTones[4] = {
        (uint8_t)(currentChord.root % 12),
        (uint8_t)(currentChord.third % 12),
        (uint8_t)(currentChord.fifth % 12),
        (uint8_t)(currentChord.seventh % 12)
    };
    uint8_t chordSize = currentChord.hasSeventh ? 4 : 3;

    uint8_t inputPC = inputNote % 12;

    // Find the closest chord tone to the input note's pitch class
    // (We just need to find the closest one, but we use toneIndex for variety)
    uint8_t minDistance = 12;
    // uint8_t bestToneIndex = 0;  // REMOVED - was unused

    for (uint8_t i = 0; i < chordSize; i++) {
        uint8_t distance = abs((int8_t)chordTones[i] - (int8_t)inputPC);
        if (distance > 6) distance = 12 - distance;
        if (distance < minDistance) {
            minDistance = distance;
            // bestToneIndex = i;  // REMOVED - was unused
        }
    }

    // Use voice index for variety (cycle through chord tones)
    uint8_t toneIndex = melodyVoiceIndex % chordSize;
    melodyVoiceIndex = (melodyVoiceIndex + 1) % chordSize;

    // Use input note's octave
    int16_t outputNote = getNoteInOctave(chordTones[toneIndex], inputNote);

    // Ensure output is close to input (within 3 semitones if possible)
    int16_t bestCandidate = outputNote;
    int16_t bestDist = abs(outputNote - inputNote);

    // Try octave up
    int16_t candidateUp = outputNote + 12;
    if (abs(candidateUp - inputNote) < bestDist && candidateUp <= 91) {
        bestDist = abs(candidateUp - inputNote);
        bestCandidate = candidateUp;
    }

    // Try octave down
    int16_t candidateDown = outputNote - 12;
    if (abs(candidateDown - inputNote) < bestDist && candidateDown >= 36) {
        bestCandidate = candidateDown;
    }

    outputNote = bestCandidate;

    // Final range safety
    if (outputNote < 36) outputNote += 12;
    if (outputNote > 91) outputNote -= 12;
    if (outputNote < 36) outputNote = 36;
    if (outputNote > 91) outputNote = 91;

    return (uint8_t)outputNote;
}

// ============================================================================
// MODE 3: BASS MODE (seq.arr_type[x] == 3) - Play bass (root/fifth pattern)
// ============================================================================
FLASHMEM uint8_t transformBassNote(uint8_t inputNote) {
    if (!currentChord.active) return inputNote;

    uint8_t rootPitchClass = currentChord.root % 12;
    uint8_t fifthPitchClass = currentChord.fifth % 12;
    uint8_t inputOctave = inputNote / 12;

    // Alternate between root and fifth
    uint8_t toneIndex = bassVoiceIndex % 2;
    bassVoiceIndex = (bassVoiceIndex + 1) % 2;

    uint8_t destPitchClass = (toneIndex == 0) ? rootPitchClass : fifthPitchClass;

    // Bass should be in lower register (C2 to C4)
    // Use input octave as guide but keep it low
    int16_t outputNote = destPitchClass + (inputOctave > 4 ? 3 : inputOctave) * 12;

    // Force bass to C2-C4 range
    while (outputNote > 60) outputNote -= 12;  // Don't go above C4
    while (outputNote < 36) outputNote += 12;  // Don't go below C2

    // Always start from root when chord changes
    static uint8_t lastRoot = 0;
    if (lastRoot != currentChord.root) {
        lastRoot = currentChord.root;
        bassVoiceIndex = 0;
        outputNote = rootPitchClass + 3 * 12; // Start at C3 range
    }

    return (uint8_t)outputNote;
}

// ============================================================================
// MODE 4: ROOT MODE (seq.arr_type[x] == 4) - Play only the root note
// ============================================================================
FLASHMEM uint8_t getChordRootNote(uint8_t inputNote) {
    if (!currentChord.active) return inputNote;

    uint8_t rootPitchClass = currentChord.root % 12;

    // Use input note's octave for the output
    int16_t outputNote = getNoteInOctave(rootPitchClass, inputNote);

    // Keep root in reasonable range (C2 to C6)
    while (outputNote < 36) outputNote += 12;
    while (outputNote > 79) outputNote -= 12;

    return (uint8_t)outputNote;
}

// ============================================================================
// TRANSFORMATION ENTRY POINT
// ============================================================================
FLASHMEM uint8_t transformNoteForArranger(uint8_t track, uint8_t inputNote) {
    if (!currentChord.active) return inputNote;
    
    // Make sure track is valid
    if (track >= NUM_SEQ_TRACKS) return inputNote;
    
    switch (seq.arr_type[track]) {
        case 0:  // OFF - no transformation
            return inputNote;
        case 1:  // ON - chord mode
            return transformNoteToChord(inputNote);
        case 2:  // MELO - melody mode
            return transformNoteToChordMelody(inputNote);
        case 3:  // BASS - bass mode
            return transformBassNote(inputNote);
        case 4:  // ROOT - root mode
            return getChordRootNote(inputNote);
        default:
            return inputNote;
    }
}

// ============================================================================
// CHORD DETECTION AND SUGGESTION FUNCTIONS
// ============================================================================

FLASHMEM bool getChordName(const uint8_t midiNotes[], uint8_t size, char* output) {
    if (size < 3) return false;

    // Convert to pitch-class only
    uint8_t pitchClasses[MAX_CHORD_NOTES];
    for (uint8_t i = 0; i < size; i++) {
        pitchClasses[i] = midiNotes[i] % 12;
    }

    uint8_t sortedNotes[MAX_CHORD_NOTES];
    memcpy(sortedNotes, pitchClasses, size * sizeof(uint8_t));
    sortArray(sortedNotes, size);

    uint8_t intervals[MAX_CHORD_NOTES];
    const char* chordType = NULL;
    uint8_t root = 0;

    for (uint8_t i = 0; i < size; i++) {
        root = sortedNotes[i];
        getIntervals(sortedNotes, size, root, intervals);
        chordType = detectChordType(intervals, size);
        if (chordType != NULL) break;
    }

    if (chordType == NULL) return false;

    const char* inversion = "Root Pos.";
    uint8_t rootPitchClass = root % 12;
    uint8_t lowestPitchClass = sortedNotes[0] % 12;

    if (lowestPitchClass != rootPitchClass) {
        uint8_t third = (strchr(chordType, 'm') != NULL) ? 3 : 4;
        uint8_t thirdPitchClass = (rootPitchClass + third) % 12;

        if (lowestPitchClass == thirdPitchClass) {
            inversion = "1st Inv.";
        }
        else {
            uint8_t fifth = 7;
            if (strcmp(chordType, "Diminished") == 0) fifth = 6;
            else if (strcmp(chordType, "Augmented") == 0) fifth = 8;

            uint8_t fifthPitchClass = (rootPitchClass + fifth) % 12;
            if (lowestPitchClass == fifthPitchClass) {
                inversion = "2nd Inv.";
            }
            else if (size >= 4) {
                uint8_t seventh = 10;
                if (strcmp(chordType, "Maj7") == 0 || strcmp(chordType, "mMaj7") == 0) seventh = 11;
                else if (strcmp(chordType, "dim7") == 0) seventh = 9;
                else if (strcmp(chordType, "6") == 0 || strcmp(chordType, "m6") == 0) seventh = 9;

                uint8_t seventhPitchClass = (rootPitchClass + seventh) % 12;
                if (lowestPitchClass == seventhPitchClass) {
                    inversion = "3rd Inv.";
                }
            }
        }
    }
    snprintf(output, 30, "%s %s (%s)", midiToNote(root), chordType, inversion);
    return true;
}

FLASHMEM void suggestNextChords(ChordInfo current, SuggestedChord suggestions[6]) {
    if (!current.active) {
        memset(suggestions, 0, 6 * sizeof(SuggestedChord));
        return;
    }

    uint8_t root = current.root;
    bool isMajor = (strstr(current.type, "Major") != NULL);
    bool isDominant = (strstr(current.type, "7th") != NULL);

    memset(suggestions, 0, 6 * sizeof(SuggestedChord));
    uint8_t valid_count = 0;

#define ADD_TRIAD_SUGGESTION(base, name_fmt, third_offset) \
        if (valid_count < 6) { \
            suggestions[valid_count].notes[0] = (base) % 12 + 60; \
            suggestions[valid_count].notes[1] = (base + (third_offset)) % 12 + 60; \
            suggestions[valid_count].notes[2] = (base + 7) % 12 + 60; \
            snprintf(suggestions[valid_count].name, 20, (name_fmt), midiToNote((base) % 12)); \
            valid_count++; \
        }

    uint8_t dominant = (root + 7) % 12;
    ADD_TRIAD_SUGGESTION(dominant, "%s", isMajor ? 4 : 3);

    uint8_t subdominant = (root + 5) % 12;
    ADD_TRIAD_SUGGESTION(subdominant, isMajor ? "%s" : "%sm", isMajor ? 4 : 3);

    uint8_t relative = isMajor ? (root + 9) % 12 : (root + 3) % 12;
    ADD_TRIAD_SUGGESTION(relative, isMajor ? "%sm" : "%s", isMajor ? 3 : 4);

    ADD_TRIAD_SUGGESTION(root, isMajor ? "%sm" : "%s", isMajor ? 3 : 4);

    uint8_t step = (root + 2) % 12;
    ADD_TRIAD_SUGGESTION(step, isMajor ? "%s" : "%sm", isMajor ? 4 : 3);

    uint8_t chrom = (root + (rand() % 2 ? 1 : 11)) % 12;
    ADD_TRIAD_SUGGESTION(chrom, "%s", 4);

    if (isDominant && valid_count > 0) {
        uint8_t tritoneSub = (root + 6) % 12;
        suggestions[valid_count - 1].notes[0] = tritoneSub + 60;
        suggestions[valid_count - 1].notes[1] = (tritoneSub + 4) % 12 + 60;
        suggestions[valid_count - 1].notes[2] = (tritoneSub + 7) % 12 + 60;
        snprintf(suggestions[valid_count - 1].name, 20, "%s7", midiToNote(tritoneSub));
    }

    for (int i = 0; i < 6; i++) {
        if (suggestions[i].notes[0] == 0) {
            suggestions[i].notes[0] = root + 60;
            suggestions[i].notes[1] = (root + (isMajor ? 4 : 3)) % 12 + 60;
            suggestions[i].notes[2] = (root + 7) % 12 + 60;
            snprintf(suggestions[i].name, 20, isMajor ? "%s" : "%sm", midiToNote(root));
        }
    }
}