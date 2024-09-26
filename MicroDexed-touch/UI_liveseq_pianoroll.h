#ifndef UI_LIVESEQ_PIANOROLL_H
#define UI_LIVESEQ_PIANOROLL_H

#include <stdio.h>
#include "editableValue.h"
#include "touchbutton.h"
#include "valuebutton.h"
#include "livesequencer.h"
#include "touchbutton.h"
#include "config.h"
#include <set>

void UI_LiveSeq_Graphic(uint8_t param);


class UI_LiveSeq_PianoRoll {
public:
  UI_LiveSeq_PianoRoll(LiveSequencer &sequencer, LiveSequencer::LiveSeqData &d);
  void processLCDM(uint8_t param);
  void handleTouchScreen(void);
  void handleKeyChanged(uint8_t key, midi::MidiType event);

private:
  std::set<uint8_t> keysPressed;
  enum ViewMode {
    VIEW_ZOOM_X,
    VIEW_ZOOM_Y,
    VIEW_SCROLL_X,
    VIEW_SCROLL_Y,
    VIEW_NUM
  };
  uint8_t editMode = VIEW_ZOOM_X;

  struct ViewSettings {
    int8_t startOctave;
    int8_t numOctaves;
    float noteHeight;   // populated automatically
    uint8_t numNotes;   // populated automatically
    int8_t startPattern;
    int8_t numPatterns;
    float msToPix;      // populated automatically
    uint8_t lowestNote; // populated automatically
    uint8_t highestNote;// populated automatically
    uint16_t lastCursorPos;
  } view;

  static constexpr uint16_t HEADER_BG_COLOR = GREY3;

  enum DrawFlags {
    DRAW_PIANOROLL = (1 << 0),
    DRAW_VERTICAL_BARS = (1 << 1),
    DRAW_BACKGROUND = (1 << 2),
    DRAW_NOTES = (1 << 3),
    DRAW_CURSOR = (1 << 4),
    DRAW_ALL = 0xFFFF
  };
  uint16_t drawFlags = 0;

  void initGUI(void);
  void drawPianoRoll(void);
  void handleEncoderRight(int8_t diff);
  bool hasConstrainedChanged(int8_t &value, int8_t diff, int8_t min, int8_t max);
  void drawCursor(void);
  void drawVerticalBars(void);
  void drawNote(LiveSequencer::NotePair note);
  void setNumOctaves(int8_t num);
  void setStartOctave(int8_t num);
  void setNumPatterns(int8_t num);
  void setPatternStart(int8_t num);
  void drawNotes(void);
  void drawGUI(void);
  void drawBackground(void);


  uint32_t midiEventTime(LiveSequencer::MidiEvent e) const;
  LiveSequencer &liveSeq;
  LiveSequencer::LiveSeqData &data;
  bool isRunningHere;
  bool isVisible = false;

  ActiveValue currentValue = { nullptr, nullptr };
  uint8_t currentTrack = 0;
  std::vector<uint16_t> layerColors;
  std::vector<TouchButton*> buttons;

  static constexpr uint16_t ROLL_WIDTH = 20;
  static constexpr uint16_t CONTENT_HEIGHT = 180; // divides nicely by 12
  static constexpr uint16_t CONTENT_WIDTH = DISPLAY_WIDTH - ROLL_WIDTH;
  static constexpr uint16_t HEADER_HEIGHT = DISPLAY_HEIGHT - CONTENT_HEIGHT - TouchButton::BUTTON_SIZE_Y;
    
};

#endif // UI_LIVESEQ_PIANOROLL_H