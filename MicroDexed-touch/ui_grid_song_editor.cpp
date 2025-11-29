#include "ui_grid_song_editor.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "ILI9341_t3n.h"
#include "touch.h"
#include "sequencer.h"

extern ts_t ts;
extern int numTouchPoints;
extern bool remote_active;
extern sequencer_t seq;

GridSongEditorUI::GridSongEditorUI(LiveSequencer& sequencer, LiveSequencer::LiveSeqData& d, ILI9341_t3n& disp)
  : liveSeq(sequencer), data(d), display(disp) {
  initButtons();
}

void GridSongEditorUI::setCallbacks(const Callbacks& cb) {
  callbacks = cb;
}

void GridSongEditorUI::enter() {
  if (active) {
    return;
  }
  active = true;
  needsRedraw = true;
  data.redraw_grid_header = true;
  data.redraw_grid_buttons = true;
  display.fillScreen(COLOR_BACKGROUND);
}

void GridSongEditorUI::exit() {
  if (!active) {
    return;
  }
  active = false;
  needsRedraw = true;
  data.redraw_grid_header = true;
  data.redraw_grid_buttons = true;
}

bool GridSongEditorUI::isActive() const {
  return active;
}

void GridSongEditorUI::tick() {
  if (active && needsRedraw) {
    draw();
  }
}

void GridSongEditorUI::handleEncoder(const EncoderEvents& events) {
  if (!active) {
    return;
  }

  if (events.down) {
    if (data.gridStepOffset < data.GRIDSONGLEN - GRID_SONG_ROWS) {
      ++data.gridStepOffset;
      needsRedraw = true;
    }
  }
  else if (events.up) {
    if (data.gridStepOffset > 0) {
      --data.gridStepOffset;
      needsRedraw = true;
    }
  }
}

void GridSongEditorUI::handleTouch() {
  if (!active) {
    return;
  }

  for (uint8_t layer = 0; layer < 4; layer++) {
    if (TouchButton::isPressed(GRID.X[layer + 1], GRID.Y[5])) {
      toggleLayer(layer);
      return;
    }
  }

  if (TouchButton::isPressed(GRID.X[0], GRID.Y[5])) {
    processBackButton();
    return;
  }

  if (TouchButton::isPressed(GRID.X[5], GRID.Y[5])) {
    processPlayButton();
    return;
  }

  processControlButtons();
  processGridTouch();
}

void GridSongEditorUI::requestRedraw() {
  needsRedraw = true;
}

void GridSongEditorUI::handleStepChanged() {
  if (active) {
    needsRedraw = true;
  }
}

void GridSongEditorUI::handlePlaybackEnded() {
  const bool playingNow = data.isRunning || data.isStarting || seq.running;
  if (playingNow) {
    data.seq_started_from_pattern_mode = false;
    liveSeq.stop();
  }
  needsRedraw = true;
  data.redraw_grid_buttons = true;
  data.redraw_grid_header = true;
}

void GridSongEditorUI::initButtons() {
  gridControlButtons.reserve(6);
  gridControlButtons.push_back(new TouchButton(GRID.X[0], GRID.Y[4],
    [this](auto* b) {
      const uint8_t layerLimit = std::min<uint8_t>(data.trackSettings[data.selectedGridTrack].layerCount, LiveSequencer::LIVESEQUENCER_NUM_LAYERS);
      const bool hasLayers = layerLimit > 0;

      bool hasContent = false;
      for (uint8_t layer = 0; layer < layerLimit; ++layer) {
        if (data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer)) {
          hasContent = true;
          break;
        }
      }

      const char* topLabel = hasContent ? "CLEAR" : "FILL";
      TouchButton::Color color = hasContent ? TouchButton::BUTTON_RED : TouchButton::BUTTON_ACTIVE;
      if (!hasLayers) {
        topLabel = "FILL";
        color = TouchButton::BUTTON_INACTIVE;
      }

      b->draw(topLabel, "CELL", color);
    },
    [this](auto*) {
      const uint8_t layerLimit = std::min<uint8_t>(data.trackSettings[data.selectedGridTrack].layerCount, LiveSequencer::LIVESEQUENCER_NUM_LAYERS);
      if (layerLimit == 0) {
        return;
      }

      bool hasContent = false;
      for (uint8_t layer = 0; layer < layerLimit; ++layer) {
        if (data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer)) {
          hasContent = true;
          break;
        }
      }

      if (hasContent) {
        clearGridCell();
      }
      else {
        fillGridCell();
      }
    }));

  gridControlButtons.push_back(new TouchButton(GRID.X[1], GRID.Y[4],
    [this](auto* b) {
      const bool hasLayers = data.trackSettings[data.selectedGridTrack].layerCount > 0;
      if (hasLayers && data.selectedGridStep > 0) {
        b->draw("FROM", "ABOVE", TouchButton::BUTTON_ACTIVE);
      }
      else {
        b->draw("FROM", "ABOVE", TouchButton::BUTTON_INACTIVE);
      }
    },
    [this](auto*) {
      if (data.trackSettings[data.selectedGridTrack].layerCount > 0 && data.selectedGridStep > 0) {
        copyFromAbove();
      }
    }));

  gridControlButtons.push_back(new TouchButton(GRID.X[2], GRID.Y[4],
    [this](auto* b) {
      const bool enabled = liveSeq.canInsertGridRow(data.selectedGridStep);
      b->draw("INSERT", "ROW", enabled ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_INACTIVE);
    },
    [this](auto*) {
      insertGridRow();
    }));

  gridControlButtons.push_back(new TouchButton(GRID.X[3], GRID.Y[4],
    [this](auto* b) {
      const bool enabled = liveSeq.canRemoveGridRow(data.selectedGridStep);
      b->draw("REMOVE", "ROW", enabled ? TouchButton::BUTTON_RED : TouchButton::BUTTON_INACTIVE);
    },
    [this](auto*) {
      removeGridRow();
    }));

  gridControlButtons.push_back(new TouchButton(GRID.X[4], GRID.Y[4],
    [this](auto* b) {
      char startLabel[5];
      snprintf(startLabel, sizeof(startLabel), "%02u", static_cast<unsigned>(data.gridPlaybackStartStep) + 1);
      b->draw("START", startLabel, TouchButton::BUTTON_ACTIVE);
    },
    [this](auto*) {
      if (data.selectedGridStep < LiveSequencer::LiveSeqData::GRIDSONGLEN) {
        data.gridPlaybackStartStep = data.selectedGridStep;
        if (data.gridPlaybackStartStep > data.gridPlaybackEndStep) {
          data.gridPlaybackEndStep = data.gridPlaybackStartStep;
        }
        data.redraw_grid_buttons = true;
        needsRedraw = true;
      }
    }));

  gridControlButtons.push_back(new TouchButton(GRID.X[5], GRID.Y[4],
    [this](auto* b) {
      char endLabel[5];
      snprintf(endLabel, sizeof(endLabel), "%02u", static_cast<unsigned>(data.gridPlaybackEndStep) + 1);
      const char* topLabel = data.gridLoopEnabled ? "LOOP" : "END";
      const bool emphasize = data.gridLoopEnabled;
      b->draw(topLabel, endLabel, emphasize ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
    },
    [this](auto*) {
      if (data.selectedGridStep < LiveSequencer::LiveSeqData::GRIDSONGLEN) {
        if (data.gridPlaybackEndStep != data.selectedGridStep) {
          data.gridPlaybackEndStep = data.selectedGridStep;
          if (data.gridPlaybackEndStep < data.gridPlaybackStartStep) {
            data.gridPlaybackStartStep = data.gridPlaybackEndStep;
          }
        }
        else {
          data.gridLoopEnabled = !data.gridLoopEnabled;
        }
        data.redraw_grid_buttons = true;
        needsRedraw = true;
      }
    }));
}

void GridSongEditorUI::draw() {
  if (!active || !needsRedraw) {
    return;
  }

  if (!seq.running) {
    data.seq_started_from_pattern_mode = false;
  }

  if (data.redraw_grid_header) {
    display.fillScreen(COLOR_BACKGROUND);
  }

  if (data.redraw_grid_header) {
    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT);
    for (uint8_t track = 0; track < GRID_SONG_COLS; track++) {
      const uint16_t x = 16 + (track * (GRID_SONG_CELL_WIDTH));
      display.setCursor(x + (GRID_SONG_CELL_WIDTH / 2) - 3, gridStartY - 15);
      display.printf("T%i", track + 1);
    }
    data.redraw_grid_header = false;
  }

  checkWebRemote();
  display.drawRect(gridStartX, gridStartY, GRID_SONG_CELL_WIDTH * GRID_SONG_COLS + 1, GRID_SONG_CELL_HEIGHT * GRID_SONG_ROWS + 1, GREY3);

  for (uint8_t track = 1; track < GRID_SONG_COLS; track++) {
    const uint16_t x = gridStartX + (track * GRID_SONG_CELL_WIDTH);
    checkWebRemote();
    display.drawLine(x, gridStartY, x, gridStartY + (GRID_SONG_CELL_HEIGHT * GRID_SONG_ROWS), GREY3);
  }

  for (uint8_t step = 1; step < GRID_SONG_ROWS; step++) {
    const uint16_t y = gridStartY + (step * GRID_SONG_CELL_HEIGHT);
    checkWebRemote();
    display.drawLine(gridStartX, y, (GRID_SONG_CELL_WIDTH * (GRID_SONG_COLS + 1)), y, GREY3);
  }

  for (uint8_t step = 0; step < GRID_SONG_ROWS; step++) {
    const uint8_t actualStep = data.gridStepOffset + step;
    if (actualStep >= data.GRIDSONGLEN) {
      break;
    }

    const bool isCurrentStep = (actualStep == data.currentGridStep) && data.isRunning;
    const bool isSelectedStep = (actualStep == data.selectedGridStep);
    const bool isEndStep = (actualStep == data.gridPlaybackEndStep);

    display.setCursor(1, gridStartY + (step * (GRID_SONG_CELL_HEIGHT)) + GRID_SONG_CELL_HEIGHT / 2 - 4);
    uint16_t stepColor = COLOR_SYSTEXT;
    if (isCurrentStep) {
      stepColor = RED;
    }
    else if (isEndStep) {
      stepColor = YELLOW;
    }
    else if (actualStep == data.gridPlaybackStartStep) {
      stepColor = GREEN;
    }
    display.setTextColor(stepColor, COLOR_BACKGROUND);
    display.printf("%02i", actualStep + 1);

    for (uint8_t track = 0; track < GRID_SONG_COLS; track++) {
      const uint16_t x = gridStartX + (track * (GRID_SONG_CELL_WIDTH));
      const uint16_t y = gridStartY + (step * (GRID_SONG_CELL_HEIGHT));

      if (isSelectedStep && (track == data.selectedGridTrack)) {
        checkWebRemote();
        display.drawRect(x, y, GRID_SONG_CELL_WIDTH + 1, GRID_SONG_CELL_HEIGHT + 1, RED);
      }

      if (isCurrentStep) {
        bool hasActiveLayers = false;
        for (uint8_t layer = 0; layer < 4; layer++) {
          if (data.gridStep(actualStep, track, layer) && !liveSeq.isTrackLayerEmpty(track, layer)) {
            hasActiveLayers = true;
            break;
          }
        }
        if (hasActiveLayers) {
          checkWebRemote();
          display.drawRect(x, y, GRID_SONG_CELL_WIDTH + 1, GRID_SONG_CELL_HEIGHT + 1, GREEN);
        }
      }

      display.setTextSize(1);
      uint16_t color;
      const uint8_t layerSpacing = GRID_SONG_CELL_WIDTH / 4;
      const uint8_t layerOffset = 1;

      for (uint8_t layer = 0; layer < 2; layer++) {
        const uint16_t layerX = x + layerOffset + (layer * layerSpacing) + 6;
        const uint16_t layerY = y + 3;

        color = data.gridStep(actualStep, track, layer) ? COLOR_SYSTEXT : GREY3;
        display.setTextColor(color, COLOR_BACKGROUND);
        display.setCursor(layerX, layerY);
        if (!liveSeq.isTrackLayerEmpty(track, layer)) {
          display.printf("%i", layer + 1);
        }
      }

      for (uint8_t layer = 2; layer < 4; layer++) {
        const uint16_t layerX = x + layerOffset + ((layer - 2) * layerSpacing) + 6;
        const uint16_t layerY = y + GRID_SONG_CELL_HEIGHT / 2 + 3;
        color = data.gridStep(actualStep, track, layer) ? COLOR_SYSTEXT : GREY3;
        display.setTextColor(color, COLOR_BACKGROUND);
        display.setCursor(layerX, layerY);
        if (!liveSeq.isTrackLayerEmpty(track, layer)) {
          display.printf("%i", layer + 1);
        }
      }
    }
  }

  if (data.redraw_grid_buttons) {
    for (auto* button : gridControlButtons) {
      button->drawNow();
    }

    for (uint8_t layer = 0; layer < 4; layer++) {
      const uint16_t x = GRID.X[layer + 1];
      const uint16_t y = GRID.Y[5];

      if (liveSeq.isTrackLayerEmpty(data.selectedGridTrack, layer)) {
        TouchButton::drawButton(x, y, "LAYER", std::to_string(layer + 1).c_str(), TouchButton::BUTTON_INACTIVE);
      }
      else {
        const bool isActiveLayer = data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer);
        TouchButton::drawButton(x, y, "LAYER", std::to_string(layer + 1).c_str(),
          isActiveLayer ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL);
      }
    }

    TouchButton::drawButton(GRID.X[0], GRID.Y[5], "BACK", "EDIT", TouchButton::BUTTON_NORMAL);
    const bool playingNow = data.isRunning || data.isStarting || seq.running;
    TouchButton::drawButton(GRID.X[5], GRID.Y[5], "PLAY", playingNow ? "STOP" : "START",
      playingNow ? TouchButton::BUTTON_RED : TouchButton::BUTTON_NORMAL);
    data.redraw_grid_buttons = false;
  }

  needsRedraw = false;
}

void GridSongEditorUI::processBackButton() {
  exit();
  data.isSongMode = false;
  data.redraw_grid_header = true;
  data.redraw_grid_buttons = true;
  display.fillScreen(COLOR_BACKGROUND);

  if (!seq.running) {
    data.seq_started_from_pattern_mode = true;
  }

  if (callbacks.onExitRequested) {
    callbacks.onExitRequested();
  }
}

void GridSongEditorUI::processPlayButton() {
  const bool playingNow = data.isRunning || data.isStarting || seq.running;
  auto stopPlayback = [&]() {
    data.seq_started_from_pattern_mode = false;
    liveSeq.stop();
  };

  if (playingNow) {
    stopPlayback();
  }
  else {
    data.seq_started_from_pattern_mode = false;
    if (data.gridPlaybackStartStep >= LiveSequencer::LiveSeqData::GRIDSONGLEN) {
      data.gridPlaybackStartStep = LiveSequencer::LiveSeqData::GRIDSONGLEN - 1;
    }
    data.currentGridStep = data.gridPlaybackStartStep;
    if (data.gridPlaybackEndStep < data.gridPlaybackStartStep) {
      data.gridPlaybackEndStep = data.gridPlaybackStartStep;
      data.redraw_grid_buttons = true;
    }
    data.redraw_grid_header = true;
    liveSeq.start();
  }
  needsRedraw = true;
  data.redraw_grid_buttons = true;
}

void GridSongEditorUI::processGridTouch() {
  if (numTouchPoints == 0) {
    return;
  }

  const uint16_t touchX = ts.p.x;
  const uint8_t touchY = ts.p.y;

  for (uint8_t step = 0; step < GRID_SONG_ROWS; step++) {
    const uint8_t actualStep = data.gridStepOffset + step;
    if (actualStep >= data.GRIDSONGLEN) {
      break;
    }

    for (uint8_t track = 0; track < GRID_SONG_COLS; track++) {
      const uint16_t x = gridStartX + (track * GRID_SONG_CELL_WIDTH);
      const uint16_t y = gridStartY + (step * GRID_SONG_CELL_HEIGHT);

      if (touchX >= x && touchX <= x + GRID_SONG_CELL_WIDTH &&
        touchY >= y && touchY <= y + GRID_SONG_CELL_HEIGHT) {
        data.selectedGridStep = actualStep;
        data.selectedGridTrack = track;
        needsRedraw = true;
        data.redraw_grid_buttons = true;
        return;
      }
    }
  }
}

void GridSongEditorUI::processControlButtons() {
  for (auto* button : gridControlButtons) {
    button->processPressed();
  }
}

void GridSongEditorUI::toggleLayer(uint8_t layer) {
  data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer) =
    !data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer);
  needsRedraw = true;
  data.redraw_grid_buttons = true;
}

void GridSongEditorUI::clearGridCell() {
  for (uint8_t layer = 0; layer < 4; layer++) {
    data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer) = false;
  }
  needsRedraw = true;
  data.redraw_grid_buttons = true;
}

void GridSongEditorUI::fillGridCell() {
  for (uint8_t layer = 0; layer < 4; layer++) {
    data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer) = true;
  }
  needsRedraw = true;
  data.redraw_grid_buttons = true;
}

void GridSongEditorUI::copyFromAbove() {
  if (data.selectedGridStep == 0) {
    return;
  }

  for (uint8_t layer = 0; layer < 4; layer++) {
    data.gridStep(data.selectedGridStep, data.selectedGridTrack, layer) =
      data.gridStep(data.selectedGridStep - 1, data.selectedGridTrack, layer);
  }
  needsRedraw = true;
  data.redraw_grid_buttons = true;
}

void GridSongEditorUI::insertGridRow() {
  if (liveSeq.insertGridRow(data.selectedGridStep)) {
    needsRedraw = true;
    data.redraw_grid_buttons = true;
  }
}

void GridSongEditorUI::removeGridRow() {
  if (liveSeq.removeGridRow(data.selectedGridStep)) {
    needsRedraw = true;
    data.redraw_grid_buttons = true;
  }
}

void GridSongEditorUI::slowRect(uint16_t x, uint8_t y, uint16_t color) {
  display.drawLine(x, y, x + GRID_SONG_CELL_WIDTH + 1, y, color);
  display.drawLine(x, y, x, y + GRID_SONG_CELL_HEIGHT - 1, color);
  display.drawLine(x, y + GRID_SONG_CELL_HEIGHT, x + GRID_SONG_CELL_WIDTH + 1, y + GRID_SONG_CELL_HEIGHT, color);
  display.drawLine(x + GRID_SONG_CELL_WIDTH, y, x + GRID_SONG_CELL_WIDTH, y + GRID_SONG_CELL_HEIGHT - 1, color);
}

void GridSongEditorUI::checkWebRemote() {
  if (remote_active) {
    display.console = true;
  }
}
