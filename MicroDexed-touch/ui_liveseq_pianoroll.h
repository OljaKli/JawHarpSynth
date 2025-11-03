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

static constexpr uint8_t SONG_MODE_NOTES_TRACK = 12;
static constexpr uint8_t SONG_MODE_MUTE_TRACK = 13;
static constexpr uint8_t MAX_TRACKS = 14; // 12 patterns + 2 song mode tracks

void UI_func_liveseq_graphic(uint8_t param);

class UI_LiveSeq_PianoRoll {
public:

  uint32_t getCurrentPlaybackTime() const;
  UI_LiveSeq_PianoRoll(LiveSequencer& sequencer, LiveSequencer::LiveSeqData& d);
  void processLCDM(uint8_t param);
  void handleTouchScreen(void);
  void handleKeyChanged(uint8_t key, midi::MidiType event, uint8_t velocity);
  FLASHMEM void onEventsChanged(void);
  FLASHMEM void drawBarNumbers(void);
  FLASHMEM uint16_t getSongPatternForNote(const LiveSequencer::NotePair& note) const;
  uint16_t stepCursorPositionPrev = ROLL_WIDTH;
  FLASHMEM void removeNoteFromSongEvents(LiveSequencer::MidiEvent& noteOn, LiveSequencer::MidiEvent& noteOff);
  FLASHMEM uint16_t findSongPatternForEvent(const LiveSequencer::MidiEvent& e);
  FLASHMEM void moveEventToSongPattern(LiveSequencer::MidiEvent& e, uint16_t targetSongPattern);
  FLASHMEM void removeEventFromSpecificPattern(LiveSequencer::MidiEvent& event, uint16_t patternIndex);
  FLASHMEM void removeEventByContent(const LiveSequencer::MidiEvent& event);
  FLASHMEM uint16_t findSongPatternByEvent(const LiveSequencer::MidiEvent& e);

private:
  uint32_t getPatternRelativeTime(const LiveSequencer::MidiEvent& e) const;
  void setPatternRelativeTime(LiveSequencer::MidiEvent& e, uint32_t time, uint16_t songPatternIndex = 0);
  uint16_t getSongPatternForEvent(const LiveSequencer::MidiEvent& e) const;
  void drawBarInfoOnTop(void);
  uint32_t getAbsoluteEventTime(const LiveSequencer::MidiEvent& e) const;
  uint16_t getMaxBarsForCurrentMode() const;
  uint16_t getActualBarForNote(const LiveSequencer::NotePair& note) const;
  void drawNoteAtPosition(LiveSequencer::NotePair note, bool isSelected, uint16_t x, uint16_t w, bool drawInGrey = false) const;
  void updateSongEvent(LiveSequencer::MidiEvent& updatedEvent);

  bool step(int8_t diff);
  std::set<uint8_t> keysPressed;

  enum ViewMode {
    VIEW_ZOOM_X,
    VIEW_SCROLL_X,
    VIEW_ZOOM_Y,
    VIEW_SCROLL_Y,
    VIEW_NUM
  };
  uint8_t viewMode = VIEW_ZOOM_X;

  enum EditMode {
    EDIT_LAYER,
    EDIT_SELECT_NOTE,
    EDIT_NOTEON,
    EDIT_NOTEOFF,
    EDIT_VELOCITY,
    EDIT_NOTE,
    EDIT_NUM
  };

  uint8_t editMode = EDIT_LAYER;

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
    DRAW_CONTENT_FULL = 1 << 7, // force full redraw
    DRAW_MUTE_AUTOMATION = (1 << 8)
  };
  uint16_t drawFlags = 0;

   enum QuantizeLevel {
        QUANTIZE_OFF,
        QUANTIZE_64,
        QUANTIZE_32, 
        QUANTIZE_16,
        QUANTIZE_8,
        QUANTIZE_4,    // 1/4 note
        QUANTIZE_NUM   // Remove QUANTIZE_1
    };
    uint8_t quantizeLevel = QUANTIZE_16; // Default to 1/16
     FLASHMEM uint32_t getQuantizationGridStep() const;
    
    // Add this method declaration
    FLASHMEM void quantizeSelectedNote(void);

TouchButton* buttonQuant = nullptr; // Add this

  struct UpdateRange {
    uint16_t fromX;
    uint16_t toX;
    std::set<uint8_t> notes; // empty = all
  };
  std::vector<UpdateRange> drawRegionsX;

  LiveSequencer::MidiEvent currentNote;
  int16_t selectedNote = -1;
  int16_t selectedMuteEvent = -1;
  std::vector<LiveSequencer::MidiEvent> muteAutomationEvents;

  void initGUI(void);
  void drawPianoRoll(void);
  void handleEncoderRight(EncoderEvents e);
  template <class T> bool hasConstrainedChanged(T& value, int16_t diff, T min, T max);

  void queueDrawNote(LiveSequencer::NotePair note);
  void queueDrawNote(UpdateRange coords);
  uint16_t getNoteCoord(uint32_t time) const;
  uint32_t getRelativeNoteTime(uint32_t time) const;
  void deleteSelectedNote(void);
  void deleteSelectedMuteEvent(void);
  void updateMuteEventInSequencer(LiveSequencer::MidiEvent& updatedEvent);
  void removeMuteEventFromSequencer(LiveSequencer::MidiEvent& eventToRemove);

  void drawHeader(uint16_t modeFlags = 0);
  void drawHeaderSetting(uint16_t x, const char* topText, const char* bottomText, bool topHighlighted, bool bottomHighlighted);
  bool isSettingActivated = false;
  void toggleSettingActivated(void);

  void drawCursor(void);
  void drawStepCursor(void);
  void drawVerticalBars(void);
  void drawNote(LiveSequencer::NotePair note, bool isSelected = false, bool drawInGrey = false) const;

  void setNumOctaves(int8_t num);
  void setStartOctave(int8_t num);
  void setNumBars(int8_t num);
  void setBarStart(int8_t num);
  void drawNotes(void);
  void drawGUI(void);
  void drawBackground(void);
  void drawAreaIndicator(void);
  void drawLayersIndicator(void);
  void drawMuteAutomation(void);
  bool setMode(uint8_t newMode);
  void setIsHighlighted(bool isActiveMode);

  std::string getNoteString(uint8_t note);

  void reloadNotes(bool autoAdaptView = false);
  const uint32_t getEventTime(LiveSequencer::MidiEvent e) const;
  void setEventTime(LiveSequencer::MidiEvent& e, uint32_t time);

  // New methods for song mode support
  std::vector<LiveSequencer::NotePair> getNotePairsFromSongTrack(uint8_t track, uint8_t& lowestNote, uint8_t& highestNote, uint8_t patternFrom, uint8_t patternTo);
  std::vector<LiveSequencer::MidiEvent> getMuteAutomationEvents(uint8_t track, uint8_t patternFrom, uint8_t patternTo);
  void drawSongModeIndicator(void);

  // New helper method for track button
  uint8_t getMaxTrackForCurrentMode() const;

  LiveSequencer& liveSeq;
  LiveSequencer::LiveSeqData& data;
  bool isRunningHere;
  int8_t selectedLayer; // The currently selected layer for editing
  bool isVisible = false;
  bool openendFromLiveSequencer = false;

  std::vector<LiveSequencer::NotePair> notePairs;

  ActiveValue currentValue = { nullptr, nullptr };
  std::vector<uint16_t> layerColors;
  std::vector<TouchButton*> buttons;

  EditableValueVector<uint8_t>* recordSteps;

  TouchButton* buttonPlay;
  TouchButton* buttonInst;
  TouchButton* buttonView;
  TouchButton* buttonEdit;
  TouchButton* buttonStep;

  static constexpr uint8_t TOTAL_OCTAVES = 8;
  static constexpr uint16_t HEADER_DRAW_ALL = 0xFFFF;
  static constexpr uint16_t HEADER_BG_COLOR = COLOR_BACKGROUND;
  static constexpr uint16_t CURSOR_BG_COLOR = HEADER_BG_COLOR; // for now..
  static constexpr uint16_t ROLL_WIDTH = 20;
  static constexpr uint16_t CONTENT_HEIGHT = 180; // divides nicely by 12
  static constexpr uint16_t CONTENT_WIDTH = DISPLAY_WIDTH - ROLL_WIDTH;
  static constexpr uint16_t LINE_HEIGHT = 1;
  static constexpr uint16_t HEADER_HEIGHT = DISPLAY_HEIGHT - CONTENT_HEIGHT - TouchButton::BUTTON_SIZE_Y - LINE_HEIGHT;

  static constexpr uint8_t STEPS_MAX = 64;
  int8_t stepRecordLayer = 0;
  uint8_t stepRecordSteps = 16;
  float stepRecordStepSizeMs = 0.0f;
  uint16_t stepCursorPositionIndex = 0;
  uint16_t stepCursorPosition = ROLL_WIDTH;
};

#endif // UI_LIVESEQ_PIANOROLL_H
