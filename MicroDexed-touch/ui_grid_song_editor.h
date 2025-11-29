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
  void processBackButton();
  void processPlayButton();
  void processGridTouch();
  void processControlButtons();
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

  static constexpr uint8_t GRID_SONG_COLS = 12;
  static constexpr uint8_t GRID_SONG_ROWS = 6;
  static constexpr uint8_t GRID_SONG_CELL_WIDTH = (320 - (GRID_SONG_COLS - 1)) / GRID_SONG_COLS;
  static constexpr uint8_t GRID_SONG_CELL_HEIGHT = (154 - (GRID_SONG_ROWS - 1)) / GRID_SONG_ROWS;
  static constexpr uint8_t gridStartX = 19;
  static constexpr uint8_t gridStartY = 12;

  std::vector<TouchButton*> gridControlButtons;
};

#endif // UI_GRID_SONG_EDITOR_H
