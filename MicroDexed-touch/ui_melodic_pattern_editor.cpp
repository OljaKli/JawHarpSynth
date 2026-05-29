#include "ui_melodic_pattern_editor.h"
#include "config.h"
#include "LCDMenuLib2.h"
#include "livesequencer.h"
#include "sequencer.h"
#include "touch.h"
#include "UI.h"
#include "virtualkeyboard.h"
#include "MD_REncoder.h"
#include <cstring>
#include <elapsedMillis.h>
#include "UI_liveseq_editor_shared.h"

extern void checkWebRemote();

// Virtual keyboard area starts at the control buttons row (y ~80)
static constexpr uint16_t VIRT_KEYB_TOP = 80;
static elapsedMillis timeSinceOpened;
// ----------------------------------------------------------------------
// External references
// ----------------------------------------------------------------------
extern LiveSequencer liveSeq;
extern MD_REncoder ENCODER[];
extern ts_t ts;
extern int numTouchPoints;
extern LCDMenuLib2 LCDML;
extern sequencer_t seq;

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined (RA_DISPLAY)
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

extern uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val);
static bool lastRunningState = false;

// ----------------------------------------------------------------------
// Constants – 4 rows (bars) × 16 steps, full width, compact cells
// ----------------------------------------------------------------------
static constexpr uint8_t STEPS_PER_BAR = 16;
static constexpr uint8_t NUM_BARS = 4;               // 4 bars = 64 steps total
static constexpr uint8_t NUM_LAYERS = 4;             // layers (not displayed at once)
static constexpr uint16_t GRID_START_X = 0;
static constexpr uint16_t GRID_START_Y = 28;
static constexpr uint16_t CELL_W = DISPLAY_WIDTH / STEPS_PER_BAR;   // 320/16 = 20
static constexpr uint16_t CELL_H = 13;               // reduced to move buttons up

// Buttons – Start/Stop, Layer, Copy, Exit (no bar buttons)
static constexpr uint16_t BTN_W = TouchButton::BUTTON_SIZE_X;   // 50
static constexpr uint16_t BTN_H = TouchButton::BUTTON_SIZE_Y;   // 35
static constexpr uint16_t BTN_Y = GRID_START_Y + NUM_BARS * CELL_H + 6;  // now ~28+52+6=86
static constexpr uint16_t BTN_START_STOP_X = GRID.X[0];
static constexpr uint16_t BTN_LAYER_X = GRID.X[2];
static constexpr uint16_t BTN_COPY_X = GRID.X[3];
static constexpr uint16_t BTN_EXIT_X = GRID.X[5];

//  Clear Step and Auto Advance
static constexpr uint16_t BTN_CLEAR_X = GRID.X[1];
static constexpr uint16_t BTN_AUTO_X = GRID.X[4];
static constexpr uint16_t BTN_Y_ROW = BTN_Y;   // same row as existing buttons
static uint8_t autoAdvanceMode = 0;   // 0=off, 1=bar advance, 2=full advance



// ----------------------------------------------------------------------
// Static data
// ----------------------------------------------------------------------
static uint8_t currentVelocity = 100;
static uint8_t activeLayer = 0;
static bool copyModeActive = false;
static bool hasCopied = false;

// Storage: [layer][bar][step]
static uint8_t noteGrid[NUM_LAYERS][NUM_BARS][STEPS_PER_BAR];
static uint8_t velGrid[NUM_LAYERS][NUM_BARS][STEPS_PER_BAR];

static uint8_t selectedBar = 0;
static uint8_t selectedStep = 0;
static uint8_t selectedCellNote = 0;
static uint8_t currentHighlightedBar = 0xFF;
static uint8_t currentHighlightedStep = 0xFF;

// ----------------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------------

void loadGrid_melodic();
void writeGrid_melodic();
void redrawCell_melodic(uint8_t bar, uint8_t step, bool highlight = false);
void setCellNote_melodic(uint8_t bar, uint8_t step, uint8_t note);
void redraw_melodic();
void handleMainTouch_melodic(int16_t x, int16_t y);

// ----------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------

// Add at the top of the file, after the includes
FLASHMEM void setVirtualKeyboardInstrumentFromTrack() {
    LiveSequencer::LiveSeqData* data = liveSeq.getData();
    if (!data) return;
    uint8_t track = data->activeTrack;
    uint8_t instr = data->trackSettings[track].instrument;

    switch (instr) {
    case LiveSequencer::INSTR_DX1:  ts.virtual_keyboard_instrument = VK_DEXED0; break;
    case LiveSequencer::INSTR_DX2:  ts.virtual_keyboard_instrument = VK_DEXED1; break;
    case LiveSequencer::INSTR_DX3:  ts.virtual_keyboard_instrument = VK_DEXED2; break;
    case LiveSequencer::INSTR_DX4:  ts.virtual_keyboard_instrument = VK_DEXED3; break;
    case LiveSequencer::INSTR_EP:   ts.virtual_keyboard_instrument = VK_EPIANO; break;
    case LiveSequencer::INSTR_MS1:  ts.virtual_keyboard_instrument = VK_MICROSYNTH0; break;
    case LiveSequencer::INSTR_MS2:  ts.virtual_keyboard_instrument = VK_MICROSYNTH1; break;
    case LiveSequencer::INSTR_BRD:  ts.virtual_keyboard_instrument = VK_BRAIDS; break;
    case LiveSequencer::INSTR_DRUM: ts.virtual_keyboard_instrument = VK_DRUMS; break;
    case LiveSequencer::INSTR_GRA:  ts.virtual_keyboard_instrument = VK_GRANULAR; break;
    default:                        ts.virtual_keyboard_instrument = VK_DEXED0; break;
    }
    // This function also sets the correct keyboard mode (keys/pads) and octave range
    virtual_keyboard_smart_preselect_mode();
}


static std::string noteNumberToString(uint8_t note) {
    if (note == 0) return "   ";
    int octave = (note / 12) - 1;
    uint8_t n = note % 12;
    char buf[8];
    char name[4];
    strcpy_P(name, noteNames[n]);
    snprintf(buf, sizeof(buf), "%s%d", name, octave);
    return std::string(buf);
}

extern void drawTrackNumber();

FLASHMEM void drawClearStepButton() {
    uint8_t note = noteGrid[activeLayer][selectedBar][selectedStep];
    TouchButton::Color color = (note != 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL;
    TouchButton::drawButton(BTN_CLEAR_X, BTN_Y_ROW, "CLEAR", "STEP", color);
}

FLASHMEM void drawAutoAdvanceButton() {
    TouchButton::Color color = (autoAdvanceMode != 0) ? TouchButton::BUTTON_RED : TouchButton::BUTTON_NORMAL;
    const char* line1;
    const char* line2;
    switch (autoAdvanceMode) {
    case 0: line1 = "AUTO"; line2 = "ADVANCE"; break;
    case 1: line1 = "BAR";  line2 = "ADVANCE";    break;
    case 2: line1 = "FULL"; line2 = "ADVANCE"; break;
    default: line1 = "AUTO"; line2 = "ADVANCE"; break;
    }
    TouchButton::drawButton(BTN_AUTO_X, BTN_Y_ROW, line1, line2, color);
}

FLASHMEM void updateClearStepButton() {
    drawClearStepButton();  // redraw with current note presence
}

FLASHMEM void advanceSelectedStep() {
    if (autoAdvanceMode == 0) return;

    uint8_t oldBar = selectedBar;
    uint8_t oldStep = selectedStep;

    if (autoAdvanceMode == 1) {
        // BAR ADV: stay in same bar, wrap within 0..STEPS_PER_BAR-1
        selectedStep = (selectedStep + 1) % STEPS_PER_BAR;
    }
    else { // mode 2: FULL ADV
        if (selectedStep + 1 >= STEPS_PER_BAR) {
            // End of current bar -> move to next bar, step 0
            selectedStep = 0;
            selectedBar = (selectedBar + 1) % NUM_BARS;
        }
        else {
            selectedStep++;
        }
    }

    // Update selectedCellNote to reflect the note at the new position
    selectedCellNote = noteGrid[activeLayer][selectedBar][selectedStep];

    // Redraw old and new selection borders
    redrawCell_melodic(oldBar, oldStep, false);
    redrawCell_melodic(selectedBar, selectedStep, false);
    drawClearStepButton();   // update clear button for new step
}

FLASHMEM void drawLayer() {

    display.setCursor(CHAR_width_small * 10, TOP_TEXT_Y);
    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.printf("LAYER %d", activeLayer + 1);
}



FLASHMEM void drawVelocitySlider_melodic() {
    checkWebRemote();
    display.fillRect(VEL_TEXT_X - 5, VEL_SLIDER_Y - 3, VEL_SLIDER_W + 45, VEL_SLIDER_H + 6, COLOR_BACKGROUND);
    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setCursor(VEL_TEXT_X, VEL_SLIDER_Y + 1);
    display.print("VEL");
    checkWebRemote();
    display.fillRect(VEL_SLIDER_X, VEL_SLIDER_Y, VEL_SLIDER_W, VEL_SLIDER_H, GREY3);
    uint16_t fillWidth = (currentVelocity * VEL_SLIDER_W) / 127;
    if (fillWidth > 0) {
        uint16_t color = velocityToColor(currentVelocity);
        checkWebRemote();
        display.fillRect(VEL_SLIDER_X, VEL_SLIDER_Y, fillWidth, VEL_SLIDER_H, color);
    }
    uint16_t markerX = VEL_SLIDER_X + fillWidth;
    display.drawLine(markerX, VEL_SLIDER_Y - 2, markerX, VEL_SLIDER_Y + VEL_SLIDER_H + 2, COLOR_SYSTEXT);
    display.setCursor(VEL_VALUE_X, VEL_SLIDER_Y);
    display.printf("%3d", currentVelocity);
}

FLASHMEM void setVelocityFromTouch_melodic(int16_t x) {
    if (x < VEL_SLIDER_X) x = VEL_SLIDER_X;
    if (x > VEL_SLIDER_X + VEL_SLIDER_W) x = VEL_SLIDER_X + VEL_SLIDER_W;
    uint8_t newVel = ((x - VEL_SLIDER_X) * 127) / VEL_SLIDER_W;
    if (newVel < 1) newVel = 1;
    if (newVel != currentVelocity) {
        currentVelocity = newVel;
        drawVelocitySlider(currentVelocity);
    }
}

FLASHMEM void redrawGridAndButtons() {
  
    // Redraw all grid cells
    for (uint8_t bar = 0; bar < NUM_BARS; bar++) {
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            redrawCell_melodic(bar, step, false);
        }
    }

    // Redraw layer button and top layer text
    drawLayerButton(BTN_LAYER_X, BTN_Y, activeLayer);
    drawLayer();

    // Redraw clear step button (its state may have changed)
    drawClearStepButton();

    // Redraw copy button (to reflect that copy mode is off)
    drawCopyPasteButton(BTN_COPY_X, BTN_Y, copyModeActive);
}

FLASHMEM void loadGrid_melodic() {
    LiveSequencer::LiveSeqData* data = liveSeq.getData();
    uint8_t activeTrack = data->activeTrack;
    uint32_t patternLen = data->patternLengthMs;
    uint32_t stepMs = patternLen / STEPS_PER_BAR;

    memset(noteGrid, 0, sizeof(noteGrid));
    memset(velGrid, 0, sizeof(velGrid));

    for (const auto& ev : data->eventsList) {
        if (ev.track != activeTrack) continue;
        if (ev.event != midi::NoteOn) continue;
        uint8_t layer = ev.layer;
        if (layer >= NUM_LAYERS) continue;
        uint8_t bar = ev.patternNumber;
        if (bar >= NUM_BARS) continue;
        uint32_t timeMs = ev.patternMs;
        uint8_t step = timeMs / stepMs;
        if (step >= STEPS_PER_BAR) continue;
        noteGrid[layer][bar][step] = ev.note_in;
        velGrid[layer][bar][step] = ev.note_in_velocity;
    }
}

FLASHMEM void writeGrid_melodic() {
    LiveSequencer::LiveSeqData* data = liveSeq.getData();
    uint8_t activeTrack = data->activeTrack;
    uint32_t patternLen = data->patternLengthMs;
    uint32_t stepMs = patternLen / STEPS_PER_BAR;

    // Remove only events for this track AND this active layer
    for (auto& ev : data->eventsList) {
        if (ev.track == activeTrack && ev.layer == activeLayer) {
            ev.event = midi::InvalidType;
        }
    }

    // Add new events from our grid for the active layer only
    for (uint8_t bar = 0; bar < NUM_BARS; bar++) {
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            uint8_t note = noteGrid[activeLayer][bar][step];
            if (note == 0) continue;
            uint8_t vel = velGrid[activeLayer][bar][step];
            if (vel == 0) vel = currentVelocity;

            // uint32_t timeOn = bar * patternLen + step * stepMs;


            LiveSequencer::MidiEvent noteOn, noteOff;
            noteOn.source = LiveSequencer::EVENT_PATTERN;
            noteOn.patternNumber = bar;
            noteOn.patternMs = step * stepMs;
            noteOn.track = activeTrack;
            noteOn.layer = activeLayer;
            noteOn.event = midi::NoteOn;
            noteOn.note_in = note;
            noteOn.note_in_velocity = vel;

            noteOff = noteOn;
            noteOff.event = midi::NoteOff;
            if (step == STEPS_PER_BAR - 1) {
                noteOff.patternNumber = bar + 1;
                noteOff.patternMs = 0;
            }
            else {
                noteOff.patternNumber = bar;
                noteOff.patternMs = (step + 1) * stepMs;
            }
            noteOff.note_in_velocity = 0;

            data->pendingEvents.push_back(noteOn);
            data->pendingEvents.push_back(noteOff);
        }
    }

    liveSeq.flushPendingEvents();
    liveSeq.requestSortEvents();
    liveSeq.notifyLayerGridChanged();
    liveSeq.notifyLayerButtonsChanged();

    if (data->trackSettings[activeTrack].layerCount <= activeLayer) {
        data->trackSettings[activeTrack].layerCount = activeLayer + 1;
    }
}

FLASHMEM void redrawCell_melodic(uint8_t bar, uint8_t step, bool highlight) {
    uint16_t x = GRID_START_X + step * CELL_W;
    uint16_t y = GRID_START_Y + bar * CELL_H;
    uint16_t bgColor = ((step / 4) % 2 == 0) ? GREY3 : GREY2;

    checkWebRemote();
    display.fillRect(x, y, CELL_W - 1, CELL_H - 1, bgColor);

    uint8_t note = noteGrid[activeLayer][bar][step];
    if (note != 0) {
        uint8_t vel = velGrid[activeLayer][bar][step];
        if (vel == 0) vel = currentVelocity;
        uint16_t noteColor = velocityToColor(vel);
        checkWebRemote();
        display.fillRect(x, y, CELL_W - 1, CELL_H - 1, noteColor);
    }

    // Draw note name (short)
    std::string name = noteNumberToString(note);
    display.setTextSize(1);  //note color
    checkWebRemote();
    display.setTextColor(COLOR_BACKGROUND, (note != 0) ? velocityToColor(velGrid[activeLayer][bar][step]) : bgColor);
    uint8_t xOffset = (name.length() == 2) ? 4 : 1;
    display.setCursor(x + xOffset, y + CELL_H / 2 - 4);
    display.print(name.c_str());

    if (highlight) {
        checkWebRemote();
        display.drawRect(x, y, CELL_W - 1, CELL_H - 1, COLOR_SYSTEXT);
    }
    if (bar == selectedBar && step == selectedStep && !highlight) {
        checkWebRemote();
        display.drawRect(x, y, CELL_W - 1, CELL_H - 1, COLOR_SYSTEXT);
    }
}

FLASHMEM void setCellNote_melodic(uint8_t bar, uint8_t step, uint8_t note) {
    if (bar >= NUM_BARS || step >= STEPS_PER_BAR) return;
    noteGrid[activeLayer][bar][step] = note;
    velGrid[activeLayer][bar][step] = currentVelocity;
    redrawCell_melodic(bar, step, (bar == currentHighlightedBar && step == currentHighlightedStep));
    writeGrid_melodic();
}

static uint8_t copiedLayer = 0;

FLASHMEM void copyPattern_melodic() {
    copiedLayer = activeLayer;
    hasCopied = true;
    copyModeActive = true;
    drawCopyPasteButton(BTN_COPY_X, BTN_Y, copyModeActive);
}

FLASHMEM void pastePattern_melodic() {
    if (!hasCopied) return;
    memcpy(noteGrid[activeLayer], noteGrid[copiedLayer], sizeof(noteGrid[activeLayer]));
    memcpy(velGrid[activeLayer], velGrid[copiedLayer], sizeof(velGrid[activeLayer]));
    writeGrid_melodic();
    redrawGridAndButtons();   // was redraw_melodic()
    copyModeActive = false;
}

FLASHMEM void changeActiveLayer() {
    activeLayer = (activeLayer + 1) % NUM_LAYERS;
    loadGrid_melodic();
    redrawGridAndButtons();
}

FLASHMEM void drawStartStopButton_melodic() {
    LiveSequencer::LiveSeqData* data = liveSeq.getData();
    if (data->isRunning) {
        TouchButton::drawButton(BTN_START_STOP_X, BTN_Y, "STOP", "SEQ", TouchButton::BUTTON_RED);
    }
    else {
        TouchButton::drawButton(BTN_START_STOP_X, BTN_Y, "START", "SEQ", TouchButton::BUTTON_ACTIVE);
    }
}

FLASHMEM void drawLayerButton_melodic() {
    char layerNum[4];
    snprintf(layerNum, sizeof(layerNum), "%u", activeLayer + 1);
    TouchButton::drawButton(BTN_LAYER_X, BTN_Y, "LAYER", layerNum, TouchButton::BUTTON_HIGHLIGHTED);
}

FLASHMEM void drawCopyButton_melodic() {
    if (copyModeActive) {
        TouchButton::drawButton(BTN_COPY_X, BTN_Y, "PASTE", "PAT", TouchButton::BUTTON_ACTIVE);
    }
    else {
        TouchButton::drawButton(BTN_COPY_X, BTN_Y, "COPY", "PAT", TouchButton::BUTTON_ACTIVE);
    }
}

FLASHMEM void drawGridUI_melodic() {
    LiveSequencer::LiveSeqData* data = liveSeq.getData();
    // Clear grid area only, keep top bar and slider
    checkWebRemote();
    display.fillRect(0, GRID_START_Y - 2, DISPLAY_WIDTH, NUM_BARS * CELL_H + 4, COLOR_BACKGROUND);

    drawLayer();
    drawVelocitySlider(currentVelocity);

    // Draw all cells (no left labels)
    for (uint8_t bar = 0; bar < NUM_BARS; bar++) {
        for (uint8_t step = 0; step < STEPS_PER_BAR; step++) {
            redrawCell_melodic(bar, step, false);
        }
    }

    // Draw buttons
    drawStartStopButton(BTN_START_STOP_X, BTN_Y, data->isRunning);
    drawClearStepButton();
    drawLayerButton(BTN_LAYER_X, BTN_Y, activeLayer);
    drawCopyPasteButton(BTN_COPY_X, BTN_Y, copyModeActive);
    drawAutoAdvanceButton();

    TouchButton::drawButton(BTN_EXIT_X, BTN_Y, "EXIT", "", TouchButton::BUTTON_RED);

    // ---- Virtual keyboard ----
    setVirtualKeyboardInstrumentFromTrack();   // sets correct instrument and mode
    drawVirtualKeyboard(VK_DRAW_ALL & ~VK_DRAW_INSTRUMENT_BUTTONS);
}

FLASHMEM void redraw_melodic() {
    drawGridUI_melodic();
    LiveSequencer::LiveSeqData* data = liveSeq.getData();
    if (data->isRunning) {
        uint8_t bar = data->currentPattern;
        if (bar < NUM_BARS) {
            uint32_t stepMs = data->patternLengthMs / STEPS_PER_BAR;
            uint8_t step = data->patternTimer / stepMs;
            if (step < STEPS_PER_BAR) {
                if (bar != currentHighlightedBar || step != currentHighlightedStep) {
                    if (currentHighlightedBar != 0xFF && currentHighlightedStep != 0xFF) {
                        redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, false);
                    }
                    currentHighlightedBar = bar;
                    currentHighlightedStep = step;
                    redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, true);
                }
            }
        }
        else if (currentHighlightedBar != 0xFF) {
            redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, false);
            currentHighlightedBar = 0xFF;
            currentHighlightedStep = 0xFF;
        }
    }
    else if (currentHighlightedBar != 0xFF) {
        redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, false);
        currentHighlightedBar = 0xFF;
        currentHighlightedStep = 0xFF;
    }
}

// ----------------------------------------------------------------------
// Touch handling
// ----------------------------------------------------------------------

FLASHMEM void handleMainTouch_melodic(int16_t x, int16_t y) {
    // 1. Velocity slider
    if (y >= VEL_SLIDER_Y - 5 && y <= VEL_SLIDER_Y + VEL_SLIDER_H + 5 &&
        x >= VEL_SLIDER_X - 5 && x <= VEL_VALUE_X + 25) {
        setVelocityFromTouch(x, currentVelocity);
        return;
    }

    // 2. Buttons – use TouchButton::isPressed()
    else if (TouchButton::isPressed(BTN_START_STOP_X, BTN_Y)) {
        LiveSequencer::LiveSeqData* data = liveSeq.getData();
        if (data->isRunning) liveSeq.stop();
        else liveSeq.start();
        drawStartStopButton(BTN_START_STOP_X, BTN_Y, data->isRunning);
        if (!data->isRunning && currentHighlightedBar != 0xFF) {
            redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, false);
            currentHighlightedBar = 0xFF;
            currentHighlightedStep = 0xFF;
        }
        return;
    }

    else if (TouchButton::isPressed(BTN_LAYER_X, BTN_Y)) {
        changeActiveLayer();
        return;
    }

    else if (TouchButton::isPressed(BTN_COPY_X, BTN_Y)) {
        if (copyModeActive) pastePattern_melodic();
        else copyPattern_melodic();
        return;
    }

    else if (TouchButton::isPressed(BTN_EXIT_X, BTN_Y)) {
        LCDML.FUNC_goBackToMenu();
        return;
    }


    else if (TouchButton::isPressed(BTN_CLEAR_X, BTN_Y_ROW)) {
        bool hadNote = (noteGrid[activeLayer][selectedBar][selectedStep] != 0);
        if (hadNote) {
            noteGrid[activeLayer][selectedBar][selectedStep] = 0;
            velGrid[activeLayer][selectedBar][selectedStep] = 0;
            redrawCell_melodic(selectedBar, selectedStep, false);
            writeGrid_melodic();
        }
        if (autoAdvanceMode != 0) {
            advanceSelectedStep();
        }
        else {
            drawClearStepButton();  // just update button if no advance
        }
        return;
    }

    else if (TouchButton::isPressed(BTN_AUTO_X, BTN_Y_ROW)) {
        autoAdvanceMode = (autoAdvanceMode + 1) % 3;
        drawAutoAdvanceButton();
        return;
    }

    // 3. Grid cells (manual coordinate check – not a button)
    else if (x >= GRID_START_X && x < GRID_START_X + STEPS_PER_BAR * CELL_W &&
        y >= GRID_START_Y && y < GRID_START_Y + NUM_BARS * CELL_H) {
        uint8_t step = (x - GRID_START_X) / CELL_W;
        uint8_t bar = (y - GRID_START_Y) / CELL_H;
        if (bar < NUM_BARS && step < STEPS_PER_BAR) {
            // Check if touching the already selected cell
            if (bar == selectedBar && step == selectedStep) {
                // Clear the note
                if (noteGrid[activeLayer][bar][step] != 0) {
                    noteGrid[activeLayer][bar][step] = 0;
                    velGrid[activeLayer][bar][step] = 0;
                    selectedCellNote = 0;
                    redrawCell_melodic(bar, step, false);
                    writeGrid_melodic();
                    drawClearStepButton();  // update button state (now inactive)
                }
            }
            else {
                // Select the new cell
                uint8_t oldBar = selectedBar;
                uint8_t oldStep = selectedStep;
                selectedBar = bar;
                selectedStep = step;
                selectedCellNote = noteGrid[activeLayer][bar][step];
                // Redraw old and new cells to update selection borders
                redrawCell_melodic(oldBar, oldStep, false);
                redrawCell_melodic(selectedBar, selectedStep, false);
                drawClearStepButton();  // update button state for the new cell
            }
        }
        return;
    }
}

// ----------------------------------------------------------------------
// LCDML entry point
// ----------------------------------------------------------------------
FLASHMEM void UI_func_melodic_pattern_editor(uint8_t param) {
    if (LCDML.FUNC_setup()) {
        selectedBar = 0;
        selectedStep = 0;

        activeLayer = 0;
        selectedBar = 0;
        selectedStep = 0;
        currentVelocity = 100;
        hasCopied = false;
        copyModeActive = false;
        selectedCellNote = 0;
        memset(noteGrid, 0, sizeof(noteGrid));
        memset(velGrid, 0, sizeof(velGrid));
        currentHighlightedBar = 0xFF;
        currentHighlightedStep = 0xFF;

        loadGrid_melodic();
        redraw_melodic();
        drawTrackNumber();
        timeSinceOpened = 0;
        lastRunningState = liveSeq.getData()->isRunning;
        LCDML.FUNC_setLoopInterval(30);
    }

    if (LCDML.FUNC_loop()) {

        LiveSequencer::LiveSeqData* data = liveSeq.getData();


        if (timeSinceOpened >= 500) {
            static bool wasTouched = false;
            static uint8_t lastProcessedNote = 0;

            // Right encoder: turn to change note, press to next step
            const EncoderEvents e_right = getEncoderEvents(ENC_R);
            if (e_right.turned) {
                int newNote = selectedCellNote;
                if (newNote == 0) {
                    // Empty cell – turning up goes to C0
                    if (e_right.dir > 0) newNote = 12;
                }
                else {
                    if (e_right.dir > 0) {
                        if (newNote < 127) newNote++;
                    }
                    else {
                        if (newNote > 12) {
                            newNote--;
                        }
                        else if (newNote == 12) {
                            newNote = 0;    // below C0 becomes empty
                        }
                    }
                }
                if (newNote != selectedCellNote) {
                    selectedCellNote = newNote;
                    setCellNote_melodic(selectedBar, selectedStep, selectedCellNote);
                }
            }
            else if (e_right.pressed) {
                // Move to next step on short press
                uint8_t oldBar = selectedBar;
                uint8_t oldStep = selectedStep;
                selectedStep = (selectedStep + 1) % STEPS_PER_BAR;
                selectedCellNote = noteGrid[activeLayer][selectedBar][selectedStep];
                redrawCell_melodic(oldBar, oldStep, false);
                redrawCell_melodic(selectedBar, selectedStep, false);
                drawClearStepButton();
                // Do not process any other events this loop to avoid conflicts
                return;
            }

            // Encoder left: exit (short press)
            const EncoderEvents e_left = getEncoderEvents(ENC_L);
            if (e_left.pressed) {
                LCDML.FUNC_goBackToMenu();
                return;
            }

            // Right encoder short press: move to next step
            if (e_right.pressed) {
                uint8_t oldBar = selectedBar;
                uint8_t oldStep = selectedStep;
                selectedStep = (selectedStep + 1) % STEPS_PER_BAR;
                selectedCellNote = noteGrid[activeLayer][selectedBar][selectedStep];
                redrawCell_melodic(oldBar, oldStep, false);
                redrawCell_melodic(selectedBar, selectedStep, false);
                drawClearStepButton();
            }

            if (data->isRunning != lastRunningState) {
                lastRunningState = data->isRunning;
                drawStartStopButton(BTN_START_STOP_X, BTN_Y, data->isRunning);
            }

            // Update step highlight while running

            if (data->isRunning) {
                uint8_t bar = data->currentPattern;
                if (bar < NUM_BARS) {
                    uint32_t stepMs = data->patternLengthMs / STEPS_PER_BAR;
                    uint8_t step = data->patternTimer / stepMs;
                    if (step < STEPS_PER_BAR) {
                        if (bar != currentHighlightedBar || step != currentHighlightedStep) {
                            // Reset previous highlighted cell to its normal state
                            if (currentHighlightedBar != 0xFF && currentHighlightedStep != 0xFF) {
                                redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, false);
                            }
                            // Set new highlighted position
                            currentHighlightedBar = bar;
                            currentHighlightedStep = step;
                            // Draw the new cell in red (playing note highlight)
                            uint16_t x = GRID_START_X + step * CELL_W;
                            uint16_t y = GRID_START_Y + bar * CELL_H;
                            checkWebRemote();
                            display.fillRect(x, y, CELL_W - 1, CELL_H - 1, RED);
                            // Overwrite the note name (if any) in black
                            uint8_t note = noteGrid[activeLayer][bar][step];
                            if (note != 0) {
                                std::string name = noteNumberToString(note);
                                display.setTextColor(COLOR_SYSTEXT);
                                display.setCursor(x + (name.length() == 2 ? 4 : 1), y + CELL_H / 2 - 4);
                                display.print(name.c_str());
                            }
                        }
                    }
                }
                else if (currentHighlightedBar != 0xFF) {
                    // No valid bar – reset highlight
                    redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, false);
                    currentHighlightedBar = 0xFF;
                    currentHighlightedStep = 0xFF;
                }
            }
            else if (currentHighlightedBar != 0xFF) {
                // Sequencer stopped – clear highlight
                redrawCell_melodic(currentHighlightedBar, currentHighlightedStep, false);
                currentHighlightedBar = 0xFF;
                currentHighlightedStep = 0xFF;
            }

            // Touch handling
            wasTouched = false;
            lastProcessedNote = 0;
            if (numTouchPoints > 0) {
                int16_t x = ts.p.x;
                int16_t y = ts.p.y;
                if (!wasTouched) {
                    // One-time actions for grid, slider, bottom buttons
                    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) {
                        handleMainTouch_melodic(x, y);
                    }
                    wasTouched = true;
                }
                // Always process virtual keyboard (buttons and keys) while touched
                if (y >= VIRT_KEYB_TOP) {
                    handleTouchVirtualKeyboard();
                }
            }
            else {
                wasTouched = false;
                // Process releases even when touch ends
                handleTouchVirtualKeyboard();
            }

            // Capture note played from virtual keyboard and enter it into the grid
           // LiveSequencer::LiveSeqData* data = liveSeq.getData();
            uint8_t newNote = data->lastPlayedNote;
            if (newNote != 0 && newNote != lastProcessedNote) {
                if (selectedBar < NUM_BARS && selectedStep < STEPS_PER_BAR) {
                    selectedCellNote = newNote;
                    setCellNote_melodic(selectedBar, selectedStep, newNote);

                    // Auto advance if not off
                    if (autoAdvanceMode != 0) {
                        advanceSelectedStep();
                    }
                }
                lastProcessedNote = newNote;
                data->lastPlayedNote = 0;
                lastProcessedNote = 0;  // ready for next note
            }
        }
    }

    if (LCDML.FUNC_close()) {
        writeGrid_melodic();
        display.fillScreen(COLOR_BACKGROUND);
    }
}
