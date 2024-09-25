#ifndef UI_LIVESEQ_PIANOROLL_H
#define UI_LIVESEQ_PIANOROLL_H

#include <stdio.h>
#include "editableValue.h"
#include "touchbutton.h"
#include "valuebutton.h"
#include "livesequencer.h"
#include "touchbutton.h"
#include "config.h"

void UI_LiveSeq_Graphic(uint8_t param);


class UI_LiveSeq_PianoRoll {
public:
    UI_LiveSeq_PianoRoll(LiveSequencer &sequencer, LiveSequencer::LiveSeqData &d);
    void processLCDM(uint8_t param);
    void handleTouchScreen(void);

private:
    enum ViewMode {
        VIEW_ZOOM_X,
        VIEW_ZOOM_Y,
        VIEW_SCROLL_X,
        VIEW_SCROLL_Y,
        VIEW_NUM
    };
    uint8_t editMode = VIEW_ZOOM_X;

    struct ViewSettings {
        int8_t startOctave = 3;
        int8_t numOctaves = 3;
        int8_t startPattern = 0;
        int8_t numPatterns = 4;
    } viewSettings;

    void initGUI(void);
    void drawPianoRoll(void);
    void handleEncoderRight(int8_t diff);
    bool hasConstrainedChanged(int8_t &value, int8_t diff, int8_t min, int8_t max);
    void drawCursor(void);
    void drawContentArea(void);
    void drawNote(LiveSequencer::NotePair note);
    uint32_t midiEventTime(LiveSequencer::MidiEvent e);
    LiveSequencer &liveSeq;
    LiveSequencer::LiveSeqData &data;
    bool isRunningHere;

    uint8_t currentTrack = 0;
    std::vector<uint16_t> layerColors;
    std::vector<TouchButton*> buttons;

    static constexpr uint16_t ROLL_WIDTH = 20;
    static constexpr uint16_t CONTENT_HEIGHT = 180; // divides nicely by 12
    static constexpr uint16_t CONTENT_WIDTH = DISPLAY_WIDTH - ROLL_WIDTH; // divides nicely by 12
    static constexpr uint16_t HEADER_HEIGHT = DISPLAY_HEIGHT - CONTENT_HEIGHT - TouchButton::BUTTON_SIZE_Y;
    
};

#endif // UI_LIVESEQ_PIANOROLL_H