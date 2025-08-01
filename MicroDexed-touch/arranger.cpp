#include "arranger.h"
#include "config.h"

ChordInfo currentChord = {0};

// Simple sort function (bubble sort)
void sortArray(uint8_t arr[], uint8_t size) {
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
    static const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return notes[midiNote % 12];
}

FLASHMEM bool containsInterval(const uint8_t intervals[], uint8_t size, uint8_t interval) {
    for (uint8_t i = 0; i < size; i++) {
        if (intervals[i] == interval) return true;
    }
    return false;
}

//  Function to calculate intervals from root
FLASHMEM void getIntervals(const uint8_t notes[], uint8_t size, uint8_t root, uint8_t intervals[]) {
    for (uint8_t i = 0; i < size; ++i) {
        intervals[i] = (notes[i] - root + 12) % 12;  // Intervals from the root, modulo 12
    }
}

FLASHMEM const char* detectChordType(const uint8_t intervals[], uint8_t size) {
    if (size != 3) return NULL;  // Return NULL for unknown chords
    
    bool hasMinorThird = containsInterval(intervals, size, 3);
    bool hasMajorThird = containsInterval(intervals, size, 4);
    bool hasDimFifth = containsInterval(intervals, size, 6);
    bool hasPerfFifth = containsInterval(intervals, size, 7);
    bool hasAugFifth = containsInterval(intervals, size, 8);

    if (hasMajorThird && hasPerfFifth) return "Major";
    if (hasMinorThird && hasPerfFifth) return "Minor";
    if (hasMinorThird && hasDimFifth) return "Diminished";
    if (hasMajorThird && hasAugFifth) return "Augmented";
    
    return NULL;  // Return NULL for unknown chords
}

FLASHMEM void updateCurrentChord(const uint8_t midiNotes[], uint8_t size) {
    if (size != 3) {
        currentChord.active = false;
        return;
    }

    uint8_t sortedNotes[3];
    memcpy(sortedNotes, midiNotes, 3 * sizeof(uint8_t));
    sortArray(sortedNotes, 3);

    uint8_t intervals[3];
    const char* chordType = NULL;
    uint8_t root = 0;
    
    // Try each note as potential root
    for (uint8_t i = 0; i < 3; i++) {
        root = sortedNotes[i];
        getIntervals(sortedNotes, 3, root, intervals);
        chordType = detectChordType(intervals, 3);
        if (chordType != NULL) break;
    }
    
    if (chordType == NULL) {
        currentChord.active = false;
        return;
    }
    
    // Store chord information
    currentChord.root = root % 12;
    currentChord.type = chordType;
    currentChord.active = true;
    
    // Set third and fifth based on chord type
    if (strcmp(chordType, "Major") == 0) {
        currentChord.third = (currentChord.root + 4) % 12;
        currentChord.fifth = (currentChord.root + 7) % 12;
    } 
    else if (strcmp(chordType, "Minor") == 0) {
        currentChord.third = (currentChord.root + 3) % 12;
        currentChord.fifth = (currentChord.root + 7) % 12;
    }
    else if (strcmp(chordType, "Diminished") == 0) {
        currentChord.third = (currentChord.root + 3) % 12;
        currentChord.fifth = (currentChord.root + 6) % 12;
    }
    else if (strcmp(chordType, "Augmented") == 0) {
        currentChord.third = (currentChord.root + 4) % 12;
        currentChord.fifth = (currentChord.root + 8) % 12;
    }
}

FLASHMEM uint8_t transformNoteToChord(uint8_t inputNote) {
    if (!currentChord.active) return inputNote; // No chord detected
    
    uint8_t pitchClass = inputNote % 12;
    uint8_t octave = inputNote / 12;
    
    // Check if note is already a chord tone
    if (pitchClass == currentChord.root || 
        pitchClass == currentChord.third || 
        pitchClass == currentChord.fifth) {
        return inputNote;
    }
    
    // Find nearest chord tone (with octave preservation)
    int8_t rootDist = (pitchClass - currentChord.root + 12) % 12;
    int8_t thirdDist = (pitchClass - currentChord.third + 12) % 12;
    int8_t fifthDist = (pitchClass - currentChord.fifth + 12) % 12;
    
    // Convert to absolute distances
    rootDist = rootDist > 6 ? 12 - rootDist : rootDist;
    thirdDist = thirdDist > 6 ? 12 - thirdDist : thirdDist;
    fifthDist = fifthDist > 6 ? 12 - fifthDist : fifthDist;
    
    // Find closest chord tone
    uint8_t closestTone;
    if (rootDist <= thirdDist && rootDist <= fifthDist) {
        closestTone = currentChord.root;
    } else if (thirdDist <= fifthDist) {
        closestTone = currentChord.third;
    } else {
        closestTone = currentChord.fifth;
    }
    
    // Return transformed note in original octave
    return closestTone + octave * 12;
}

FLASHMEM bool getChordName(const uint8_t midiNotes[], uint8_t size, char* output) {
    if (size != 3) return false;

    uint8_t sortedNotes[3];
    memcpy(sortedNotes, midiNotes, 3 * sizeof(uint8_t));
    sortArray(sortedNotes, 3);

    uint8_t intervals[3];
    const char* chordType = NULL;
    uint8_t root = 0;
    
    // Try each note as potential root to find a known chord
    for (uint8_t i = 0; i < 3; i++) {
        root = sortedNotes[i];
        getIntervals(sortedNotes, 3, root, intervals);
        chordType = detectChordType(intervals, 3);
        if (chordType != NULL) break;
    }
    
    // If no known chord type found, return false
    if (chordType == NULL) return false;
    
    // Determine inversion
    const char* inversion = "Root Pos.";
    uint8_t rootPitchClass = root % 12;
    uint8_t lowestPitchClass = sortedNotes[0] % 12;
    
    if (lowestPitchClass != rootPitchClass) {
        uint8_t third = containsInterval(intervals, 3, 3) ? 3 : 4;
        uint8_t thirdPitchClass = (rootPitchClass + third) % 12;
        
        if (lowestPitchClass == thirdPitchClass) {
            inversion = "1st Inv.";
        } else {
            uint8_t fifthPitchClass = (rootPitchClass + 7) % 12;
            if (lowestPitchClass == fifthPitchClass) {
                inversion = "2nd Inv.";
            }
        }
    }
    
    snprintf(output, 30, "%s %s (%s)", midiToNote(root), chordType, inversion);
    return true;
}

FLASHMEM uint8_t getChordRootNote(uint8_t inputNote) {
    if (!currentChord.active) return inputNote;
    
    // Return root note in octave 3 (C3 = 48, C#3 = 49, etc.)
    return currentChord.root + 48; 
}
