#ifndef UI_GRID_SONG_EDITOR_H
#define UI_GRID_SONG_EDITOR_H

#include <functional>
#include <vector>
#include "touchbutton.h"
#include "livesequencer.h"

class ILI9341_t3n;
struct EncoderEvents;

class GridSongEditorUI {
public:
  enum class ControlBank : uint8_t {
    Primary = 0,
    Secondary
  };

  struct Callbacks {
    std::function<void()> onExitRequested;
  };

  GridSongEditorUI(LiveSequencer& sequencer, LiveSequencer::LiveSeqData& data, ILI9341_t3n& display);
  void setCallbacks(const Callbacks& cb);

  void enter();
  void exit();
  bool isActive() const;

  void tick();
  void handleEncoder(const EncoderEvents& events);
  void handleTouch();
  void requestRedraw();
  void handleStepChanged();
  void handlePlaybackEnded();
  void exitToLiveSequencer();

private:
  void initButtons();
  void draw();
  void processPlayButton();
  void processGridTouch();
  void processControlButtons();
  void toggleControlBank();
  void setControlBank(ControlBank bank);
  void handleSecondaryButton(uint8_t index);
  void drawSecondaryControlButton(TouchButton* button, uint8_t index);
  void drawSecondaryBottomButton(uint16_t x, uint16_t y, uint8_t index);
  bool isSecondaryFunctionEnabled(uint8_t index) const;
  bool canCopyBlock() const;
  bool canClearBlock() const;
  bool canClearRow() const;
  bool canCopyFromPattern() const;
  bool playbackBlockIsValid() const;
  bool blockHasContent() const;
  bool rowHasContent(uint8_t step) const;
  bool destinationRangeIsEmpty(uint8_t destStart, uint8_t blockLength) const;
  void copySelectedBlock();
  void clearSelectedBlock();
  void clearSelectedRow();
  void copyRowFromPattern();
  void armClearBlockConfirmation();
  void cancelClearBlockConfirmation();
  void updateClearBlockConfirmation();
  void armClearRowConfirmation();
  void cancelClearRowConfirmation();
  void updateClearRowConfirmation();
  void armFromPatternConfirmation();
  void cancelFromPatternConfirmation();
  void updateFromPatternConfirmation();
  void toggleLayer(uint8_t layer);
  void clearGridCell();
  void fillGridCell();
  void copyFromAbove();
  void insertGridRow();
  void removeGridRow();
  void slowRect(uint16_t x, uint8_t y, uint16_t color);
  void checkWebRemote();

  LiveSequencer& liveSeq;
  LiveSequencer::LiveSeqData& data;
  ILI9341_t3n& display;
  Callbacks callbacks{};

  bool active = false;
  bool needsRedraw = true;
  ControlBank controlBank = ControlBank::Primary;
  bool clearBlockConfirmPending = false;
  uint32_t clearBlockConfirmDeadlineMs = 0;
  bool clearRowConfirmPending = false;
  uint32_t clearRowConfirmDeadlineMs = 0;
  bool fromPatternConfirmPending = false;
  uint32_t fromPatternConfirmDeadlineMs = 0;

  static constexpr uint8_t GRID_SONG_COLS = 12;
  static constexpr uint8_t GRID_SONG_ROWS = 6;
  static constexpr uint8_t GRID_SONG_CELL_WIDTH = (320 - (GRID_SONG_COLS - 1)) / GRID_SONG_COLS;
  static constexpr uint8_t GRID_SONG_CELL_HEIGHT = (154 - (GRID_SONG_ROWS - 1)) / GRID_SONG_ROWS;
  static constexpr uint8_t gridStartX = 19;
  static constexpr uint8_t gridStartY = 12;
  static constexpr uint32_t CONFIRM_TIMEOUT_MS = 2000;

  std::vector<TouchButton*> gridControlButtons;
};

#endif // UI_GRID_SONG_EDITOR_H
