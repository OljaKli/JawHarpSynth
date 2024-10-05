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
  void handleKeyChanged(uint8_t key, midi::MidiType event, uint8_t velocity);

private:
  std::set<uint8_t> keysPressed;
  enum ViewMode {
    VIEW_ZOOM_Y,
    VIEW_SCROLL_Y,
    VIEW_ZOOM_X,
    VIEW_SCROLL_X,
    VIEW_NUM
  };
  uint8_t viewMode = VIEW_ZOOM_Y;

  enum EditMode {
    EDIT_SELECT_NOTE,
    EDIT_NOTEON,
    EDIT_VELOCITY,
    EDIT_DURATION,
    EDIT_NOTE,
    EDIT_DELETE,
    EDIT_NUM
  };
  uint8_t editMode = EDIT_SELECT_NOTE;

  enum StepMode {
    STEP_RECORD,
    STEP_SIZE,
    STEP_LAYER,
    STEP_NUM
  };
  uint8_t stepMode = STEP_RECORD;

  enum Mode {
    MODE_VIEW,
    MODE_EDIT,
    MODE_STEP
  };
  uint8_t mode = MODE_VIEW;

  struct ViewSettings {
    int8_t startOctave;
    int8_t numOctaves;
    float noteHeight;   // populated automatically
    uint8_t numNotes;   // populated automatically
    int8_t startBar;
    int8_t numBars;
    float msToPix;      // populated automatically
    uint8_t lowestNote; // populated automatically
    uint8_t highestNote;// populated automatically
    uint16_t cursorPos;
    uint16_t cursorPosPrev;
    uint16_t cursorColor;
  } view;

  enum DrawFlags {
    DRAW_PIANOROLL = (1 << 0),
    DRAW_VERTICAL_BARS = (1 << 1),
    DRAW_BACKGROUND = (1 << 2),
    DRAW_NOTES = (1 << 3),
    DRAW_CURSOR = (1 << 4),
    DRAW_SINGLE_NOTE = (1 << 5),
    DRAW_STEP_CURSOR = (1 << 6),
    DRAW_CONTENT_AREA = DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES,
    DRAW_CONTENT_FULL = 1 << 7 // force full redraw
  };
  uint16_t drawFlags = 0;
  std::set<uint8_t> singleNoteDraw;

  struct SubUpdateX {
    uint16_t fromX;
    uint16_t toX;
  };
  std::vector<SubUpdateX> drawRegionsX;
  
  uint16_t GRID_X[LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN];

  void initGUI(void);
  void drawPianoRoll(void);
  void handleEncoderRight(int8_t diff);
  bool hasConstrainedChanged(int8_t &value, int8_t diff, int8_t min, int8_t max);
  bool hasConstrainedChanged(uint16_t &value, int16_t diff, uint16_t min, uint16_t max);

  void printActionName(void);
  void printActionValue(void);

  void drawCursor(void);
  void drawStepCursor(void);
  void drawVerticalBars(void);
  void drawNote(LiveSequencer::NotePair note, bool isSelected = false);
  void setNumOctaves(int8_t num);
  void setStartOctave(int8_t num);
  void setNumBars(int8_t num);
  void setBarStart(int8_t num);
  void drawNotes(void);
  void drawGUI(void);
  void drawBackground(void);
  void drawAreaIndicator(void);
  void drawLayersIndicator(void);
  bool setMode(uint8_t newMode);

  std::string getNoteString(uint8_t note);

  void reloadNotes(uint8_t patternFrom = 0, uint8_t patternTo = 0xFF);
  int8_t selectedNote = -1; // index of notePairs or -1

  static std::string getViewModeString(uint8_t subMode);
  static std::string getEditModeString(uint8_t subMode);
  static std::string getStepModeString(uint8_t subMode);

  bool step(int8_t diff);

  uint32_t midiEventTime(LiveSequencer::MidiEvent e) const;
  LiveSequencer &liveSeq;
  LiveSequencer::LiveSeqData &data;
  bool isRunningHere;
  bool isVisible = false;

  std::vector<LiveSequencer::NotePair> notePairs;

  ActiveValue currentValue = { nullptr, nullptr };
  std::vector<uint16_t> layerColors;
  std::vector<TouchButton*> buttons;

  TouchButton *buttonPlay;
  TouchButton *buttonInst;
  TouchButton *buttonView;
  TouchButton *buttonEdit;
  TouchButton *buttonStep;

  static constexpr uint16_t HEADER_BG_COLOR = COLOR_BACKGROUND;
  static constexpr uint16_t CURSOR_BG_COLOR = HEADER_BG_COLOR; // for now..
  static constexpr uint16_t ROLL_WIDTH = 20;
  static constexpr uint16_t CONTENT_HEIGHT = 180; // divides nicely by 12
  static constexpr uint16_t CONTENT_WIDTH = DISPLAY_WIDTH - ROLL_WIDTH;
  static constexpr uint16_t LINE_HEIGHT = 1;
  static constexpr uint16_t HEADER_HEIGHT = DISPLAY_HEIGHT - CONTENT_HEIGHT - TouchButton::BUTTON_SIZE_Y - LINE_HEIGHT;

  static constexpr uint8_t STEPS_MAX = 32;
  int8_t stepRecordLayer = 0;
  int8_t stepRecordSteps = 16;
  float stepRecordStepSizeMs = 0.0f;
  int8_t stepCursorPositionIndex = 0;
  uint16_t stepCursorPosition = ROLL_WIDTH;
};

#endif // UI_LIVESEQ_PIANOROLL_H