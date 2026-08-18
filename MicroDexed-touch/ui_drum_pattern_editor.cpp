#include "ui_drum_pattern_editor.h"
#include "config.h"
#include "LCDMenuLib2.h"
#include "livesequencer.h"
#include "sequencer.h"
#include "drums.h"
#include "touch.h"
#include "UI.h"
#include "MD_REncoder.h"
#include <vector>
#include <cstring>
#include <algorithm>
#include <elapsedMillis.h>
#include <SD.h>  
#include <Audio.h>
#include "UI_liveseq_editor_shared.h"

extern void checkWebRemote();

extern uint8_t sd_card_internal;
static elapsedMillis timeSinceOpened;
static constexpr uint8_t NUM_DRUMSET_ACTIVE = NUM_DRUMSET_CONFIG - 1;  // exclude the empty last sample

// ----------------------------------------------------------------------
// External references
// ----------------------------------------------------------------------
extern LiveSequencer liveSeq;
extern drum_config_t drum_config[NUM_DRUMSET_CONFIG];
extern void handleNoteOn(byte channel, byte note, byte velocity, byte device);
extern uint8_t drum_midi_channel;
extern MD_REncoder ENCODER[];
extern ts_t ts;
extern uint8_t numTouchPoints;
extern LCDMenuLib2 LCDML;
extern sequencer_t seq;

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined RA_DISPLAY
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

bool saveDrumSlotIndices();

// ----------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------
static constexpr uint8_t NUM_DRUM_SLOTS = 12;
static constexpr uint8_t STEPS_PER_BAR = 16;

// Bottom buttons
static constexpr uint16_t BTN_W = TouchButton::BUTTON_SIZE_X;   // 50
static constexpr uint16_t BTN_H = TouchButton::BUTTON_SIZE_Y;   // 35
static constexpr uint16_t BTN_Y = DISPLAY_HEIGHT - 35;
static constexpr uint16_t BTN_START_STOP_X = GRID.X[0];
static constexpr uint16_t BTN_PREV_X = GRID.X[1];
static constexpr uint16_t BTN_NEXT_X = GRID.X[2];
static constexpr uint16_t BTN_LAYER_X = GRID.X[3];
static constexpr uint16_t BTN_COPY_X = GRID.X[4];
static constexpr uint16_t BTN_EXIT_X = GRID.X[5];

static constexpr uint16_t LABEL_WIDTH = 60;
static constexpr uint16_t GRID_START_X = LABEL_WIDTH + 5;
static constexpr uint16_t GRID_START_Y = 25;
static constexpr uint16_t BOTTOM_MARGIN = 45;

static constexpr uint16_t CELL_W = 16;
static constexpr uint16_t CELL_H = (DISPLAY_HEIGHT - GRID_START_Y - BOTTOM_MARGIN) / (NUM_DRUM_SLOTS + 1);

// Sound selection layout
static constexpr uint8_t SELECTION_COLS = 3;
static constexpr uint8_t SELECTION_ROWS_VISIBLE = 6;
static constexpr uint16_t SELECTION_START_X = 10;
static constexpr uint16_t SELECTION_START_Y = 30;
static constexpr uint16_t SELECTION_CELL_W = (DISPLAY_WIDTH - 20) / SELECTION_COLS;
static constexpr uint16_t SELECTION_CELL_H = 24;

// ----------------------------------------------------------------------
// Static data
// ----------------------------------------------------------------------
static int8_t currentBar = 0;
static bool selectionMode = false;
static uint8_t selectedSlot = 0;
static uint8_t selectionScroll = 0;
static uint8_t drumSlotNotes[NUM_DRUM_SLOTS];
uint8_t drumSlotIndices[NUM_DRUM_SLOTS];   // new: persistent indices
static std::vector<std::vector<bool>> noteGrid;
static std::vector<std::vector<uint8_t>> noteVelocity;
static uint8_t activeLayer = 0;
static uint8_t currentVelocity = 100;
static uint8_t lastSeqStep = 0;
static bool lastRunningState = false;

// Copy/paste buffer
static std::vector<std::vector<bool>> copyBuffer;
static std::vector<std::vector<uint8_t>> copyVelocityBuffer;
static bool hasCopied = false;
static bool copyModeActive = false;

// Forward declarations
void redraw();
void loadGrid();
void writeGrid();
void drawGridUI();
void drawSelectionScreen();
void drawDrumList();
void drawRunningIndicator(bool forceFullRedraw = false);
void redrawCell(uint8_t slot, uint8_t step);
void toggleNoteEvent(uint8_t slot, uint8_t step);
void copyPattern();
void pastePattern();
void updateStartStopButton();

// ----------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------

// Show a temporary text message at the top of the screen
FLASHMEM void showMessage(const char* msg) {
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    checkWebRemote();
    display.fillRect(0, 0, DISPLAY_WIDTH, 20, COLOR_BACKGROUND);
    display.setTextSize(1);
    uint16_t textWidth = strlen(msg) * CHAR_width_small;
    uint16_t x = DISPLAY_WIDTH - textWidth;
    display.setCursor(x, 0);
    display.print(msg);
}

FLASHMEM void showDrumSlotList() {
    // display.fillScreen(COLOR_BACKGROUND);
    // display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    // display.setTextSize(2);

    // // Header
    // display.setCursor(0, 0);
    // display.print("LOADED DRUM SLOTS:");
    // display.setTextSize(1);
    // // List each slot line by line
    // for (uint8_t i = 0; i < NUM_DRUM_SLOTS; i++) {
    //     uint8_t idx = drumSlotIndices[i];
    //     const char* name = (idx < NUM_DRUMSET_ACTIVE) ? drum_config[idx].name : "???";
    //     display.setCursor(0, i * 11 + 28);
    //     display.printf("%02d: %s", i + 1, name);
    // }
    // delay(1000);
}


// ----------------------------------------------------------------------
// Find drum_config index by MIDI note
// ----------------------------------------------------------------------
FLASHMEM uint8_t findDrumIndexByNote(uint8_t note) {
    for (uint8_t i = 0; i < NUM_DRUMSET_ACTIVE; i++) {
        if (drum_config[i].midinote == note) return i;
    }
    return 0;   // fallback to first drum
}

static LiveSequencer::LiveSeqData* getData() {
    return liveSeq.getData();
}

static uint16_t getStepMs() {
    return getData()->patternLengthMs / STEPS_PER_BAR;
}

static uint8_t getActiveTrack() {
    return getData()->activeTrack;
}

static uint32_t stepToAbsTime(uint8_t step, int8_t bar) {
    return bar * getData()->patternLengthMs + step * getStepMs();
}

extern uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val);

// ----------------------------------------------------------------------
// Core logic
// ----------------------------------------------------------------------
FLASHMEM void loadGrid() {
    LiveSequencer::LiveSeqData* data = getData();
    for (uint8_t s = 0; s < STEPS_PER_BAR; s++) {
        for (uint8_t slot = 0; slot < NUM_DRUM_SLOTS; slot++) {
            noteGrid[slot][s] = false;
            noteVelocity[slot][s] = 0;
        }
    }

    uint8_t lowest = 0, highest = 127;
    auto pairs = liveSeq.getNotePairsFromTrack(getActiveTrack(), lowest, highest, 0, data->numberOfBars);
    for (const auto& pair : pairs) {
        if (pair.noteOn.layer != activeLayer) continue;
        uint32_t absTime = pair.noteOn.patternNumber * data->patternLengthMs + pair.noteOn.patternMs;
        int8_t bar = absTime / data->patternLengthMs;
        if (bar != currentBar) continue;
        uint16_t step = (absTime % data->patternLengthMs) / getStepMs();
        if (step >= STEPS_PER_BAR) continue;
        for (uint8_t slot = 0; slot < NUM_DRUM_SLOTS; slot++) {
            if (drumSlotNotes[slot] == pair.noteOn.note_in) {
                noteGrid[slot][step] = true;
                noteVelocity[slot][step] = pair.noteOn.note_in_velocity;
                break;
            }
        }
    }
}

FLASHMEM void clearCurrentBar() {
    LiveSequencer::LiveSeqData* data = getData();
    for (auto& event : data->eventsList) {
        if (event.track == getActiveTrack() && event.layer == activeLayer) {
            uint32_t absTime = event.patternNumber * data->patternLengthMs + event.patternMs;
            if ((absTime / data->patternLengthMs) == (uint32_t)currentBar) {
                event.event = midi::InvalidType;
            }
        }
    }
    liveSeq.requestSortEvents();
}

FLASHMEM void writeGrid() {
    LiveSequencer::LiveSeqData* data = getData();
    clearCurrentBar();
    for (uint8_t slot = 0; slot < NUM_DRUM_SLOTS; slot++) {
        uint8_t note = drumSlotNotes[slot];
        if (note == 0) continue;
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            if (noteGrid[slot][step]) {
                uint8_t vel = noteVelocity[slot][step];
                if (vel == 0) vel = 100;
                uint32_t timeOn = stepToAbsTime(step, currentBar);
                uint32_t timeOff = timeOn + getStepMs();
                LiveSequencer::MidiEvent noteOn, noteOff;
                noteOn.event = midi::NoteOn;
                noteOn.track = getActiveTrack();
                noteOn.layer = activeLayer;
                noteOn.note_in = note;
                noteOn.note_in_velocity = vel;
                noteOn.source = LiveSequencer::EVENT_PATTERN;
                noteOn.patternNumber = timeOn / data->patternLengthMs;
                noteOn.patternMs = timeOn % data->patternLengthMs;
                noteOff = noteOn;
                noteOff.event = midi::NoteOff;
                noteOff.patternNumber = timeOff / data->patternLengthMs;
                noteOff.patternMs = timeOff % data->patternLengthMs;
                liveSeq.addNotePair(noteOn, noteOff);
            }
        }
    }
    liveSeq.flushPendingEvents();
    liveSeq.requestSortEvents();
    liveSeq.notifyLayerGridChanged();
    liveSeq.notifyLayerButtonsChanged();

    uint8_t track = getActiveTrack();
    if (activeLayer >= data->trackSettings[track].layerCount) {
        data->trackSettings[track].layerCount = activeLayer + 1;
        liveSeq.notifyLayerButtonsChanged();
    }
    loadGrid();

}

FLASHMEM void playPreview(uint8_t slot) {
    uint8_t note = drumSlotNotes[slot];
    if (note) handleNoteOn(drum_midi_channel, note, currentVelocity, 0);
}

FLASHMEM void toggleNoteEvent(uint8_t slot, uint8_t step) {
    if (slot >= NUM_DRUM_SLOTS || step >= STEPS_PER_BAR) return;

    bool currentlyActive = noteGrid[slot][step];
    uint8_t note = drumSlotNotes[slot];
    if (note == 0) return;

    LiveSequencer::LiveSeqData* data = getData();
    uint32_t timeOn = stepToAbsTime(step, currentBar);
    uint32_t timeOff = timeOn + getStepMs();

    if (currentlyActive) {
        for (auto& event : data->eventsList) {
            if (event.track == getActiveTrack() && event.layer == activeLayer &&
                event.note_in == note &&
                event.patternNumber == (timeOn / data->patternLengthMs) &&
                event.patternMs == (timeOn % data->patternLengthMs) &&
                event.event == midi::NoteOn) {
                event.event = midi::InvalidType;
                break;
            }
        }
        for (auto& event : data->eventsList) {
            if (event.track == getActiveTrack() && event.layer == activeLayer &&
                event.note_in == note &&
                event.patternNumber == (timeOff / data->patternLengthMs) &&
                event.patternMs == (timeOff % data->patternLengthMs) &&
                event.event == midi::NoteOff) {
                event.event = midi::InvalidType;
                break;
            }
        }
        noteGrid[slot][step] = false;
        noteVelocity[slot][step] = 0;
    }
    else {
        LiveSequencer::MidiEvent noteOn, noteOff;
        noteOn.event = midi::NoteOn;
        noteOn.track = getActiveTrack();
        noteOn.layer = activeLayer;
        noteOn.note_in = note;
        noteOn.note_in_velocity = currentVelocity;
        noteOn.source = LiveSequencer::EVENT_PATTERN;
        noteOn.patternNumber = timeOn / data->patternLengthMs;
        noteOn.patternMs = timeOn % data->patternLengthMs;
        noteOff = noteOn;
        noteOff.event = midi::NoteOff;
        noteOff.patternNumber = timeOff / data->patternLengthMs;
        noteOff.patternMs = timeOff % data->patternLengthMs;
        data->eventsList.push_back(noteOn);
        data->eventsList.push_back(noteOff);
        noteGrid[slot][step] = true;
        noteVelocity[slot][step] = currentVelocity;
    }

    liveSeq.requestSortEvents();
    liveSeq.notifyLayerGridChanged();
    liveSeq.notifyLayerButtonsChanged();
    playPreview(slot);
    redrawCell(slot, step);

}

FLASHMEM void setDrumForSlot(uint8_t slot, uint8_t drumIndex) {
    if (slot >= NUM_DRUM_SLOTS) return;
    if (drumIndex >= NUM_DRUMSET_ACTIVE) drumIndex = 0;
    drumSlotNotes[slot] = drum_config[drumIndex].midinote;
    drumSlotIndices[slot] = drumIndex;           // store index
    loadGrid();
    writeGrid();
    redraw();
    if (saveDrumSlotIndices()) {
        // optional: short flash or message already shown by save function
    }
}

FLASHMEM void copyPattern() {
    if (copyBuffer.empty()) {
        copyBuffer.assign(NUM_DRUM_SLOTS, std::vector<bool>(STEPS_PER_BAR, false));
        copyVelocityBuffer.assign(NUM_DRUM_SLOTS, std::vector<uint8_t>(STEPS_PER_BAR, 0));
    }
    for (uint8_t slot = 0; slot < NUM_DRUM_SLOTS; slot++) {
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            copyBuffer[slot][step] = noteGrid[slot][step];
            copyVelocityBuffer[slot][step] = noteVelocity[slot][step];
        }
    }
    hasCopied = true;
    copyModeActive = true;
    drawCopyPasteButton(BTN_COPY_X, BTN_Y, copyModeActive);
}

FLASHMEM void redrawGridOnly() {
    for (uint8_t slot = 0; slot < NUM_DRUM_SLOTS; slot++) {
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            redrawCell(slot, step);
        }
    }
    drawRunningIndicator(true);
}

FLASHMEM void pastePattern() {
    if (!hasCopied) return;

    // 1. Copy the buffer into the local working grid
    for (uint8_t slot = 0; slot < NUM_DRUM_SLOTS; slot++) {
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            noteGrid[slot][step] = copyBuffer[slot][step];
            noteVelocity[slot][step] = copyVelocityBuffer[slot][step];
        }
    }

    // 2. Write the local grid to the sequencer (this adds events to pendingEvents)
    writeGrid();

    // 3. FORCE PENDING EVENTS TO BE MERGED INTO THE MAIN EVENT LIST
    liveSeq.flushPendingEvents();   // merges pendingEvents into eventsList
    liveSeq.requestSortEvents();    // sorts the updated eventsList

    // 4. RELOAD the grid from the sequencer's main event list
    loadGrid();

    // 5. Reset copy mode and redraw the copy button
    copyModeActive = false;
    drawCopyPasteButton(BTN_COPY_X, BTN_Y, copyModeActive);

    // 6. Force a complete redraw of the grid (now using the fresh data)
    redrawGridOnly();
}

// ----------------------------------------------------------------------
// Drawing functions
// ----------------------------------------------------------------------
FLASHMEM void drawDrumList() {
    uint8_t totalRows = (NUM_DRUMSET_ACTIVE + SELECTION_COLS - 1) / SELECTION_COLS;
    uint8_t startRow = selectionScroll;

    // Page number (optional, can be drawn in drawSelectionScreen)
    uint8_t rowsPerPage = SELECTION_ROWS_VISIBLE;
    uint8_t currentPage = (selectionScroll / rowsPerPage) + 1;
    uint8_t totalPages = (totalRows + rowsPerPage - 1) / rowsPerPage;

    display.setCursor(DISPLAY_WIDTH - 80, 10);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(1);

    display.printf("PAGE %d/%d", currentPage, totalPages);

    for (uint8_t row = 0; row < SELECTION_ROWS_VISIBLE; row++) {
        uint8_t actualRow = startRow + row;
        for (uint8_t col = 0; col < SELECTION_COLS; col++) {
            uint16_t x = SELECTION_START_X + col * SELECTION_CELL_W;
            uint16_t y = SELECTION_START_Y + row * SELECTION_CELL_H;
            uint8_t idx = actualRow * SELECTION_COLS + col;

            // Determine if this cell should be drawn or cleared
            bool isValid = (actualRow < totalRows) && (idx < NUM_DRUMSET_ACTIVE);
            if (isValid) {
                uint8_t currentNote = drumSlotNotes[selectedSlot];
                // uint8_t candidateNote = drum_config[idx].midinote & 0x7F;
                uint8_t candidateNote = drum_config[idx].midinote;
                bool isSelected = (currentNote == candidateNote);
                uint16_t bg = isSelected ? MIDDLEGREEN : DX_DARKCYAN;
                checkWebRemote();
                display.fillRect(x, y, SELECTION_CELL_W - 2, SELECTION_CELL_H - 2, bg);
                display.setTextColor(COLOR_SYSTEXT, bg);
                display.setCursor(x + 4, y + 6);
                display.setTextSize(1);
                // Truncate name
                const char* fullName = drum_config[idx].name;
                const size_t maxLen = 13;
                char truncatedName[16];
                if (strlen(fullName) > maxLen) {
                    strncpy(truncatedName, fullName, maxLen);
                    truncatedName[maxLen] = '\0';
                    strcat(truncatedName, "..");
                }
                else {
                    strcpy(truncatedName, fullName);
                }
                display.print(truncatedName);
            }
            else {
                // Clear this cell (no sample)
                checkWebRemote();
                display.fillRect(x, y, SELECTION_CELL_W - 2, SELECTION_CELL_H - 2, COLOR_BACKGROUND);
            }
        }
    }
}

FLASHMEM void drawSelectionScreen() {
    // display.fillScreen(COLOR_BACKGROUND);
    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setCursor(10, 10);
    display.printf("SELECT DRUM FOR SLOT %d", selectedSlot + 1);

    // --- Page number display ---
    uint8_t totalRows = (NUM_DRUMSET_ACTIVE + SELECTION_COLS - 1) / SELECTION_COLS;
    uint8_t rowsPerPage = SELECTION_ROWS_VISIBLE;
    uint8_t totalPages = (totalRows + rowsPerPage - 1) / rowsPerPage;
    uint8_t currentPage = (selectionScroll / rowsPerPage) + 1;
    display.setCursor(DISPLAY_WIDTH - 80, 10);
    display.printf("PAGE %d/%d", currentPage, totalPages);
    // -------------------------


    drawDrumList();
    TouchButton::drawButton(10, DISPLAY_HEIGHT - 40, "PAGE-", "", TouchButton::BUTTON_ACTIVE);
    TouchButton::drawButton(80, DISPLAY_HEIGHT - 40, "PAGE+", "", TouchButton::BUTTON_ACTIVE);
    TouchButton::drawButton(DISPLAY_WIDTH - 60, DISPLAY_HEIGHT - 40, "CANCEL", "", TouchButton::BUTTON_RED);
}

FLASHMEM void drawStartStopButton() {
    LiveSequencer::LiveSeqData* data = getData();
    if (data->isRunning) {
        TouchButton::drawButton(BTN_START_STOP_X, BTN_Y, "STOP", "SEQ", TouchButton::BUTTON_RED);
    }
    else {
        TouchButton::drawButton(BTN_START_STOP_X, BTN_Y, "START", "SEQ", TouchButton::BUTTON_ACTIVE);
    }
}

FLASHMEM void updateStartStopButton() {
    LiveSequencer::LiveSeqData* data = getData();
    if (data->isRunning != lastRunningState) {
        lastRunningState = data->isRunning;
        drawStartStopButton(BTN_START_STOP_X, BTN_Y, data->isRunning);
        if (!data->isRunning) {
            // Reset step display when stopped
            drawRunningIndicator(true); // force full redraw of indicator row (all grey)
        }
    }
}

FLASHMEM void drawLayerButton() {
    char layerNum[4];
    sprintf(layerNum, "%u", activeLayer + 1);
    TouchButton::drawButton(BTN_LAYER_X, BTN_Y, "LAYER", layerNum, TouchButton::BUTTON_HIGHLIGHTED);
}

FLASHMEM void drawCopyButton() {
    if (copyModeActive) {
        TouchButton::drawButton(BTN_COPY_X, BTN_Y, "PASTE", "PAT", TouchButton::BUTTON_ACTIVE);
    }
    else {
        TouchButton::drawButton(BTN_COPY_X, BTN_Y, "COPY", "PAT", TouchButton::BUTTON_ACTIVE);
    }
}

//  static uint8_t lastIndicatorStep = 255;
//  static uint8_t lastDisplayedBar = 255;

FLASHMEM void drawRunningIndicator(bool forceFullRedraw) {
    LiveSequencer::LiveSeqData* data = getData();
    uint16_t y = GRID_START_Y + NUM_DRUM_SLOTS * CELL_H + 4;
    static uint8_t lastStep = 255;
    static uint8_t lastDisplayedBar = 255;
    //static bool lastRunning = false;

    bool runningNow = data->isRunning;
    //bool justStarted = (runningNow && !lastRunning);
    //lastRunning = runningNow;

    // Handle pending first-step draw (set by start button)
    if (pendingFirstStepDraw && runningNow) {
        pendingFirstStepDraw = false;

        for (uint8_t step = 0; step < 2; step++) {
            uint16_t x = GRID_START_X + step * CELL_W;
            checkWebRemote();
            display.fillRect(x, y, CELL_W - 1, CELL_H - 1, GREY3);
        }
        // Draw step 0 explicitly
        uint8_t currentStep = 0;
        bool isPlayingCurrentBar = (data->currentPattern == (uint32_t)currentBar);
        uint16_t cursorColor = isPlayingCurrentBar ? RED : GREY1;
        uint16_t xCurr = GRID_START_X + currentStep * CELL_W;
        checkWebRemote();
        display.fillRect(xCurr, y, CELL_W - 1, CELL_H - 1, cursorColor);
        lastStep = currentStep;
        lastDisplayedBar = currentBar;
        return;
    }

    // Full redraw on force or bar change (but not on justStarted, because we handled it above)
    if (forceFullRedraw || currentBar != lastDisplayedBar) {
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            uint16_t x = GRID_START_X + step * CELL_W;
            checkWebRemote();
            display.fillRect(x, y, CELL_W - 1, CELL_H - 1, GREY3);
        }
        lastStep = 255;
        lastDisplayedBar = currentBar;
        if (!runningNow) return;
    }

    if (!runningNow) {
        if (lastStep != 255) {
            uint16_t x = GRID_START_X + lastStep * CELL_W;
            checkWebRemote();
            display.fillRect(x, y, CELL_W - 1, CELL_H - 1, GREY3);
            lastStep = 255;
        }
        return;
    }

    uint8_t currentStep = seq.step % STEPS_PER_BAR;
    bool wrap = (lastStep == STEPS_PER_BAR - 1 && currentStep == 0);
    uint8_t barForColor = wrap ? (data->currentPattern + 1) % data->numberOfBars : data->currentPattern;
    bool isPlayingCurrentBar = (barForColor == (uint32_t)currentBar);

    // If we are at the very beginning (lastStep == 255) and not handled by pending, draw current step
    if (lastStep == 255) {
        uint16_t xCurr = GRID_START_X + currentStep * CELL_W;
        checkWebRemote();
        display.fillRect(xCurr, y, CELL_W - 1, CELL_H - 1, isPlayingCurrentBar ? RED : GREY2);
        lastStep = currentStep;
        return;
    }

    if (wrap) {
        if (lastStep != 255) {
            uint16_t xPrev = GRID_START_X + lastStep * CELL_W;
            checkWebRemote();
            display.fillRect(xPrev, y, CELL_W - 1, CELL_H - 1, GREY3);
        }
        uint16_t xCurr = GRID_START_X + currentStep * CELL_W;
        checkWebRemote();
        display.fillRect(xCurr, y, CELL_W - 1, CELL_H - 1, isPlayingCurrentBar ? RED : GREY2);
        lastStep = currentStep;
        return;
    }

    if (currentStep == lastStep && !forceFullRedraw) return;
    if (lastStep != 255 && lastStep != currentStep) {
        uint16_t xPrev = GRID_START_X + lastStep * CELL_W;
        checkWebRemote();
        display.fillRect(xPrev, y, CELL_W - 1, CELL_H - 1, GREY3);
    }
    uint16_t xCurr = GRID_START_X + currentStep * CELL_W;
    checkWebRemote();
    display.fillRect(xCurr, y, CELL_W - 1, CELL_H - 1, isPlayingCurrentBar ? RED : GREY2);
    lastStep = currentStep;
}

FLASHMEM void redrawCell(uint8_t slot, uint8_t step) {
    uint16_t x = GRID_START_X + step * CELL_W;
    uint16_t y = GRID_START_Y + slot * CELL_H;
    uint16_t bgColor = ((step / 4) % 2 == 0) ? GREY3 : GREY2;
    checkWebRemote();
    display.fillRect(x, y, CELL_W - 1, CELL_H - 1, bgColor);
    if (noteGrid[slot][step]) {
        uint8_t vel = noteVelocity[slot][step];
        if (vel == 0) vel = 100;
        uint16_t noteColor = velocityToColor(vel);
        checkWebRemote();
        display.fillRect(x, y, CELL_W - 1, CELL_H - 1, noteColor);
    }
}


FLASHMEM void drawBarMeasure() {
    LiveSequencer::LiveSeqData* data = getData();
    uint16_t y = GRID_START_Y + NUM_DRUM_SLOTS * CELL_H + 2;
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(1);
    display.setCursor(13, y + 5);
    if (data->isRunning) {
        display.printf("BAR %d/%d", data->currentPattern + 1, data->numberOfBars);
    }
    else {
        display.print("BAR -/-");
    }
}

FLASHMEM void drawTrackNumber()
{
    display.setCursor(0, TOP_TEXT_Y);
    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.printf("TRACK %02d", getActiveTrack() + 1);
}

FLASHMEM void drawBarLayer()
{
    LiveSequencer::LiveSeqData* data = getData();
    display.setCursor(CHAR_width_small * 10, TOP_TEXT_Y);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(1);
    display.printf("BAR %d/%d LAYER %d", currentBar + 1, data->numberOfBars, activeLayer + 1);

}

FLASHMEM void drawGridUI() {
    LiveSequencer::LiveSeqData* data = getData();
    display.fillScreen(COLOR_BACKGROUND);
    display.setTextSize(1);
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.setCursor(0, 0);
    display.printf("DRUM PATTERN TOUCH EDITOR");
    drawTrackNumber();
    drawBarLayer();
    drawVelocitySlider(currentVelocity);

    for (uint8_t slot = 0; slot < NUM_DRUM_SLOTS; slot++) {
        uint8_t y = GRID_START_Y + slot * CELL_H;
        checkWebRemote();
        display.fillRect(0, y, LABEL_WIDTH, CELL_H - 1, DX_DARKCYAN);
        display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
        display.setCursor(2, y + 3);
        uint8_t note = drumSlotNotes[slot];
        bool found = false;
        for (uint8_t i = 0; i < NUM_DRUMSET_ACTIVE; i++) {
            //  if ((drum_config[i].midinote & 0x7F) == note) {
            if ((drum_config[i].midinote) == note) {
                char shortName[9];
                strncpy(shortName, drum_config[i].name, 8);
                shortName[8] = 0;
                display.print(shortName);
                found = true;
                break;
            }
        }
        if (!found) display.print("---");

        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            uint16_t x = GRID_START_X + step * CELL_W;
            uint16_t bgColor = ((step / 4) % 2 == 0) ? GREY3 : GREY2;
            checkWebRemote();
            display.fillRect(x, y, CELL_W - 1, CELL_H - 1, bgColor);
            if (noteGrid[slot][step]) {
                uint8_t vel = noteVelocity[slot][step];
                if (vel == 0) vel = 100;
                uint16_t noteColor = velocityToColor(vel);
                checkWebRemote();
                display.fillRect(x, y, CELL_W - 1, CELL_H - 1, noteColor);
            }
        }
    }

    drawBarMeasure();
    // Running indicator row (always visible)
    drawRunningIndicator(true);

    // if (data->isRunning) {
    //     uint32_t now = data->currentPattern * data->patternLengthMs + data->patternTimer;
    //     if ((now / data->patternLengthMs) == (uint32_t)currentBar) {
    //         uint16_t step = (now % data->patternLengthMs) / getStepMs();
    //         if (step < STEPS_PER_BAR) {
    //             uint16_t cursorX = GRID_START_X + step * CELL_W;
    //           // display.drawRect(cursorX, GRID_START_Y, CELL_W, NUM_DRUM_SLOTS * CELL_H, RED);
    //             display.drawRect(200,1,15,7, RED);
    //         }
    //     }
    // }

    drawStartStopButton(BTN_START_STOP_X, BTN_Y, data->isRunning);
    TouchButton::drawButton(BTN_PREV_X, BTN_Y, "< BAR", "", TouchButton::BUTTON_ACTIVE);
    TouchButton::drawButton(BTN_NEXT_X, BTN_Y, "BAR >", "", TouchButton::BUTTON_ACTIVE);
    drawLayerButton();
    drawCopyPasteButton(BTN_COPY_X, BTN_Y, copyModeActive);
    TouchButton::drawButton(BTN_EXIT_X, BTN_Y, "EXIT", "", TouchButton::BUTTON_RED);
}

FLASHMEM void redraw() {
    if (selectionMode)
    {
        // display.fillScreen(COLOR_BACKGROUND);
        drawSelectionScreen();
    }

    else
        drawGridUI();
}

// ----------------------------------------------------------------------
// Touch handling with sliding for velocity
// ----------------------------------------------------------------------
FLASHMEM void handleMainTouch(int16_t x, int16_t y) {
    if (timeSinceOpened < 500) return;
    LiveSequencer::LiveSeqData* data = getData();

    // Velocity slider – always update if touch is inside slider area
    if (y >= VEL_SLIDER_Y - 5 && y <= VEL_SLIDER_Y + VEL_SLIDER_H + 5 &&
        x >= VEL_SLIDER_X - 5 && x <= VEL_VALUE_X + 25) {
        setVelocityFromTouch(x, currentVelocity);
        return;
    }



    // Grid cells
    // else if (x >= GRID_START_X && x < GRID_START_X + STEPS_PER_BAR * CELL_W &&
    //     y >= GRID_START_Y && y < GRID_START_Y + NUM_DRUM_SLOTS * CELL_H) {
    //     uint8_t step = (x - GRID_START_X) / CELL_W;
    //     uint8_t slot = (y - GRID_START_Y) / CELL_H;
    //     if (slot < NUM_DRUM_SLOTS && step < STEPS_PER_BAR) toggleNoteEvent(slot, step);
    //     return;
    // }


    else if (x >= GRID_START_X && x < GRID_START_X + STEPS_PER_BAR * CELL_W &&
        y >= GRID_START_Y - 2 && y < GRID_START_Y + NUM_DRUM_SLOTS * CELL_H + 4) {
        uint8_t step = (x - GRID_START_X) / CELL_W;
        uint8_t slot = (y - GRID_START_Y) / CELL_H;
        if (slot < NUM_DRUM_SLOTS && step < STEPS_PER_BAR) {
            toggleNoteEvent(slot, step);
            return;
        }
    }

    // Bottom buttons
    else if (y >= BTN_Y && y < BTN_Y + BTN_H) {
        if (x >= BTN_START_STOP_X && x < BTN_START_STOP_X + BTN_W) {
            if (data->isRunning) liveSeq.stop();
            else liveSeq.start();
            // Force immediate update of button and running indicator
            drawStartStopButton(BTN_START_STOP_X, BTN_Y, data->isRunning);
            if (!data->isRunning) {
                drawRunningIndicator(true);
            }
            return;
        }
        else   if (x >= BTN_PREV_X && x < BTN_PREV_X + BTN_W) {
            if (currentBar > 0) { currentBar--; loadGrid(); redrawGridOnly(); }
            drawBarLayer();
            drawLayerButton();
            return;
        }
        else   if (x >= BTN_NEXT_X && x < BTN_NEXT_X + BTN_W) {
            if (currentBar < (int8_t)(data->numberOfBars - 1)) { currentBar++; loadGrid(); redrawGridOnly(); }

            drawBarLayer();
            drawLayerButton();
            return;
        }
        else   if (x >= BTN_LAYER_X && x < BTN_LAYER_X + BTN_W) {
            activeLayer = (activeLayer + 1) % 4;
            loadGrid();
            redrawGridOnly();

            drawBarLayer();
            drawLayerButton();
            return;
        }
        else   if (x >= BTN_COPY_X && x < BTN_COPY_X + BTN_W) {
            if (copyModeActive) pastePattern();
            else copyPattern();
            display.setTextSize(1);
            return;
        }
        else   if (x >= BTN_EXIT_X && x < BTN_EXIT_X + BTN_W) {
            LCDML.FUNC_goBackToMenu();
            return;
        }
        return;
    }

    // Drum label area (assign drum to slot)
    else if (x >= 0 && x < LABEL_WIDTH && y >= GRID_START_Y && y < GRID_START_Y + NUM_DRUM_SLOTS * CELL_H) {
        uint8_t slot = (y - GRID_START_Y) / CELL_H;
        if (slot < NUM_DRUM_SLOTS) {
            selectedSlot = slot;
            selectionMode = true;
            selectionScroll = 0;
            display.fillScreen(COLOR_BACKGROUND);
            drawSelectionScreen();
        }
    }
}

FLASHMEM void handleSelectionTouch(int16_t x, int16_t y) {
    if (timeSinceOpened < 1500) return;
    // uint8_t totalRows = (NUM_DRUMSET_ACTIVE + SELECTION_COLS - 1) / SELECTION_COLS;

    if (y >= DISPLAY_HEIGHT - 40 && y < DISPLAY_HEIGHT) {
        uint8_t totalRows = (NUM_DRUMSET_ACTIVE + SELECTION_COLS - 1) / SELECTION_COLS;
        uint8_t rowsPerPage = SELECTION_ROWS_VISIBLE;
        uint8_t totalPages = (totalRows + rowsPerPage - 1) / rowsPerPage;
        uint8_t currentPage = selectionScroll / rowsPerPage;  // 0‑based

        if (x >= 10 && x < 80) {
            if (currentPage > 0) {
                currentPage--;
                selectionScroll = currentPage * rowsPerPage;
                drawSelectionScreen();        // redraw everything once
            }
            return;
        }
        if (x >= 80 && x < 150) {
            if (currentPage + 1 < totalPages) {
                currentPage++;
                selectionScroll = currentPage * rowsPerPage;
                drawSelectionScreen();        // redraw everything once
            }
            return;
        }


        if (x >= DISPLAY_WIDTH - 80) {
            selectionMode = false;
            redraw();
            return;
        }
        return;
    }

    for (uint8_t row = 0; row < SELECTION_ROWS_VISIBLE; row++) {
        for (uint8_t col = 0; col < SELECTION_COLS; col++) {
            uint16_t cellX = SELECTION_START_X + col * SELECTION_CELL_W;
            uint16_t cellY = SELECTION_START_Y + row * SELECTION_CELL_H;
            if (x >= cellX && x < cellX + SELECTION_CELL_W - 2 &&
                y >= cellY && y < cellY + SELECTION_CELL_H - 2) {
                uint8_t idx = (selectionScroll + row) * SELECTION_COLS + col;
                if (idx < NUM_DRUMSET_ACTIVE) {
                    setDrumForSlot(selectedSlot, idx);
                    selectionMode = false;
                    redraw();
                }
                return;
            }
        }
    }
}

// ----------------------------------------------------------------------
// Preselection: custom notes
// slot0 (top) = 91 (G#6), slot1 = 100 (E7), slots2..11 = 81 down to 72
// ----------------------------------------------------------------------
FLASHMEM void initDefaultDrumSlots() {
    // default MIDI notes (original mapping)
    const uint8_t defaultNotes[NUM_DRUM_SLOTS] = {
        91, 100, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72
    };
    for (uint8_t i = 0; i < NUM_DRUM_SLOTS; i++) {
        drumSlotNotes[i] = defaultNotes[i];
        drumSlotIndices[i] = findDrumIndexByNote(defaultNotes[i]);
    }
}

// ----------------------------------------------------------------------
// Persistent storage of drum slot indices (SD card)
// ----------------------------------------------------------------------
static const char* DRUM_SLOTS_FILE = "/drum_slots.dat";

FLASHMEM bool saveDrumSlotIndices() {
    if (sd_card_internal == 0) {
        showMessage("NO SD CARD - CAN NOT SAVE");
        delay(1000);
        return false;
    }

    AudioNoInterrupts();
    File f = SD.open(DRUM_SLOTS_FILE, FILE_WRITE);
    AudioInterrupts();
    if (!f) {
        showMessage("SAVE FAILED: CAN NOT OPEN FILE");
        delay(1000);
        return false;
    }

    // Ensure we start at the beginning (overwrite)
    f.seek(0);

    AudioNoInterrupts();
    size_t written = f.write(drumSlotIndices, NUM_DRUM_SLOTS);
    f.flush();
    f.close();
    AudioInterrupts();

    if (written != NUM_DRUM_SLOTS) {
        char buf[40];
        snprintf(buf, sizeof(buf), "SAVE ERROR: WROTE %d/%d BYTES", (int)written, NUM_DRUM_SLOTS);
        showMessage(buf);
        delay(1500);
        return false;
    }

    // Verify file size after saving
    AudioNoInterrupts();
    File check = SD.open(DRUM_SLOTS_FILE, FILE_READ);
    AudioInterrupts();
    if (check) {
        size_t actualSize = check.size();
        check.close();
        if (actualSize != NUM_DRUM_SLOTS) {
            char buf[40];
            snprintf(buf, sizeof(buf), "FILE SIZE MISMATCH: %d", (int)actualSize);
            showMessage(buf);
            delay(1500);
            return false;
        }
    }

    // showMessage("DRUM SLOTS SAVED");
    // delay(600);
    return true;
}

FLASHMEM bool loadDrumSlotIndices() {
    if (sd_card_internal == 0) {
        showMessage("NO SD CARD - USING DEFAULTS");
        delay(1000);
        return false;
    }

    AudioNoInterrupts();
    File f = SD.open(DRUM_SLOTS_FILE, FILE_READ);
    AudioInterrupts();
    if (!f) {
        showMessage("NO SAVE FILE - USING DEFAULTS");
        delay(1000);
        return false;
    }

    size_t fileSize = f.size();
    if (fileSize != NUM_DRUM_SLOTS) {
        char buf[50];
        snprintf(buf, sizeof(buf), "PREFS CORRUPT (SIZE=%d, EXPECTED=%d)", (int)fileSize, NUM_DRUM_SLOTS);
        showMessage(buf);
        delay(2000);
        f.close();
        // Optionally remove the corrupt file
        SD.remove(DRUM_SLOTS_FILE);
        return false;
    }

    uint8_t temp[NUM_DRUM_SLOTS];
    AudioNoInterrupts();
    size_t bytes = f.read(temp, NUM_DRUM_SLOTS);
    f.close();
    AudioInterrupts();

    if (bytes != NUM_DRUM_SLOTS) {
        showMessage("READ ERROR - USING DEFAULTS");
        delay(1500);
        return false;
    }

    memcpy(drumSlotIndices, temp, NUM_DRUM_SLOTS);
    for (uint8_t i = 0; i < NUM_DRUM_SLOTS; i++) {
        if (drumSlotIndices[i] >= NUM_DRUMSET_ACTIVE) drumSlotIndices[i] = 0;
        drumSlotNotes[i] = drum_config[drumSlotIndices[i]].midinote;
    }

    showDrumSlotList();
    return true;
}

// ----------------------------------------------------------------------
// LCDML entry point
// ----------------------------------------------------------------------
FLASHMEM void UI_func_drum_pattern_editor(uint8_t param) {
    if (LCDML.FUNC_setup()) {
        if (!loadDrumSlotIndices()) {
            initDefaultDrumSlots();
            if (sd_card_internal) {
                saveDrumSlotIndices();   // write defaults to SD if card present
            }
        }
        // 3. Now drumSlotNotes and drumSlotIndices are ready
        timeSinceOpened = 0;
        noteGrid.assign(NUM_DRUM_SLOTS, std::vector<bool>(STEPS_PER_BAR, false));
        noteVelocity.assign(NUM_DRUM_SLOTS, std::vector<uint8_t>(STEPS_PER_BAR, 0));
        activeLayer = 0;
        currentBar = 0;
        currentVelocity = 100;
        hasCopied = false;
        copyModeActive = false;
        lastSeqStep = 255;
        lastRunningState = getData()->isRunning;
        loadGrid();
        redraw();
        LCDML.FUNC_setLoopInterval(30);
    }
    if (LCDML.FUNC_loop()) {
        // Update running indicator and start/stop button if state changed
        updateStartStopButton();
        drawRunningIndicator(); // only updates if step changed

        static uint8_t lastPlaybackBar = 0xFF;
        // static bool lastRunningState = false;
        LiveSequencer::LiveSeqData* data = getData();

        if (data->isRunning) {
            if (data->currentPattern != lastPlaybackBar) {
                lastPlaybackBar = data->currentPattern;
                drawBarMeasure();
            }
        }
        else if (lastRunningState != data->isRunning) {
            // Sequencer just stopped -> show dashes
            drawBarMeasure();
        }
        lastRunningState = data->isRunning;

        static bool wasTouched = false;
        if (numTouchPoints > 0) {
            if (!wasTouched) {
                int16_t x = ts.p.x;
                int16_t y = ts.p.y;
                if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) {
                    if (selectionMode)
                        handleSelectionTouch(x, y);
                    else
                        handleMainTouch(x, y);
                }
                wasTouched = true;
            }
        }
        else {
            wasTouched = false;
        }

        const EncoderEvents e_right = getEncoderEvents(ENC_R);
        if (e_right.turned && !selectionMode) {
            LiveSequencer::LiveSeqData* data = getData();
            if (e_right.dir > 0 && currentBar < (int8_t)(data->numberOfBars - 1)) {
                currentBar++; loadGrid(); redrawGridOnly();

                drawBarLayer();
                drawLayerButton();
            }
            else if (e_right.dir < 0 && currentBar > 0) {
                currentBar--; loadGrid(); redrawGridOnly();

                drawBarLayer();
                drawLayerButton();
            }
        }
    }
    if (LCDML.FUNC_close()) {
        writeGrid();
        saveDrumSlotIndices();
        liveSeq.notifyLayerButtonsChanged();
        display.fillScreen(COLOR_BACKGROUND);
    }
}
