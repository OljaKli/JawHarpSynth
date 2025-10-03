#include "ui_liveseq_pianoroll.h"
#include "UI.h"
#include "touch.h"
#include "ILI9341_t3n.h"
#include "LCDMenuLib2.h"
#include "ui_livesequencer.h"
#include "MD_REncoder.h"

extern LCDMenuLib2 LCDML;
extern ILI9341_t3n display;
extern MD_REncoder ENCODER[NUM_ENCODER];

UI_LiveSeq_PianoRoll* ui_pianoroll;

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getActualBarForNote(const LiveSequencer::NotePair& note) const {
  if (!data.isSongMode || data.useGridSongMode) {
    return note.noteOn.patternNumber;
  }

  // For song mode, find which map entry this event belongs to
  for (const auto& patternEntry : data.songEvents) {
    const uint16_t songPatternIndex = patternEntry.first;
    const auto& events = patternEntry.second;

    // Look for this specific event in the map
    for (const auto& event : events) {
      if (&event == &note.noteOn) {
        // Found it! Calculate absolute bar
        return (songPatternIndex * data.numberOfBars) + event.patternNumber;
      }
    }
  }

  // Fallback (shouldn't happen)
  return note.noteOn.patternNumber;
}

FLASHMEM UI_LiveSeq_PianoRoll::UI_LiveSeq_PianoRoll(LiveSequencer& sequencer, LiveSequencer::LiveSeqData& d) :
  liveSeq(sequencer),
  data(d),
  isRunningHere(d.isRunning) {
  ui_pianoroll = this;
  layerColors.push_back(COLOR_ARP); // layer 0
  layerColors.push_back(COLOR_CHORDS);
  layerColors.push_back(COLOR_DRUMS);
  layerColors.push_back(COLOR_PITCHSMP);
  initGUI();

  // initial view settings
  view.cursorPos = ROLL_WIDTH;
  view.cursorPosPrev = ROLL_WIDTH;
  view.startBar = 0;
  view.startOctave = 0;
  view.numOctaves = 5;
  view.numNotes = view.numOctaves * 12;
  view.noteHeight = CONTENT_HEIGHT / float(view.numNotes);
  view.lowestNote = 24 + view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);

  recordSteps = new EditableValueVector<uint8_t>(stepRecordSteps, { 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64 }, 16, [](auto* v) {});
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getMaxBarsForCurrentMode() const {
  if (data.isSongMode && !data.useGridSongMode) {
    uint16_t maxBar = 0;

    // Check if songEvents is empty
    if (data.songEvents.empty()) {
      return 1; // At least 1 bar even if empty
    }

    for (const auto& patternEntry : data.songEvents) {
      const uint16_t songPatternIndex = patternEntry.first; // Map key

      // Skip empty event lists
      if (patternEntry.second.empty()) {
        continue;
      }

      for (const auto& event : patternEntry.second) {
        // Calculate absolute bar from map key and cyclic pattern number
        uint16_t absoluteBar = (songPatternIndex * data.numberOfBars) + event.patternNumber;

        if (absoluteBar > maxBar) {
          maxBar = absoluteBar;
        }
      }
    }

    // Return maxBar + 1 to convert from index to count, but ensure at least 1
    return (maxBar == 0) ? 1 : (maxBar + 1);
  }
  else {
    return data.numberOfBars;
  }
}

FLASHMEM void UI_func_liveseq_graphic(uint8_t param) {  // for Livesequencer
  ui_pianoroll->processLCDM(param);
}

FLASHMEM void handle_touchscreen_pianoroll(void) {
  ui_pianoroll->handleTouchScreen();
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleTouchScreen() {
  for (TouchButton* b : buttons) {
    b->processPressed();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawLayersIndicator(void) {
  display.setTextSize(1);
  static constexpr uint16_t layerViewSizeX = ROLL_WIDTH / 2;
  static constexpr uint16_t layerViewY = CONTENT_HEIGHT + LINE_HEIGHT;

  // Handle both pattern mode and song mode
  if (data.activeTrack < SONG_MODE_NOTES_TRACK) {
    // Pattern mode: show layers for current pattern track
    for (uint8_t l = 0; l < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; l++) {
      const uint16_t x = GRID.X[0] + (l % 2) * (layerViewSizeX);
      const uint16_t y = layerViewY + (l / 2) * (HEADER_HEIGHT / 2);
      const bool isMuted = data.tracks[data.activeTrack].layerMutes & (1 << l);
      const bool hasNotes = data.trackSettings[data.activeTrack].layerCount > l;
      display.console = true;
      if (hasNotes) {
        if (isMuted) {
          display.fillRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), COLOR_BACKGROUND);
          display.drawRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), layerColors[l]);
          display.setTextColor(layerColors[l], COLOR_BACKGROUND);
        }
        else {
          display.fillRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), layerColors[l]);
          display.setTextColor(COLOR_BACKGROUND, layerColors[l]);
        }
      }
      else {
        display.fillRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), COLOR_BACKGROUND);
        display.drawRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), GREY2);
        display.setTextColor(GREY3, COLOR_BACKGROUND); // no text
      }
      display.setCursor(x + 2, y + 2);
      display.print(l + 1);
    }
  }
  else if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
    // Song mode: show song tracks (layers) instead of pattern layers
    for (uint8_t l = 0; l < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; l++) {
      const uint16_t x = GRID.X[0] + (l % 2) * (layerViewSizeX);
      const uint16_t y = layerViewY + (l / 2) * (HEADER_HEIGHT / 2);

      // For song mode, check if the layer has any notes in the current view
      bool hasNotes = false;
      for (const auto& note : notePairs) {
        if (note.noteOn.layer == l) {
          hasNotes = true;
          break;
        }
      }

      // Check if this is the active layer (for filtering)
      bool isActiveLayer = false;
      if (selectedNote > -1) {
        isActiveLayer = (notePairs.at(selectedNote).noteOn.layer == l);
      }

      display.console = true;
      if (hasNotes) {
        if (isActiveLayer) {
          // Active layer: filled with color
          display.fillRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), layerColors[l]);
          display.setTextColor(COLOR_BACKGROUND, layerColors[l]);
        }
        else {
          // Non-active layer: outline only
          display.fillRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), COLOR_BACKGROUND);
          display.drawRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), layerColors[l]);
          display.setTextColor(layerColors[l], COLOR_BACKGROUND);
        }
      }
      else {
        // No notes in this layer: grey outline
        display.fillRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), COLOR_BACKGROUND);
        display.drawRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), GREY2);
        display.setTextColor(GREY2, COLOR_BACKGROUND);
      }
      display.setCursor(x + 2, y + 2);
      display.print(l + 1); // Show layer number 1-4
    }
  }
  else {
    // Mute automation track or other tracks: clear the area
    display.fillRect(GRID.X[0], layerViewY, ROLL_WIDTH, HEADER_HEIGHT, COLOR_BACKGROUND);
  }
}

FLASHMEM uint32_t UI_LiveSeq_PianoRoll::getAbsoluteEventTime(const LiveSequencer::MidiEvent& e) const {
  if (data.isSongMode && !data.useGridSongMode) {
    return e.patternNumber * data.patternLengthMs + e.patternMs;
  }
  else {
    return e.patternNumber * data.patternLengthMs + e.patternMs;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(UpdateRange coords) {
  drawRegionsX.push_back(coords);
  drawFlags |= DRAW_NOTES;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note, bool isSelected, bool drawInGrey) const {
  uint32_t timeOn, timeOff;

  if (data.isSongMode && !data.useGridSongMode) {
    // Use absolute bar for time calculation
    uint16_t absoluteBarOn = getActualBarForNote(note);
    timeOn = absoluteBarOn * data.patternLengthMs + note.noteOn.patternMs;

    // Note: noteOff will be in the same or later absolute bar
    uint16_t absoluteBarOff = absoluteBarOn; // Start with same bar

    // If noteOff has a different cyclic patternNumber, it might wrap to next cycle
    if (note.noteOff.patternNumber < note.noteOn.patternNumber) {
      // Wrapped to next 4-bar cycle
      absoluteBarOff = absoluteBarOn + (data.numberOfBars - note.noteOn.patternNumber) + note.noteOff.patternNumber;
    }
    else {
      absoluteBarOff = absoluteBarOn + (note.noteOff.patternNumber - note.noteOn.patternNumber);
    }

    timeOff = absoluteBarOff * data.patternLengthMs + note.noteOff.patternMs;
  }
  else {
    timeOn = getEventTime(note.noteOn);
    timeOff = getEventTime(note.noteOff);
  }

  const uint16_t x = getNoteCoord(timeOn);
  const uint32_t lenMs = (timeOff > timeOn) ? (timeOff - timeOn) : 1;
  const int w = std::max(1, int(roundf(lenMs * view.msToPix)));

  if (!drawRegionsX.empty()) {
    for (auto& u : drawRegionsX) {
      const bool isInsideX = (int32_t(x) <= int32_t(u.toX)) && (int32_t(x + w) >= int32_t(u.fromX));
      const bool isInsideY = u.notes.count(note.noteOn.note_in) || u.notes.empty();
      if (isInsideX && isInsideY) {
        drawNoteAtPosition(note, isSelected, x, uint16_t(w), drawInGrey);
        break;
      }
    }
  }
  else {
    drawNoteAtPosition(note, isSelected, x, uint16_t(w), drawInGrey);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawAreaIndicator(void) {
  display.console = true;
  static constexpr uint16_t layerViewY = CONTENT_HEIGHT + LINE_HEIGHT;
  display.drawRect(ROLL_WIDTH, layerViewY,
    TouchButton::BUTTON_SIZE_X - ROLL_WIDTH,
    HEADER_HEIGHT, GREY1);

  static constexpr uint16_t wTotal = TouchButton::BUTTON_SIZE_X - ROLL_WIDTH - 2;
  static constexpr uint16_t hTotal = HEADER_HEIGHT - 2;

  display.fillRect(ROLL_WIDTH + 1, layerViewY + 1, wTotal, hTotal, COLOR_BACKGROUND);

  uint16_t maxBars = getMaxBarsForCurrentMode();
  uint32_t totalTimeMs, viewTimeMs, startTimeMs;

  if (data.isSongMode && !data.useGridSongMode) {
    // Song mode: convert visual bars to internal bars for timing
    uint16_t maxVisualBars = (maxBars + 3) / 4;
    totalTimeMs = uint32_t(maxVisualBars) * 4 * data.patternLengthMs;
    viewTimeMs = uint32_t(view.numBars) * 4 * data.patternLengthMs;
    startTimeMs = uint32_t(view.startBar) * 4 * data.patternLengthMs;
  }
  else {
    // Pattern mode
    totalTimeMs = uint32_t(maxBars) * data.patternLengthMs;
    viewTimeMs = uint32_t(view.numBars) * data.patternLengthMs;
    startTimeMs = uint32_t(view.startBar) * data.patternLengthMs;
  }

  if (totalTimeMs == 0) return;

  const uint16_t x = ROLL_WIDTH + 1 + roundf((float)wTotal * (float)startTimeMs / (float)totalTimeMs);
  const uint16_t h = roundf(hTotal * view.numOctaves / float(TOTAL_OCTAVES));
  const uint16_t y = layerViewY + 1 + hTotal - h -
    roundf(hTotal * view.startOctave / float(TOTAL_OCTAVES));

  int w = int(roundf((float)wTotal * (float)viewTimeMs / (float)totalTimeMs));
  if (w < 1) w = 1;
  if (x + w > ROLL_WIDTH + wTotal) w = (ROLL_WIDTH + wTotal) - x;

  display.fillRect(x, y, uint16_t(w), h, MIDDLEGREEN);

  // Show bar info
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setTextSize(1);
  display.setCursor(GRID.X[1], layerViewY + 3);

  if (data.isSongMode && !data.useGridSongMode) {
    uint16_t startInternalBar = view.startBar * 4 + 1;
    uint16_t endInternalBar = (view.startBar + view.numBars) * 4;
    uint16_t maxInternalBars = maxBars;
    display.printf("BAR:%i-%i/%i", startInternalBar, endInternalBar, maxInternalBars);
  }
  else {
    display.printf("BAR:%i-%i", view.startBar + 1, view.startBar + view.numBars);
  }

  display.setCursor(GRID.X[1], layerViewY + CHAR_height_small + 5);
  display.printf("OCT:%i-%i", view.startOctave, view.startOctave + view.numOctaves - 1);
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawBackground(void) {
  for (auto& u : drawRegionsX) {
    for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
      if (u.notes.count(note) || u.notes.empty()) {
        const uint8_t noteIndex = view.highestNote - note - 1;
        const uint16_t y = round(noteIndex * view.noteHeight);
        const bool isPressed = keysPressed.find(note) != keysPressed.end();
        const uint16_t bgColor = isPressed ? GREY2 : (note & 0x01 ? GREY4 : COLOR_BACKGROUND);
        display.console = true;

        const uint16_t width = u.toX - u.fromX;
        const uint16_t xFrom = std::max(ROLL_WIDTH, u.fromX);

        // Draw full height background to match grid
        if (width > 1) {
          display.fillRect(xFrom, y, u.toX - u.fromX, view.noteHeight, bgColor);
        }
        else {
          display.drawLine(xFrom, y, xFrom, y + view.noteHeight - 1, bgColor);
        }
      }
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(LiveSequencer::NotePair note) {
  UpdateRange coords;
  const uint32_t timeOn = getEventTime(note.noteOn);
  const uint32_t timeOff = getEventTime(note.noteOff);
  coords.fromX = getNoteCoord(timeOn);
  coords.toX = getNoteCoord(timeOff);
  coords.notes = { note.noteOn.note_in };

  coords.fromX = (coords.fromX < 2 + ROLL_WIDTH) ? ROLL_WIDTH : (coords.fromX - 2);
  coords.toX = (coords.toX > DISPLAY_WIDTH - 2) ? DISPLAY_WIDTH : (coords.toX + 2);

  drawRegionsX.push_back(coords);
  drawFlags |= DRAW_NOTES | DRAW_VERTICAL_BARS;
}


FLASHMEM void UI_LiveSeq_PianoRoll::processLCDM(uint8_t param) {
  if (LCDML.FUNC_setup()) { // ****** SETUP *********
    openendFromLiveSequencer = data.processMidiIn;
    if (openendFromLiveSequencer == false) {
      data.processMidiIn = true;
    }

    // Set initial track based on song mode
    if (data.isSongMode && !data.useGridSongMode) {
      // Start in song mode (notes view)
      data.activeTrack = SONG_MODE_NOTES_TRACK;
    }
    else {
      // Start at first pattern
      data.activeTrack = 0;
    }

    liveSeq.setActiveTrack(data.activeTrack);

    isVisible = true;
    registerTouchHandler(handle_touchscreen_pianoroll);

    if (data.isSongMode && !data.useGridSongMode) {
      uint16_t maxBars = getMaxBarsForCurrentMode();
      view.startBar = 0;
      view.numBars = std::min(uint16_t(16), maxBars);

      // Calculate msToPix: patternLengthMs is 4 bars, so divide by 4 to get per-bar time
      view.msToPix = CONTENT_WIDTH / float(view.numBars * (data.patternLengthMs));
    }
    else {
      // Pattern mode
      view.startBar = 0;
      view.numBars = data.numberOfBars;
      view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);

    }

    reloadNotes(true);

    // setup function
    display.console = true;
    display.fillScreen(COLOR_BACKGROUND);
    LCDML.FUNC_setLoopInterval(25); // 40Hz gui refresh

    display.drawLine(0, CONTENT_HEIGHT, DISPLAY_WIDTH, CONTENT_HEIGHT, GREY2);

    drawLayersIndicator();
    drawAreaIndicator();

    drawHeader(HEADER_DRAW_ALL);

    drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;

    for (auto* b : buttons) {
      b->drawNow();
    }
  }
  if (LCDML.FUNC_loop()) {
    const EncoderEvents e = getEncoderEvents(ENC_R);
    if (e.turned) {
      handleEncoderRight(e);
    }
    if (e.pressed) {
      toggleSettingActivated();
    }

    const bool runningChanged = isRunningHere != data.isRunning;
    const bool justStopped = isRunningHere && !data.isRunning;
    if (runningChanged) {
      isRunningHere = data.isRunning;
      buttonPlay->drawNow();
      buttonStep->drawNow(); // handle active state
    }

    if (isRunningHere || justStopped) {
      uint32_t currentTimeMs = 0;
      uint32_t viewStartMs = 0;
      uint32_t viewEndMs = 0;

      if (data.isSongMode && !data.useGridSongMode) {
        // Song mode: calculate absolute time from song start
        currentTimeMs = data.currentPattern * data.patternLengthMs + data.patternTimer;

        // Calculate viewport time range (absolute time)
        viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
        viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);
      }
      else {
        // Pattern mode: calculate relative time within pattern
        currentTimeMs = data.currentPattern * data.patternLengthMs + data.patternTimer;
        viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
        viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);
      }

      // Calculate cursor position
      if (currentTimeMs >= viewStartMs && currentTimeMs <= viewEndMs) {
        // Cursor is within viewport - calculate position
        float ratio = float(currentTimeMs - viewStartMs) / float(viewEndMs - viewStartMs);
        view.cursorPos = ROLL_WIDTH + roundf(ratio * CONTENT_WIDTH);
      }
      else if (currentTimeMs < viewStartMs) {
        // Cursor is before viewport
        view.cursorPos = ROLL_WIDTH;
      }
      else {
        // Cursor is after viewport
        view.cursorPos = DISPLAY_WIDTH - 1;
      }

      // Handle cursor color based on whether it's in viewport
      const bool inViewport = (currentTimeMs >= viewStartMs && currentTimeMs <= viewEndMs);
      const uint16_t color = inViewport ? MIDDLEGREEN : RED;
      const bool colorChanged = color != view.cursorColor;
      const bool cursorPosChanged = (view.cursorPos != view.cursorPosPrev);

      if (justStopped || colorChanged || cursorPosChanged) {
        // Redraw the area where the cursor was
        if (view.cursorPosPrev >= ROLL_WIDTH && view.cursorPosPrev < DISPLAY_WIDTH) {
          UpdateRange cursorRegion;
          cursorRegion.fromX = view.cursorPosPrev - 1;
          cursorRegion.toX = view.cursorPosPrev + 1;
          cursorRegion.notes = {}; // Empty set = all notes (for background redraw)
          drawRegionsX.push_back(cursorRegion);
          drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
        }

        if (!justStopped) {
          view.cursorColor = color;
          drawFlags |= DRAW_CURSOR;
        }
        view.cursorPosPrev = view.cursorPos;
      }
    }

    drawGUI();
  }
  if (LCDML.FUNC_close()) {
    if (openendFromLiveSequencer == false) {
      data.processMidiIn = false;
    }
    isVisible = false;
    unregisterTouchHandler();
    display.fillScreen(COLOR_BACKGROUND);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawCursor(void) {
  display.console = true;
  display.drawLine(view.cursorPos, 0, view.cursorPos, CONTENT_HEIGHT - 1, view.cursorColor);
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawStepCursor(void) {
  display.console = true;
  
  // First, clear the previous cursor position by redrawing the background
  if (stepCursorPositionPrev != stepCursorPosition && stepCursorPositionPrev >= ROLL_WIDTH) {
    // We rely on the background/notes being redrawn first via drawFlags
  }
  
  // Then draw the new cursor
  display.drawLine(stepCursorPosition, 0, stepCursorPosition, CONTENT_HEIGHT - 1, RED);
}

FLASHMEM std::vector<LiveSequencer::NotePair>
UI_LiveSeq_PianoRoll::getNotePairsFromSongTrack(
  uint8_t track, uint8_t& lowestNote, uint8_t& highestNote,
  uint8_t patternFrom, uint8_t patternTo) {

  std::vector<LiveSequencer::NotePair> result;
  uint8_t lowestFound = 0xFF;
  uint8_t highestFound = 0x00;

  const uint8_t viewLowestNote = view.lowestNote;
  const uint8_t viewHighestNote = view.highestNote;

  if (data.songEvents.empty()) {
    lowestNote = viewLowestNote;
    highestNote = viewHighestNote;
    return result;
  }

  for (const auto& patternEntry : data.songEvents) {
    const uint16_t songPatternIndex = patternEntry.first;
    const auto& events = patternEntry.second;

    for (auto itOn = events.begin(); itOn != events.end(); ++itOn) {
      if ((track == 0xFF || itOn->track == track) && itOn->event == midi::NoteOn) {

        if (itOn->note_in < viewLowestNote || itOn->note_in >= viewHighestNote) {
          continue;
        }

        // Calculate absolute bar for filtering
        uint16_t absoluteBar = (songPatternIndex * data.numberOfBars) + itOn->patternNumber;

        if (absoluteBar >= patternFrom && absoluteBar <= patternTo) {
          for (auto itOff = itOn; itOff != events.end(); ++itOff) {
            if (itOff->track == itOn->track &&
              itOff->layer == itOn->layer &&
              itOff->note_in == itOn->note_in &&
              itOff->event == midi::NoteOff) {

              LiveSequencer::NotePair p = {
                const_cast<LiveSequencer::MidiEvent&>(*itOn),
                const_cast<LiveSequencer::MidiEvent&>(*itOff),
                false
              };

              result.emplace_back(p);

              highestFound = std::max(highestFound, itOn->note_in);
              lowestFound = std::min(lowestFound, itOn->note_in);
              break;
            }
          }
        }
      }
    }
  }

  lowestNote = (lowestFound == 0xFF) ? viewLowestNote : lowestFound;
  highestNote = (highestFound == 0x00) ? viewHighestNote : highestFound;
  return result;
}

FLASHMEM std::vector<LiveSequencer::MidiEvent>
UI_LiveSeq_PianoRoll::getMuteAutomationEvents(
  uint8_t track, uint8_t patternFrom, uint8_t patternTo) {

  std::vector<LiveSequencer::MidiEvent> result;

  // View time (ms) and pixel bounds (X)
  const uint32_t viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
  const uint32_t viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);
  const uint16_t pixStartX = ROLL_WIDTH;
  const uint16_t pixEndX = DISPLAY_WIDTH - 1;

  // Check if songEvents is empty
  if (data.songEvents.empty()) {
    return result;
  }

  for (const auto& patternEntry : data.songEvents) {
    const uint16_t actualBar = patternEntry.first;

    // Skip patterns outside our view range
    if (actualBar < view.startBar || actualBar >= view.startBar + view.numBars) {
      continue;
    }

    // Skip empty event lists
    if (patternEntry.second.empty()) {
      continue;
    }

    for (const auto& e : patternEntry.second) {
      if ((track == 0xFF || e.track == track) &&
        e.event == midi::ControlChange &&
        (e.note_in == LiveSequencer::TYPE_MUTE_ON || e.note_in == LiveSequencer::TYPE_MUTE_OFF)) {

        // absolute time for this song-map event
        const uint32_t eventTime = uint32_t(actualBar) * data.patternLengthMs + e.patternMs;

        // time overlap (allow endpoints)
        const bool isInTime = (eventTime >= viewStartMs && eventTime <= viewEndMs);
        if (!isInTime) continue;

        // pixel overlap
        const uint16_t x = getNoteCoord(eventTime);
        const bool isInPixels = (x >= pixStartX && x <= pixEndX);
        if (!isInPixels) continue;

        // Return a copy but set the absolute patternNumber so downstream code sees the absolute bar
        LiveSequencer::MidiEvent ev = e;
        ev.patternNumber = actualBar;
        result.emplace_back(ev);
      }
    }
  }

  return result;
}

FLASHMEM void UI_LiveSeq_PianoRoll::reloadNotes(bool autoAdaptView) {
  const uint8_t patternFrom = view.startBar;
  const uint8_t patternTo = view.startBar + view.numBars - 1;
  uint8_t lowestNote = view.lowestNote;
  uint8_t highestNote = view.highestNote;

  // Ensure active track is valid for current mode
  const uint8_t maxTrack = getMaxTrackForCurrentMode();
  if (data.activeTrack > maxTrack) {
    data.activeTrack = 0;
  }

  if (autoAdaptView) {
    lowestNote = 0x00;
    highestNote = 0xFF;
  }

  // Store previous state to detect changes
  size_t prevNotePairsCount = notePairs.size();
  size_t prevMuteEventsCount = muteAutomationEvents.size();

  // Load note pairs based on track type
  if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
    // Song mode: show ALL notes from ALL tracks
    notePairs = getNotePairsFromSongTrack(0xFF, lowestNote, highestNote, patternFrom, patternTo);
    muteAutomationEvents.clear();
    selectedMuteEvent = -1;

    // Reset selected note if it's now out of bounds
    if (selectedNote >= static_cast<int16_t>(notePairs.size())) {
      selectedNote = notePairs.empty() ? -1 : 0;
    }
  }
  else if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    // Song mode: show only automation events
    notePairs.clear();
    muteAutomationEvents = getMuteAutomationEvents(0xFF, patternFrom, patternTo);
    selectedNote = -1;

    // Reset selected mute event if it's now out of bounds
    if (selectedMuteEvent >= static_cast<int16_t>(muteAutomationEvents.size())) {
      selectedMuteEvent = muteAutomationEvents.empty() ? -1 : 0;
    }
  }
  else {
    // Pattern mode: use the existing method for specific track
    notePairs = liveSeq.getNotePairsFromTrack(data.activeTrack, lowestNote, highestNote, patternFrom, patternTo);
    muteAutomationEvents.clear();
    selectedMuteEvent = -1;

    // Reset selected note if it's now out of bounds
    if (selectedNote >= static_cast<int16_t>(notePairs.size())) {
      selectedNote = notePairs.empty() ? -1 : 0;
    }
  }

  // Only set draw flags if data actually changed
  bool dataChanged = (prevNotePairsCount != notePairs.size()) ||
    (prevMuteEventsCount != muteAutomationEvents.size());

  if (autoAdaptView && !notePairs.empty()) {
    view.startOctave = std::min((lowestNote - 24) / 12, TOTAL_OCTAVES - view.numOctaves);
    setNumOctaves(5);

    // Use same calculation for both modes
    view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);

    drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
  }
  else if (dataChanged) {
    // Only redraw if data actually changed
    drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
  }

  // Always update header when reloading
  drawHeader(HEADER_DRAW_ALL);
  drawLayersIndicator();
}


FLASHMEM void UI_LiveSeq_PianoRoll::drawMuteAutomation(void) {
  if (!data.isSongMode || data.useGridSongMode) return;
  if (data.activeTrack != SONG_MODE_MUTE_TRACK) return;

  display.setTextSize(1);
  display.console = true;

  const uint32_t viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
  const uint32_t viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);

  for (size_t i = 0; i < muteAutomationEvents.size(); i++) {
    const auto& event = muteAutomationEvents.at(i);
    const uint32_t eventTime = getEventTime(event);

    // Skip events outside viewport
    if (eventTime < viewStartMs || eventTime > viewEndMs) {
      continue;
    }

    const uint16_t x = getNoteCoord(eventTime);
    const uint16_t y = CONTENT_HEIGHT - 12;

    const bool isSelected = (static_cast<int16_t>(i) == selectedMuteEvent);

    uint16_t arrowColor = isSelected ? RED :
      (event.note_in == LiveSequencer::TYPE_MUTE_ON ? GREY2 : GREEN);

    if (event.note_in == LiveSequencer::TYPE_MUTE_ON) {
      display.setTextColor(arrowColor, COLOR_BACKGROUND);
      display.setCursor(x - 2, y);
      display.write(0x1F);
    }
    else if (event.note_in == LiveSequencer::TYPE_MUTE_OFF) {
      display.setTextColor(arrowColor, COLOR_BACKGROUND);
      display.setCursor(x - 2, y);
      display.write(0x1E);
    }

    display.setTextColor(isSelected ? RED : COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setCursor(x - 2, y - 8);
    display.printf("%d", event.note_in_velocity + 1);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawGUI(void) {
  // Only draw if there are actual draw flags set
  if (drawFlags == 0) {
    return;
  }

  // NOTE: order of drawing is important
  if (drawFlags & DRAW_CONTENT_FULL) {
    drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES | DRAW_MUTE_AUTOMATION;
    queueDrawNote({ ROLL_WIDTH, DISPLAY_WIDTH, {} }); // force redraw all

    // Clear the content area for full redraw
    display.console = true;
    display.fillRect(ROLL_WIDTH, 0, CONTENT_WIDTH, CONTENT_HEIGHT, COLOR_BACKGROUND);
  }
  if (drawFlags & DRAW_BACKGROUND) {
    drawBackground();
  }
  if (drawFlags & DRAW_VERTICAL_BARS) {
    drawVerticalBars();
  }
  if (drawFlags & DRAW_CURSOR) {
    drawCursor();
  }
  if (drawFlags & DRAW_STEP_CURSOR) {
    drawStepCursor();
  }
  if (drawFlags & DRAW_NOTES) {
    drawNotes();
  }
  if (drawFlags & DRAW_MUTE_AUTOMATION) {
    drawMuteAutomation();
  }
  if (drawFlags & DRAW_PIANOROLL) {
    drawPianoRoll();
  }

  // ADDED: Draw bar information on top of everything
  // Only draw when we're actually drawing content, not just UI elements
  if (drawFlags & (DRAW_CONTENT_FULL | DRAW_BACKGROUND | DRAW_NOTES | DRAW_VERTICAL_BARS)) {
    drawBarInfoOnTop();
  }

  // Clear all draw flags after processing
  drawFlags = 0;
  drawRegionsX.clear();
}

// ADDED: New function to draw bar/time information that won't be cleared by background


FLASHMEM void UI_LiveSeq_PianoRoll::handleKeyChanged(uint8_t key, midi::MidiType event, uint8_t velocity) {
  if (isVisible) {
    switch (event) {
    case midi::NoteOn:
      keysPressed.insert(key);
      drawFlags |= DRAW_CURSOR;
      break;

    case midi::NoteOff:
      keysPressed.erase(key);
      drawFlags |= DRAW_CURSOR;
      break;

    default:
      break;
    }

    if ((key >= view.lowestNote) && (key <= view.highestNote)) {
      queueDrawNote({ ROLL_WIDTH, DISPLAY_WIDTH, { key } }); // fullwidth highlight key bar
    }

    // CHANGE: Allow step recording in ANY step mode, not just STEP_RECORD
    if ((data.isRunning == false) && (mode == MODE_STEP) ) {
      if (event == midi::NoteOn) {
        // record note
        const uint16_t stepCursorPositionMs = view.startBar * data.patternLengthMs + round(stepCursorPositionIndex * stepRecordStepSizeMs);
        LiveSequencer::MidiEvent on;
        on.source = data.isSongMode ? LiveSequencer::EVENT_SONG : LiveSequencer::EVENT_PATTERN;
        on.event = midi::NoteOn;
        on.note_in = key;
        on.note_in_velocity = velocity;
        on.patternNumber = stepCursorPositionMs / data.patternLengthMs;
        on.patternMs = stepCursorPositionMs % data.patternLengthMs;
        on.track = data.activeTrack;
        on.layer = stepRecordLayer;
        LiveSequencer::MidiEvent off = on;
        off.event = midi::NoteOff;
        off.patternMs += stepRecordStepSizeMs / 2; // half step note length
        if (notePairs.empty()) {
          drawFlags |= DRAW_CONTENT_FULL; // clear "no notes" text
        }
        if (stepRecordLayer + 1 > data.trackSettings[data.activeTrack].layerCount) {
          data.trackSettings[data.activeTrack].layerCount++;
          drawLayersIndicator();
        }
        liveSeq.addNotePair(on, off);
        reloadNotes();
      }
      else if (keysPressed.empty()) {
        // advance on last key released
        step(+1);
      }
    }
  }
}

template<typename T>
FLASHMEM bool UI_LiveSeq_PianoRoll::hasConstrainedChanged(T& value, int16_t diff, T min, T max) {
  const T newValue = constrain(value + diff, min, max);
  const bool changed = newValue != value;
  value = newValue;
  return changed;
}

FLASHMEM void UI_LiveSeq_PianoRoll::setNumBars(int8_t num) {
  if (num == view.numBars) return;

  uint16_t maxBars = getMaxBarsForCurrentMode();
  uint16_t newNumBars = constrain(num, 1, maxBars);

  if (newNumBars != view.numBars) {
    view.numBars = newNumBars;

    if (view.startBar > (maxBars - view.numBars)) {
      setBarStart(maxBars - view.numBars);
    }

    view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);

    reloadNotes();
    drawAreaIndicator();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::setBarStart(int8_t num) {
  if (num == view.startBar) return;

  uint16_t maxBars = getMaxBarsForCurrentMode();

  // Calculate maximum start bar that still shows content
  uint16_t maxStartBar = 0;
  if (maxBars > view.numBars) {
    maxStartBar = maxBars - view.numBars;
  }

  // Additional safety: ensure we don't scroll past actual content
  // If there are fewer bars than our view, always start at 0
  if (maxBars < view.numBars) {
    maxStartBar = 0;
  }

  uint16_t newStartBar = constrain(num, 0, maxStartBar);

  if (newStartBar != view.startBar) {
    view.startBar = newStartBar;
    reloadNotes();
    drawAreaIndicator();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::toggleSettingActivated(void) {
  isSettingActivated = !isSettingActivated;
  if (mode == MODE_EDIT) {
    buttonEdit->drawNow(); // Redraw Edit button to show/hide DELETE text for ALL edit modes
  }
  drawHeader(HEADER_DRAW_ALL);
}

FLASHMEM void UI_LiveSeq_PianoRoll::onEventsChanged(void) {
  if (isVisible) {
    reloadNotes();

    // re-locate selected note as it may have changed order (index)
    for (int16_t i = 0; i < int16_t(notePairs.size()); i++) {
      if (memcmp(&notePairs.at(i).noteOn, &currentNote, sizeof(currentNote)) == 0) {
        selectedNote = i;
        break;
      }
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::updateSongEvent(LiveSequencer::MidiEvent& updatedEvent) {
  // Find and update the event in songEvents
  for (auto& patternEntry : data.songEvents) {
    for (auto& songEvent : patternEntry.second) {
      // Match by all immutable properties
      if (songEvent.track == updatedEvent.track &&
        songEvent.layer == updatedEvent.layer &&
        songEvent.source == updatedEvent.source) {

        // For notes, also match by the original note value and event type
        bool isMatch = false;
        if (updatedEvent.event == midi::NoteOn || updatedEvent.event == midi::NoteOff) {
          // Match by event type (NoteOn/Off) and similar timing
          isMatch = (songEvent.event == updatedEvent.event) &&
            (abs(int(songEvent.patternNumber) - int(updatedEvent.patternNumber)) <= 1);
        }
        else {
          // For CC/automation, match by note_in (controller number or mute type)
          isMatch = (songEvent.event == updatedEvent.event) &&
            (songEvent.note_in == updatedEvent.note_in ||
              (updatedEvent.event == midi::ControlChange &&
                songEvent.patternNumber == updatedEvent.patternNumber &&
                songEvent.patternMs == updatedEvent.patternMs));
        }

        if (isMatch) {
          songEvent = updatedEvent;
          return;
        }
      }
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleEncoderRight(EncoderEvents e) {
  if (isSettingActivated) {
    uint16_t headerFlags = 0;
    switch (mode) {
    case MODE_VIEW:
      switch (viewMode) {
      case VIEW_ZOOM_Y:
        if (hasConstrainedChanged(view.numOctaves, -2 * e.dir, int8_t(1), int8_t(5))) {
          setNumOctaves(view.numOctaves);
          drawFlags |= DRAW_CONTENT_FULL;
        }
        break;

      case VIEW_SCROLL_Y:
      {
        int8_t newStartOctave = view.startOctave + e.dir;
        int8_t maxStartOctave = TOTAL_OCTAVES - view.numOctaves;
        if (maxStartOctave < 0) maxStartOctave = 0;

        if (newStartOctave < 0) newStartOctave = 0;
        if (newStartOctave > maxStartOctave) newStartOctave = maxStartOctave;

        if (newStartOctave != view.startOctave) {
          view.startOctave = newStartOctave;
          view.lowestNote = 24 + view.startOctave * 12;
          view.highestNote = (view.lowestNote + view.numNotes);

          // Force full content redraw
          drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
          drawAreaIndicator();
        }
      }
      break;

      case VIEW_ZOOM_X:
        if (data.isSongMode && !data.useGridSongMode) {
          uint16_t maxBars = getMaxBarsForCurrentMode();

          int16_t newNumBars = view.numBars + e.dir;
          if (newNumBars < 1) newNumBars = 1;
          if (newNumBars > int16_t(maxBars)) newNumBars = int16_t(maxBars);

          if (newNumBars != view.numBars) {
            setNumBars(newNumBars);
            drawFlags |= DRAW_CONTENT_FULL;
          }
        }
        else {
          int16_t newNumBars = view.numBars - e.dir;
          if (newNumBars < 1) newNumBars = 1;
          if (newNumBars > int16_t(data.numberOfBars)) newNumBars = data.numberOfBars;
          if (newNumBars != view.numBars) {
            setNumBars(newNumBars);
            drawFlags |= DRAW_CONTENT_FULL;
          }
        }
        break;

      case VIEW_SCROLL_X:
        if (data.isSongMode && !data.useGridSongMode) {
          uint16_t maxBars = getMaxBarsForCurrentMode();

          int16_t newStartBar = view.startBar + e.dir;

          // Use the same logic as setBarStart
          uint16_t maxStartBar = 0;
          if (maxBars > view.numBars) {
            maxStartBar = maxBars - view.numBars;
          }

          if (newStartBar < 0) newStartBar = 0;
          if (newStartBar > maxStartBar) newStartBar = maxStartBar;

          if (newStartBar != view.startBar) {
            setBarStart(newStartBar);
            drawFlags |= DRAW_CONTENT_FULL;
          }
        }
        else {
          int16_t newStartBar = view.startBar + e.dir;
          int16_t maxStartBar = int16_t(data.numberOfBars) - view.numBars;
          if (maxStartBar < 0) maxStartBar = 0;
          if (newStartBar < 0) newStartBar = 0;
          if (newStartBar > maxStartBar) newStartBar = maxStartBar;
          if (newStartBar != view.startBar) {
            setBarStart(newStartBar);
            drawFlags |= DRAW_CONTENT_FULL;
          }
        }
        break;
      } // end switch(viewMode)

      if (drawFlags && data.isRunning) {
        drawFlags |= DRAW_CURSOR; // handle cursor off on viewport changed redraw
      }
      break;

    case MODE_EDIT:
      headerFlags |= (1 << editMode);
      switch (editMode) {
      case EDIT_SELECT_NOTE:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          // For mute automation: scroll through events
          if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
            int16_t newSelection = selectedMuteEvent + e.dir;
            if (newSelection < 0) newSelection = muteAutomationEvents.size() - 1;
            else if (newSelection >= static_cast<int16_t>(muteAutomationEvents.size())) newSelection = 0;

            if (newSelection != selectedMuteEvent) {
              selectedMuteEvent = newSelection;
              headerFlags = (1 << EDIT_SELECT_NOTE);
              drawFlags |= DRAW_MUTE_AUTOMATION;
            }
          }
        }
        else {
          if (!notePairs.empty()) {
            // Get the current track/layer
            int8_t currentTrackLayer = 0;
            if (selectedNote > -1) {
              currentTrackLayer = notePairs.at(selectedNote).noteOn.layer;
            }

            // Calculate current viewport time range
            uint32_t viewStartMs, viewEndMs;
            if (data.isSongMode && !data.useGridSongMode) {
              // Song mode: view.startBar is in visual bars (1:1 mapping now)
              viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
              viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);
            }
            else {
              // Pattern mode
              viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
              viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);
            }

            // Build list of notes in current track/layer AND visible in viewport
            std::vector<int16_t> visibleTrackNotes;
            for (size_t i = 0; i < notePairs.size(); i++) {
              auto& note = notePairs[i];

              // Filter by track/layer
              if (note.noteOn.layer != currentTrackLayer) {
                continue;
              }

              // Filter by viewport
              uint32_t noteTime;
              if (data.isSongMode && !data.useGridSongMode) {
                uint16_t actualBar = getActualBarForNote(note);
                noteTime = actualBar * data.patternLengthMs + note.noteOn.patternMs;
              }
              else {
                noteTime = getEventTime(note.noteOn);
              }

              if (noteTime >= viewStartMs && noteTime <= viewEndMs) {
                visibleTrackNotes.push_back(i);
              }
            }

            // Sort by time for logical navigation order
            std::sort(visibleTrackNotes.begin(), visibleTrackNotes.end(),
              [this](int16_t a, int16_t b) {
                uint32_t timeA, timeB;

                if (data.isSongMode && !data.useGridSongMode) {
                  uint16_t barA = getActualBarForNote(notePairs[a]);
                  uint16_t barB = getActualBarForNote(notePairs[b]);
                  timeA = barA * data.patternLengthMs + notePairs[a].noteOn.patternMs;
                  timeB = barB * data.patternLengthMs + notePairs[b].noteOn.patternMs;
                }
                else {
                  timeA = getEventTime(notePairs[a].noteOn);
                  timeB = getEventTime(notePairs[b].noteOn);
                }

                return timeA < timeB;
              });

            if (!visibleTrackNotes.empty()) {
              int16_t newSelection;

              if (selectedNote == -1) {
                // Start with first visible note in current track
                newSelection = visibleTrackNotes[0];
              }
              else {
                // Find current position in visible track notes
                int16_t currentPos = -1;
                for (size_t i = 0; i < visibleTrackNotes.size(); i++) {
                  if (visibleTrackNotes[i] == selectedNote) {
                    currentPos = i;
                    break;
                  }
                }

                if (currentPos == -1) {
                  // Current selection not visible - start with first visible
                  newSelection = visibleTrackNotes[0];
                }
                else {
                  // Move within visible track notes
                  int16_t newPos = currentPos + e.dir;
                  if (newPos < 0) newPos = visibleTrackNotes.size() - 1;
                  else if (newPos >= static_cast<int16_t>(visibleTrackNotes.size())) newPos = 0;

                  newSelection = visibleTrackNotes[newPos];
                }
              }

              if (newSelection != selectedNote) {
                if (selectedNote > -1) queueDrawNote(notePairs.at(selectedNote));
                selectedNote = newSelection;
                if (selectedNote > -1) queueDrawNote(notePairs.at(selectedNote));
                drawFlags |= DRAW_NOTES;
                headerFlags = (1 << EDIT_SELECT_NOTE);
              }
            }
            else {
              // No visible notes in current track - clear selection
              if (selectedNote > -1) {
                queueDrawNote(notePairs.at(selectedNote));
                selectedNote = -1;
                drawFlags |= DRAW_NOTES;
              }
            }
          }
        }
        break;

      case EDIT_LAYER:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          // For mute automation: change layer
          if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
            LiveSequencer::MidiEvent& event = muteAutomationEvents.at(selectedMuteEvent);
            int8_t layer = event.note_in_velocity;
            if (hasConstrainedChanged(layer, e.dir, int8_t(0), int8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1))) {
              event.note_in_velocity = layer;
              updateSongEvent(event);
              headerFlags |= (1 << EDIT_LAYER);
              drawFlags |= DRAW_MUTE_AUTOMATION;
            }
          }
        }
        else {
          // Layer editing for both pattern mode AND song mode
          if (selectedNote > -1) {
            int8_t layer = notePairs.at(selectedNote).noteOn.layer;

            // Find available layers with notes
            std::set<int8_t> availableLayers;
            for (const auto& note : notePairs) {
              availableLayers.insert(note.noteOn.layer);
            }

            // If current layer is not in available layers (shouldn't happen), add it
            if (availableLayers.empty()) {
              availableLayers.insert(layer);
            }

            // Find next/previous available layer
            int8_t newLayer = layer;
            if (e.dir > 0) {
              // Move to next available layer
              auto it = availableLayers.upper_bound(layer);
              if (it == availableLayers.end()) {
                newLayer = *availableLayers.begin(); // Wrap around to first
              }
              else {
                newLayer = *it;
              }
            }
            else if (e.dir < 0) {
              // Move to previous available layer
              auto it = availableLayers.lower_bound(layer);
              if (it == availableLayers.begin()) {
                newLayer = *availableLayers.rbegin(); // Wrap around to last
              }
              else {
                --it;
                newLayer = *it;
              }
            }

            if (newLayer != layer) {
              // Update both noteOn and noteOff
              notePairs.at(selectedNote).noteOn.layer = newLayer;
              notePairs.at(selectedNote).noteOff.layer = newLayer;

              if (data.isSongMode && !data.useGridSongMode) {
                // Update in songEvents for song mode
                updateSongEvent(notePairs.at(selectedNote).noteOn);
                updateSongEvent(notePairs.at(selectedNote).noteOff);
              }

              // Reload to reflect changes
              reloadNotes();
              headerFlags |= (1 << EDIT_LAYER);
              drawFlags |= DRAW_CONTENT_FULL;
              drawLayersIndicator();

              DBG_LOG(printf("Changed layer to %i\n", newLayer));
            }
          }
        }
        break;

      case EDIT_NOTEON:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          // For mute automation: change timing
          if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
            LiveSequencer::MidiEvent& event = muteAutomationEvents.at(selectedMuteEvent);
            uint32_t time = getEventTime(event);
            uint32_t newTime = time + (e.dir * e.speed * 10); // Small timing adjustments
            if (newTime != time) {
              setEventTime(event, newTime);
              updateSongEvent(event);
              headerFlags |= (1 << EDIT_NOTEON);
              drawFlags |= DRAW_MUTE_AUTOMATION;
            }
          }
        }
        else {
          if (selectedNote > -1) {
            LiveSequencer::MidiEvent& on = notePairs.at(selectedNote).noteOn;
            LiveSequencer::MidiEvent& off = notePairs.at(selectedNote).noteOff;

            uint32_t oldNoteOnTime, noteOffTime;

            if (data.isSongMode && !data.useGridSongMode) {
              // Use absolute timing for song mode
              uint16_t actualBar = getActualBarForNote(notePairs.at(selectedNote));
              oldNoteOnTime = uint32_t(actualBar) * data.patternLengthMs + on.patternMs;
              noteOffTime = uint32_t(actualBar) * data.patternLengthMs + off.patternMs;

              DBG_LOG(printf("EDIT_NOTEON - Song Mode: actualBar=%i, relTime=%i, absTime=%i\n",
                actualBar, on.patternMs, oldNoteOnTime));
            }
            else {
              // Pattern mode - use relative timing
              oldNoteOnTime = getEventTime(on);
              noteOffTime = getEventTime(off);
            }

            uint32_t time = oldNoteOnTime;
            const uint32_t gridStep = data.patternLengthMs / 16;

            const uint32_t minTime = 0;
            const uint32_t maxTime = noteOffTime - gridStep;

            // Snap to grid
            int32_t gridSteps = roundf(float(time) / float(gridStep));
            gridSteps = constrain(gridSteps + e.dir * e.speed,
              roundf(float(minTime) / float(gridStep)),
              roundf(float(maxTime) / float(gridStep)));
            uint32_t newTime = gridSteps * gridStep;

            if (newTime != time) {
              // Create update region for OLD position (to clear it properly)
              UpdateRange oldRegion;
              const uint16_t oldX = getNoteCoord(oldNoteOnTime);
              const uint32_t oldLenMs = (noteOffTime > oldNoteOnTime) ? (noteOffTime - oldNoteOnTime) : 1;
              const int oldW = std::max(1, int(roundf(oldLenMs * view.msToPix)));
              oldRegion.fromX = (oldX < 2 + ROLL_WIDTH) ? ROLL_WIDTH : (oldX - 2);
              oldRegion.toX = (oldX + oldW > DISPLAY_WIDTH - 2) ? DISPLAY_WIDTH : (oldX + oldW + 2);
              oldRegion.notes = { on.note_in };
              drawRegionsX.push_back(oldRegion);

              // Update the note time
              if (data.isSongMode && !data.useGridSongMode) {
                // Convert absolute time back to pattern-relative for storage
                uint16_t newActualBar = newTime / data.patternLengthMs;
                uint32_t newPatternMs = newTime % data.patternLengthMs;

                DBG_LOG(printf("EDIT_NOTEON - Updating: newAbsTime=%i, newBar=%i, newPatternMs=%i\n",
                  newTime, newActualBar, newPatternMs));

                // Update the event in songEvents
                on.patternNumber = newActualBar; // Store absolute bar number
                on.patternMs = newPatternMs;
                updateSongEvent(on);

                // Also update the noteOff event to maintain consistency
                uint32_t noteLength = noteOffTime - oldNoteOnTime;
                off.patternNumber = newActualBar;
                off.patternMs = newPatternMs + noteLength;
                updateSongEvent(off);
              }
              else {
                setEventTime(on, newTime);
                liveSeq.requestSortEvents();
              }

              memcpy(&currentNote, &on, sizeof(on));

              // Queue redraw of NEW position
              queueDrawNote(notePairs.at(selectedNote));

              headerFlags |= (1 << EDIT_NOTEON);
              // Redraw background AND notes to ensure proper cleanup
              drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
            }
          }
        }
        break;

      case EDIT_NOTEOFF:
        if (selectedNote > -1) {
          LiveSequencer::MidiEvent& on = notePairs.at(selectedNote).noteOn;
          LiveSequencer::MidiEvent& off = notePairs.at(selectedNote).noteOff;

          const bool isNoteOn = editMode == EDIT_NOTEON;
          const uint32_t noteOnTime = getEventTime(on);
          const uint32_t oldNoteOffTime = getEventTime(off);

          uint32_t time = isNoteOn ? noteOnTime : oldNoteOffTime;
          const uint32_t gridStep = data.patternLengthMs / 16;

          // Ensure minimum note length of 1/16 step
          const uint32_t minTime = isNoteOn ? 0 : noteOnTime + gridStep;
          const uint32_t maxTime = isNoteOn ? oldNoteOffTime - gridStep : uint32_t(data.numberOfBars) * data.patternLengthMs;

          // Snap to grid
          int32_t gridSteps = roundf(float(time) / float(gridStep));
          gridSteps = constrain(gridSteps + e.dir * e.speed,
            roundf(float(minTime) / float(gridStep)),
            roundf(float(maxTime) / float(gridStep)));
          uint32_t newTime = gridSteps * gridStep;

          if (newTime != time) {
            // Create update region for OLD position (to clear it properly)
            UpdateRange oldRegion;
            const uint16_t oldX = getNoteCoord(noteOnTime);
            const uint32_t oldLenMs = (oldNoteOffTime > noteOnTime) ? (oldNoteOffTime - noteOnTime) : 1;
            const int oldW = std::max(1, int(roundf(oldLenMs * view.msToPix)));
            oldRegion.fromX = (oldX < 2 + ROLL_WIDTH) ? ROLL_WIDTH : (oldX - 2);
            oldRegion.toX = (oldX + oldW > DISPLAY_WIDTH - 2) ? DISPLAY_WIDTH : (oldX + oldW + 2);
            oldRegion.notes = { on.note_in };
            drawRegionsX.push_back(oldRegion);

            if (isNoteOn) {
              setEventTime(on, newTime);
              memcpy(&currentNote, &on, sizeof(on));
              if (data.isSongMode && !data.useGridSongMode) {
                updateSongEvent(on);
              }
              else {
                liveSeq.requestSortEvents();
              }
              headerFlags |= (1 << EDIT_NOTEON);
            }
            else {
              setEventTime(off, newTime);
              if (data.isSongMode && !data.useGridSongMode) {
                updateSongEvent(off);
              }
              headerFlags |= (1 << EDIT_NOTEOFF);
            }

            // Queue redraw of NEW position
            queueDrawNote(notePairs.at(selectedNote));

            // Redraw background AND notes to ensure proper cleanup
            drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
          }
        }
        break;

      case EDIT_VELOCITY:
        if (selectedNote > -1) {
          LiveSequencer::MidiEvent& event = notePairs.at(selectedNote).noteOn;
          int8_t velo = notePairs.at(selectedNote).noteOn.note_in_velocity;
          if (hasConstrainedChanged(velo, e.dir * e.speed, int8_t(0), int8_t(127))) {
            event.note_in_velocity = velo;
            if (data.isSongMode && !data.useGridSongMode) {
              updateSongEvent(event);
              reloadNotes();
            }
            headerFlags |= (1 << EDIT_VELOCITY);
          }
        }
        break;

      case EDIT_NOTE:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          // For mute automation: toggle mute state
          if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
            LiveSequencer::MidiEvent& event = muteAutomationEvents.at(selectedMuteEvent);
            if (e.dir != 0) {
              event.note_in = (event.note_in == LiveSequencer::TYPE_MUTE_ON) ? LiveSequencer::TYPE_MUTE_OFF : LiveSequencer::TYPE_MUTE_ON;
              updateSongEvent(event);
              headerFlags |= (1 << EDIT_NOTE);
              drawFlags |= DRAW_MUTE_AUTOMATION;
            }
          }
        }
        else if (selectedNote > -1) {
          LiveSequencer::NotePair& thisNote = notePairs.at(selectedNote);
          int8_t note = thisNote.noteOn.note_in;
          if (hasConstrainedChanged(note, e.dir, int8_t(view.lowestNote), int8_t(view.highestNote))) {
            uint8_t oldNote = thisNote.noteOn.note_in;

            thisNote.noteOn.note_in = note;
            thisNote.noteOff.note_in = note;
            headerFlags |= (1 << EDIT_NOTE);

            // Get precise coordinates for old and new note positions
            const uint32_t timeOn = getEventTime(thisNote.noteOn);
            const uint32_t timeOff = getEventTime(thisNote.noteOff);
            uint16_t fromX = getNoteCoord(timeOn);
            uint16_t toX = getNoteCoord(timeOff);

            fromX = (fromX < 2 + ROLL_WIDTH) ? ROLL_WIDTH : (fromX - 2);
            toX = (toX > DISPLAY_WIDTH - 2) ? DISPLAY_WIDTH : (toX + 2);

            // Queue redraw for BOTH old and new note rows with EMPTY notes set
            UpdateRange updateRange;
            updateRange.fromX = fromX;
            updateRange.toX = toX;
            updateRange.notes = { oldNote, static_cast<uint8_t>(note) };
            drawRegionsX.push_back(updateRange);

            // Set flags for complete redraw of this region
            drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
          }
        }
        break;
      } // end switch(editMode)
      break;

    case MODE_STEP:
      switch (stepMode) {
  case STEP_RECORD:
  // Change record/advance position
  if (hasConstrainedChanged(stepCursorPositionIndex, e.dir * e.speed, 
                           uint16_t(0), uint16_t(view.numBars * stepRecordSteps - 1))) {
    // Store previous position
    stepCursorPositionPrev = stepCursorPosition;
    
    // Calculate new position
    stepCursorPosition = ROLL_WIDTH + round(stepCursorPositionIndex * stepRecordStepSizeMs * view.msToPix);
    
    // Queue redraw for both positions
    queueDrawNote({ stepCursorPositionPrev, stepCursorPositionPrev, {} });
    queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
    
    drawFlags |= DRAW_STEP_CURSOR | DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    headerFlags |= (1 << STEP_RECORD);
  }
  break;

      case STEP_SIZE:
        // Change step size (number of steps per bar)
        if (hasConstrainedChanged(stepRecordSteps, e.dir, uint8_t(1), uint8_t(32))) {
          stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);
          // Reset or adjust step cursor position when step size changes
          stepCursorPositionIndex = 0;
          stepCursorPosition = ROLL_WIDTH;
          drawFlags |= DRAW_STEP_CURSOR;
          headerFlags |= (1 << STEP_SIZE);
        }
        break;

      case STEP_LAYER:
        // Change recording layer
        if (hasConstrainedChanged(stepRecordLayer, e.dir, 
                                 int8_t(0), int8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1))) {
          headerFlags |= (1 << STEP_LAYER);
        }
        break;
      }
      break;
    } // end switch(mode)

    drawHeader(headerFlags);
  }
  else {
    // non-settings branch: switch views / modes
    uint8_t oldMode = 0;
    uint8_t newMode = 0;
    switch (mode) {
    case MODE_VIEW:
      oldMode = viewMode;
      newMode = constrain(viewMode + e.dir, 0, VIEW_NUM - 1);
      viewMode = newMode;
      buttonView->drawNow();
      break;

    case MODE_EDIT:
      oldMode = editMode;
      if (data.activeTrack == SONG_MODE_MUTE_TRACK)
      {
        newMode = constrain(editMode + e.dir, 0, EDIT_NUM - 1);
        //this is not pretty but without splitting up the whole editMode loop in 2 parts with new enum for automation, this is the best i can do.
        //automation editor has less paramater than noteeditor but i want to use the most meaningful case names for it.
        if (oldMode == 2 && newMode == 3)
          newMode = 5;
        else if (oldMode == 5 && newMode == 4)
          newMode = 2;
      }
      else {
        newMode = constrain(editMode + e.dir, 0, EDIT_NUM - 1);
      }
      editMode = newMode;
      buttonEdit->drawNow();
      break;

case MODE_STEP:
  // Store old step mode to detect changes
  uint8_t oldStepMode = stepMode;
  stepMode = constrain(stepMode + e.dir, 0, STEP_NUM - 1);
  
  if ((data.isRunning == false) && (stepMode == STEP_RECORD)) {
    drawFlags |= DRAW_STEP_CURSOR;
  }
  queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
  buttonStep->drawNow();
  
  // ADD: Redraw header when step mode changes (like view/edit modes do)
  if (oldStepMode != stepMode) {
    drawHeader(HEADER_DRAW_ALL);
  }
  break;
    }
    if (oldMode != newMode) {
      if (mode == MODE_VIEW || mode == MODE_EDIT) {
        drawHeader((1 << oldMode) | (1 << newMode));
      }
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::setStartOctave(int8_t num) {
  if (num == view.startOctave) return; // No change

  view.startOctave = num;
  view.lowestNote = 24 + view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
  reloadNotes();
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setNumOctaves(int8_t num) {
  view.numOctaves = num;
  view.numNotes = view.numOctaves * 12;
  view.noteHeight = CONTENT_HEIGHT / float(view.numNotes);
  view.lowestNote = 24 + view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
  // reloadNotes();
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setEventTime(LiveSequencer::MidiEvent& e, uint32_t time) {
  e.patternNumber = time / data.patternLengthMs;
  e.patternMs = time % data.patternLengthMs;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawHeader(uint16_t modeFlags) {
  display.setTextSize(1);
  char s[16] = { "\0" };

if (modeFlags == HEADER_DRAW_ALL) {
    display.console = true;
    display.fillRect(132, CONTENT_HEIGHT+4, DISPLAY_WIDTH, HEADER_HEIGHT-6, HEADER_BG_COLOR);
  }

  switch (mode) {
  case MODE_VIEW:
    if (modeFlags & (1 << VIEW_ZOOM_X)) {
      drawHeaderSetting(GRID.X[3], "ZOOM X", "", (viewMode == VIEW_ZOOM_X), false);
    }
    if (modeFlags & (1 << VIEW_SCROLL_X)) {
      drawHeaderSetting(GRID.X[4], "SCROLL X", "", (viewMode == VIEW_SCROLL_X), false);
    }
    if (modeFlags & (1 << VIEW_ZOOM_Y)) {
      drawHeaderSetting(GRID.X[3], "", "ZOOM Y ", false, (viewMode == VIEW_ZOOM_Y));
    }
    if (modeFlags & (1 << VIEW_SCROLL_Y)) {
      drawHeaderSetting(GRID.X[4], "", "SCROLL Y", false, (viewMode == VIEW_SCROLL_Y));
    }
    break;

  case MODE_EDIT:
    if (data.activeTrack == SONG_MODE_MUTE_TRACK && selectedMuteEvent > -1 && muteAutomationEvents.size() > 0) {
      // Draw mute automation header - always show when on mute track
      LiveSequencer::MidiEvent& m = muteAutomationEvents.at(selectedMuteEvent);

      // Highlight based on current edit mode selection
      if (modeFlags & (1 << EDIT_LAYER)) {
        sprintf(s, "%i", m.note_in_velocity + 1);
        drawHeaderSetting(GRID.X[2] + 25, "LAYR", s, false, (editMode == EDIT_LAYER));
      }
      if (modeFlags & (1 << EDIT_SELECT_NOTE)) {
        sprintf(s, "%03i", selectedMuteEvent + 1);
        drawHeaderSetting(GRID.X[3], "SELECT", s, (editMode == EDIT_SELECT_NOTE), false);
      }

      if (modeFlags & (1 << EDIT_NOTEON)) {
        if (data.isSongMode && !data.useGridSongMode) {
          sprintf(s, "/ %i.%04i", m.patternNumber, m.patternMs);
        }
        else {
          sprintf(s, "/ %i.%04i", m.patternNumber, m.patternMs);
        }
        drawHeaderSetting(GRID.X[4], s, "", (editMode == EDIT_NOTEON), false);
      }
      if (modeFlags & (1 << EDIT_NOTE)) {
        sprintf(s, "%s", m.note_in == LiveSequencer::TYPE_MUTE_ON ? "MUTE ON " : "MUTE OFF");
        drawHeaderSetting(GRID.X[4], "", s, false, (editMode == EDIT_NOTE));
      }
    }
    else if (selectedNote > -1) {
      LiveSequencer::NotePair& p = notePairs.at(selectedNote);
      std::string noteString = getNoteString(p.noteOn.note_in);
      if (noteString.length() == 2) {
        noteString.append(" ");
      }

      if (modeFlags & (1 << EDIT_SELECT_NOTE)) {
        // Count only notes in the current layer (both song mode and pattern mode)
        int8_t currentLayer = p.noteOn.layer;
        int16_t layerNoteIndex = 1;
        int16_t totalLayerNotes = 0;

        for (size_t i = 0; i < notePairs.size(); i++) {
          if (notePairs[i].noteOn.layer == currentLayer) {
            totalLayerNotes++;
            if (i < static_cast<size_t>(selectedNote)) {
              layerNoteIndex++;
            }
          }
        }
        sprintf(s, "%03i/%03i", layerNoteIndex, totalLayerNotes);
        drawHeaderSetting(GRID.X[3], "SELECT", s, false, (editMode == EDIT_SELECT_NOTE));
      }
      if (modeFlags & (1 << EDIT_LAYER)) {
        sprintf(s, "%i", p.noteOn.layer + 1);
        drawHeaderSetting(GRID.X[2] + 25, "LAYR", s, false, (editMode == EDIT_LAYER));
      }
      if (modeFlags & (1 << EDIT_NOTEON)) {
        if (data.isSongMode && !data.useGridSongMode) {
          uint16_t actualBar = getActualBarForNote(p);
          sprintf(s, "/ %i.%04i", actualBar, p.noteOn.patternMs);
        }
        else {
          sprintf(s, "/ %i.%04i", p.noteOn.patternNumber, p.noteOn.patternMs);
        }
        drawHeaderSetting(GRID.X[4], s, "", (editMode == EDIT_NOTEON), false);
      }
      if (modeFlags & (1 << EDIT_NOTEOFF)) {
        if (data.isSongMode && !data.useGridSongMode) {
          uint16_t actualBar = getActualBarForNote(p);
          sprintf(s, "\\ %i.%04i", actualBar, p.noteOff.patternMs);
        }
        else {
          sprintf(s, "\\ %i.%04i", p.noteOff.patternNumber, p.noteOff.patternMs);
        }
        drawHeaderSetting(GRID.X[4], "", s, false, (editMode == EDIT_NOTEOFF));
      }
      if (modeFlags & (1 << EDIT_VELOCITY)) {
        sprintf(s, "%03u", p.noteOn.note_in_velocity);
        drawHeaderSetting(GRID.X[5], "VEL", s, false, (editMode == EDIT_VELOCITY));
      }
      if (modeFlags & (1 << EDIT_NOTE)) {
        sprintf(s, "%s", noteString.c_str());
        drawHeaderSetting(GRID.X[5] + 25, "NOTE", s, false, (editMode == EDIT_NOTE));
      }
    }
    break;

case MODE_STEP:
  // Always show all step modes, highlight the active one (like view/edit modes)
  if (modeFlags & (1 << STEP_RECORD)) {
    const uint8_t pattern = (stepCursorPositionIndex / stepRecordSteps);
    sprintf(s, "P%i %02i/%02i", pattern, stepCursorPositionIndex % stepRecordSteps, stepRecordSteps);
    // CHANGE: Remove the isSettingActivated check - highlight based on stepMode only
    drawHeaderSetting(GRID.X[3], "REC/ADV", s, false, (stepMode == STEP_RECORD));
  }
  if (modeFlags & (1 << STEP_SIZE)) {
    sprintf(s, "1/%02i", stepRecordSteps);
    // CHANGE: Remove the isSettingActivated check - highlight based on stepMode only
    drawHeaderSetting(GRID.X[4], "STEPSIZE", s, false, (stepMode == STEP_SIZE));
  }
  if (modeFlags & (1 << STEP_LAYER)) {
    sprintf(s, "%i", stepRecordLayer + 1);
    // CHANGE: Remove the isSettingActivated check - highlight based on stepMode only
    drawHeaderSetting(GRID.X[5], "TO LAYER", s, false, (stepMode == STEP_LAYER));
  }
  break;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawHeaderSetting(uint16_t x, const char* topText, const char* bottomText, bool topHighlighted, bool bottomHighlighted) {
  if (topText != 0) {
    setIsHighlighted(topHighlighted);
    display.setCursor(x, CONTENT_HEIGHT + 4);
    display.print(topText);
  }
  if (bottomText != 0) {
    setIsHighlighted(bottomHighlighted);
    display.setCursor(x, CONTENT_HEIGHT + 6 + CHAR_height_small);
    display.print(bottomText);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::setIsHighlighted(bool isHighlighted) {
  const uint16_t textColor = isHighlighted ? (isSettingActivated ? COLOR_SYSTEXT : HEADER_BG_COLOR) : COLOR_SYSTEXT;
  const uint16_t bgColor = isHighlighted ? (isSettingActivated ? RED : COLOR_SYSTEXT) : HEADER_BG_COLOR;
  display.setTextColor(textColor, bgColor);
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawPianoRoll() {
  const uint16_t numWhiteKeys = view.numOctaves * 7;
  const float keyHeight = CONTENT_HEIGHT / float(numWhiteKeys);

  display.console = true;
  display.fillRect(0, 0, ROLL_WIDTH, CONTENT_HEIGHT, COLOR_SYSTEXT);
  display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
  display.setTextSize(1);

  for (uint8_t i = 1; i < numWhiteKeys; i++) {
    const uint16_t y = round(i * keyHeight);
    display.console = true;
    display.drawLine(0, y, ROLL_WIDTH, y, COLOR_BACKGROUND);
  }

  for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
    const uint8_t noteIndex = view.highestNote - note - 1;
    const uint16_t y = ceil(noteIndex * view.noteHeight); // ceil looks best rounded
    switch (note % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
      display.console = true;
      display.fillRect(0, y, 12, view.noteHeight, COLOR_BACKGROUND);
      break;
    }
  }
}

FLASHMEM std::string UI_LiveSeq_PianoRoll::getNoteString(uint8_t note) {
  std::string result;
  static constexpr char notes[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  static constexpr char octs[10][2] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
  const uint8_t octave = (note - 24) / 12;
  result.append(notes[note % 12]);
  result.append(octs[octave]);
  return result;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawBarInfoOnTop(void) {
  display.console = true;
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setTextSize(1);

  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numBars * substeps);

  // Draw bar numbers and time information at the TOP of the screen
  for (uint8_t bar = 0; bar <= view.numBars; bar++) {
    const uint16_t x = ROLL_WIDTH + round(bar * stepWidth * substeps);

    if (bar < view.numBars) {
      const uint16_t displayBar = view.startBar + bar;

      // Draw bar number at the TOP (above the grid)
      char barInfo[16];
      if (data.isSongMode && !data.useGridSongMode) {
        // Song mode: show absolute bar number
        snprintf(barInfo, sizeof(barInfo), "%d", displayBar + 1);
      }
      else {
        // Pattern mode: show bar number within pattern
        snprintf(barInfo, sizeof(barInfo), "%d", (displayBar % data.numberOfBars) + 1);
      }

      // Position text at the top of the screen
      display.setCursor(x - 4, 5);
      display.print(barInfo);

      // Draw time information for every few bars to avoid clutter
      if (bar % 2 == 0 && view.numBars > 4) {
        const uint32_t barTimeMs = displayBar * data.patternLengthMs;
        const uint32_t barMinutes = barTimeMs / 60000;
        const uint32_t barSeconds = (barTimeMs % 60000) / 1000;
        const uint32_t barMillis = barTimeMs % 1000;

        char timeInfo[16];
        if (barTimeMs < 60000) {
          snprintf(timeInfo, sizeof(timeInfo), "%lu.%02lu",
            (unsigned long)barSeconds,
            (unsigned long)(barMillis / 10));
        }
        else {
          snprintf(timeInfo, sizeof(timeInfo), "%lu:%02lu",
            (unsigned long)barMinutes,
            (unsigned long)barSeconds);
        }

        display.setCursor(x - 8, 15);
        display.print(timeInfo);
      }
    }
  }

  // Current playback position indicator at the top
  if (data.isRunning) {
    const uint32_t currentTimeMs = data.currentPattern * data.patternLengthMs + data.patternTimer;
    const uint32_t currentMinutes = currentTimeMs / 60000;
    const uint32_t currentSeconds = (currentTimeMs % 60000) / 1000;
    const uint32_t currentMillis = currentTimeMs % 1000;

    char currentTime[16];
    if (currentTimeMs < 60000) {
      snprintf(currentTime, sizeof(currentTime), "%lu.%02lu",
        (unsigned long)currentSeconds,
        (unsigned long)(currentMillis / 10));
    }
    else {
      snprintf(currentTime, sizeof(currentTime), "%lu:%02lu",
        (unsigned long)currentMinutes,
        (unsigned long)currentSeconds);
    }

    // Draw current time in top-left corner
    display.setCursor(ROLL_WIDTH + 5, 5);
    display.printf("%s", currentTime);

    // Draw current bar info
    display.setCursor(ROLL_WIDTH + 5, 15);
    if (data.isSongMode && !data.useGridSongMode) {
      display.printf("BAR: %d", data.currentPattern + 1);
    }
    else {
      display.printf("BAR: %d", (data.currentPattern % data.numberOfBars) + 1);
    }
  }
}


FLASHMEM void UI_LiveSeq_PianoRoll::drawVerticalBars(void) {
  // Draw vertical pattern lines first
  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numBars * substeps);

  // Draw vertical lines (full height) - always draw regardless of note filter
  for (uint8_t step = 0; step < view.numBars * substeps; step++) {
    const uint16_t x = ROLL_WIDTH + round(step * stepWidth);
    const uint16_t lineColor = (step % substeps == 0) ? GREY3 : GREY4;

    for (auto& u : drawRegionsX) {
      const bool drawLine = ((x >= u.fromX) && (x <= u.toX));
      if (drawLine) {  // <-- REMOVED the u.notes.empty() check
        display.console = true;
        display.drawFastVLine(x, 0, CONTENT_HEIGHT, lineColor);
        break; // Only draw once per line
      }
    }
  }

  // Calculate white key height to match the keyboard
  const uint16_t numWhiteKeys = view.numOctaves * 7;
  const float whiteKeyHeight = CONTENT_HEIGHT / float(numWhiteKeys);

  // Draw horizontal lines - respect note filter for horizontal lines
  for (uint8_t whiteKey = 0; whiteKey <= numWhiteKeys; whiteKey++) {
    const uint16_t y = (uint16_t)(whiteKey * whiteKeyHeight + 0.5f);
    const uint16_t lineColor = (whiteKey % 7 == 0) ? GREY3 : GREY4;

    // Check if any update region wants this line drawn
    bool shouldDraw = false;
    for (auto& u : drawRegionsX) {
      if (u.notes.empty()) {
        shouldDraw = true;
        break;
      }
    }

    if (shouldDraw) {
      display.console = true;
      display.drawFastHLine(ROLL_WIDTH, y, CONTENT_WIDTH, lineColor);
    }
  }
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getNoteCoord(uint32_t time) const {
  uint32_t viewStartMs, viewEndMs;

  if (data.isSongMode && !data.useGridSongMode) {
    // Song mode: use 1:1 mapping (no 4:1 compression)
    viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
    viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);
  }
  else {
    // Pattern mode
    viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
    viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);
  }

  // Handle times before viewport
  if (time < viewStartMs) {
    return ROLL_WIDTH;
  }

  // Handle times after viewport  
  if (time > viewEndMs) {
    return DISPLAY_WIDTH - 1;
  }

  // Calculate position within viewport
  float ratio = float(time - viewStartMs) / float(viewEndMs - viewStartMs);
  uint16_t px = ROLL_WIDTH + roundf(ratio * CONTENT_WIDTH);

  // Clamp to display bounds
  if (px < ROLL_WIDTH) px = ROLL_WIDTH;
  if (px > DISPLAY_WIDTH - 1) px = DISPLAY_WIDTH - 1;

  return px;
}

FLASHMEM uint32_t UI_LiveSeq_PianoRoll::getRelativeNoteTime(uint32_t time) const {
  if (data.isSongMode && !data.useGridSongMode) {
    // Song mode: return time relative to viewport start
    // DO NOT clamp to viewport end - we need full time for length calculations
    const uint32_t startMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);

    if (time < startMs) {
      return 0;
    }

    return time - startMs;
  }
  else {
    // Pattern mode: original logic with clamping
    const int32_t startMs = int32_t(view.startBar) * int32_t(data.patternLengthMs);
    const int32_t endMs = (int32_t(view.startBar) + int32_t(view.numBars)) * int32_t(data.patternLengthMs);

    int32_t timeRel = int32_t(time) - startMs;
    if (timeRel < 0) timeRel = 0;
    if (timeRel > endMs) timeRel = endMs;

    return uint32_t(timeRel);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNotes(void) {
  // Always determine active layer for both song mode and pattern mode
  int8_t activeLayer = -1;

  if (data.activeTrack == SONG_MODE_NOTES_TRACK || data.activeTrack < SONG_MODE_NOTES_TRACK) {
    // For both song mode and pattern mode, filter by layer
    // Use the selected note's layer if available, regardless of mode
    if (selectedNote > -1 && selectedNote < int16_t(notePairs.size())) {
      activeLayer = notePairs.at(selectedNote).noteOn.layer;
    }
    else if (!notePairs.empty()) {
      // If no note selected but we have notes, use the first note's layer
      activeLayer = notePairs.at(0).noteOn.layer;
    }
    else {
      // Only default to 0 if there are no notes at all
      activeLayer = 0;
    }
  }

  // Early return if there are no notes to draw
  if (notePairs.empty()) {
    return;
  }

  // Three-pass drawing to ensure selected note is always on top

  // First pass: draw non-active layer notes in grey
  for (size_t i = 0; i < notePairs.size(); ++i) {
    if ((data.activeTrack == SONG_MODE_NOTES_TRACK || data.activeTrack < SONG_MODE_NOTES_TRACK) &&
      notePairs[i].noteOn.layer != activeLayer) {
      drawNote(notePairs[i], false, true); // true = draw in grey
    }
  }

  // Second pass: draw active layer notes in color (except selected note)
  for (size_t i = 0; i < notePairs.size(); ++i) {
    if ((data.activeTrack >= SONG_MODE_MUTE_TRACK || notePairs[i].noteOn.layer == activeLayer) &&
      selectedNote != int16_t(i)) {
      drawNote(notePairs[i], false, false);
    }
  }

  // Third pass: draw selected note last (on top)
  if (selectedNote > -1 && selectedNote < int16_t(notePairs.size())) {
    drawNote(notePairs.at(selectedNote), true, false);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNoteAtPosition(LiveSequencer::NotePair note, bool isSelected, uint16_t x, uint16_t w, bool drawInGrey) const {
  // Skip notes outside the visible octave range
  if (note.noteOn.note_in < view.lowestNote || note.noteOn.note_in >= view.highestNote) {
    return;
  }

  // Calculate Y position
  const uint8_t noteIndex = view.highestNote - note.noteOn.note_in - 1;
  const int16_t y = (int16_t)(noteIndex * view.noteHeight);
  const uint8_t h = view.noteHeight;

  // Ensure note is fully within content area
  if (y < 0 || y >= CONTENT_HEIGHT) {
    return;
  }

  // Ensure note doesn't overflow into header area
  if (y + h > CONTENT_HEIGHT) {
    return;
  }

  uint16_t color;
  if (drawInGrey) {
    color = GREY2; // Draw non-active layer notes in grey
  }
  else if (isSelected) {
    color = COLOR_SYSTEXT;
  }
  else {
    color = layerColors.at(note.noteOn.layer);
  }

  display.console = true;

  if (w > 1) {
    if (note.isMuted) {
      display.drawRect(x, y, w, h, color);
    }
    else {
      display.fillRect(x, y, w, h, color);
    }
  }
  else {
    display.drawLine(x, y, x, y + h - 1, color);
  }
}

FLASHMEM const uint32_t UI_LiveSeq_PianoRoll::getEventTime(LiveSequencer::MidiEvent e) const {
  return e.patternNumber * data.patternLengthMs + e.patternMs;
}

FLASHMEM bool UI_LiveSeq_PianoRoll::setMode(uint8_t newMode) {
  const uint8_t oldMode = mode;
  const bool modeChanged = oldMode != newMode;
  if (modeChanged) {
    isSettingActivated = false;
    mode = newMode;

    // leaving old mode actions
    switch (oldMode) {
    case MODE_VIEW:
      buttonView->drawNow();
      break;

    case MODE_EDIT:
      editMode = EDIT_LAYER;
      buttonEdit->drawNow();
      if (selectedNote > -1) {
        queueDrawNote(notePairs.at(selectedNote));
        drawFlags |= DRAW_NOTES;
        selectedNote = -1;
      }
      break;

   case MODE_STEP:
  // reset step cursor
  stepMode = STEP_RECORD;
  buttonStep->drawNow();
  queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
  stepCursorPositionIndex = 0;
  stepCursorPosition = ROLL_WIDTH;
  stepCursorPositionPrev = ROLL_WIDTH; // Initialize previous position
  drawFlags |= DRAW_CONTENT_AREA;
  break;
    }

    // entering new mode actions
    switch (newMode) {
    case MODE_VIEW:
      break;

    case MODE_EDIT:
      if ((selectedNote == -1) && notePairs.size()) {
        selectedNote = 0;
        queueDrawNote(notePairs.at(0));
        drawFlags |= DRAW_NOTES;
      }
      break;

    case MODE_STEP:
      // Initialize step record parameters
      stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);
      stepCursorPositionIndex = 0;
      stepCursorPosition = ROLL_WIDTH;
      drawFlags |= DRAW_STEP_CURSOR;
      break;
    }

    drawHeader(HEADER_DRAW_ALL);
  }
  return modeChanged;
}

FLASHMEM void UI_LiveSeq_PianoRoll::deleteSelectedNote(void) {
  // For mute automation track, delete works for any mute automation parameter
  bool isMuteAutomationMode = (data.activeTrack == SONG_MODE_MUTE_TRACK) &&
    (editMode == EDIT_SELECT_NOTE || editMode == EDIT_LAYER ||
      editMode == EDIT_NOTEON || editMode == EDIT_NOTE);

  if (isMuteAutomationMode && selectedMuteEvent > -1 && muteAutomationEvents.size() > 0) {
    LiveSequencer::MidiEvent eventToRemove = muteAutomationEvents.at(selectedMuteEvent);

    // Remove from songEvents
    for (auto& patternEntry : data.songEvents) {
      patternEntry.second.erase(
        std::remove_if(patternEntry.second.begin(), patternEntry.second.end(),
          [&eventToRemove](const LiveSequencer::MidiEvent& e) {
            return e.track == eventToRemove.track &&
              e.layer == eventToRemove.layer &&
              e.patternNumber == eventToRemove.patternNumber &&
              e.patternMs == eventToRemove.patternMs &&
              e.note_in == eventToRemove.note_in;
          }),
        patternEntry.second.end()
      );
    }

    // Remove from local list
    muteAutomationEvents.erase(muteAutomationEvents.begin() + selectedMuteEvent);

    // Update selection
    if (muteAutomationEvents.size() == 0) {
      selectedMuteEvent = -1;
      isSettingActivated = false;
      buttonEdit->drawNow();
    }
    else {
      selectedMuteEvent = std::min(selectedMuteEvent, (int16_t)(muteAutomationEvents.size() - 1));
    }

    // Use the same draw flags that were working before
    drawFlags |= DRAW_CONTENT_FULL | DRAW_MUTE_AUTOMATION;


  }
  else if (selectedNote > -1) {
    // Get the events to delete
    LiveSequencer::MidiEvent noteOnToDelete = notePairs.at(selectedNote).noteOn;
    LiveSequencer::MidiEvent noteOffToDelete = notePairs.at(selectedNote).noteOff;

    // Delete from songEvents for song mode
    if (data.isSongMode && !data.useGridSongMode) {
      for (auto& patternEntry : data.songEvents) {
        patternEntry.second.erase(
          std::remove_if(patternEntry.second.begin(), patternEntry.second.end(),
            [&noteOnToDelete, &noteOffToDelete](const LiveSequencer::MidiEvent& e) {
              // Match the specific note on and note off events
              bool isNoteOn = (e.track == noteOnToDelete.track &&
                e.layer == noteOnToDelete.layer &&
                e.patternNumber == noteOnToDelete.patternNumber &&
                e.patternMs == noteOnToDelete.patternMs &&
                e.note_in == noteOnToDelete.note_in &&
                e.event == midi::NoteOn);

              bool isNoteOff = (e.track == noteOffToDelete.track &&
                e.layer == noteOffToDelete.layer &&
                e.patternNumber == noteOffToDelete.patternNumber &&
                e.patternMs == noteOffToDelete.patternMs &&
                e.note_in == noteOffToDelete.note_in &&
                e.event == midi::NoteOff);

              return isNoteOn || isNoteOff;
            }),
          patternEntry.second.end()
        );
      }
    }
    else {
      // Pattern mode - mark as invalid
      notePairs.at(selectedNote).noteOn.event = midi::InvalidType;
      notePairs.at(selectedNote).noteOff.event = midi::InvalidType;
    }

    // Store previous selection
    int16_t prevSelectedNote = selectedNote;

    // Reload notes from songEvents (or pattern events)
    reloadNotes();

    const int16_t notesCount = notePairs.size();
    if (notesCount == 0) {
      selectedNote = -1;
      isSettingActivated = false;
      buttonEdit->drawNow();
    }
    else {
      selectedNote = std::min(prevSelectedNote, int16_t(notesCount - 1));
    }

    // Only set draw flags, don't call drawGUI() - let the main loop handle it
    drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
  }
}

FLASHMEM uint8_t UI_LiveSeq_PianoRoll::getMaxTrackForCurrentMode() const {
  return MAX_TRACKS - 1; // 0-13 for all tracks
}

FLASHMEM void UI_LiveSeq_PianoRoll::initGUI(void) {
  const uint16_t y = CONTENT_HEIGHT + HEADER_HEIGHT + LINE_HEIGHT;

  buttonPlay = new TouchButton(GRID.X[0], y,
    [this](auto* b) { // drawHandler
      const TouchButton::Color playColor = data.isRunning ? TouchButton::BUTTON_RED : TouchButton::BUTTON_ACTIVE;
      b->draw((data.isRunning ? "STOP" : "START"), "", playColor);
    }, [this](auto* b) { // clickedHandler
      const bool isRunning = data.isRunning;
      if (isRunning) {
        liveSeq.stop();
      }
      else {
        liveSeq.start();
      }
      if (mode == MODE_STEP && stepMode == STEP_RECORD) {
        // hide or draw step cursor
        drawFlags |= isRunning ? DRAW_STEP_CURSOR : DRAW_CONTENT_AREA;
        ui_pianoroll->queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
      }
      });
  buttons.push_back(buttonPlay);

  // Updated track button with new cycling behavior
  buttons.push_back(new TouchButton(GRID.X[1], y,
    [this](auto* b) { // drawHandler
      char temp_char[4];
      if (data.activeTrack >= SONG_MODE_NOTES_TRACK) {
        // Song mode tracks
        if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
          b->draw("SONG", "NOTES", TouchButton::BUTTON_ACTIVE);
          TouchButton::drawButton(GRID.X[2], GRID.Y[5], "", "", TouchButton::BUTTON_INACTIVE);
        }
        else {
          b->draw("SONG", "MUTES", TouchButton::BUTTON_ACTIVE);
          TouchButton::drawButton(GRID.X[2], GRID.Y[5], "", "", TouchButton::BUTTON_INACTIVE);
        }
      }
      else {
        // Pattern tracks
        itoa(data.activeTrack + 1, temp_char, 10);
        b->draw("PATTERN", temp_char, TouchButton::BUTTON_ACTIVE);
        const uint8_t device = data.trackSettings[data.activeTrack].device;
        const uint8_t instrument = data.trackSettings[data.activeTrack].instrument;
        char name[10];
        char sub[10];
        liveSeq.getInstrumentName(device, instrument, name, sub);
        TouchButton::drawButton(GRID.X[2], GRID.Y[5], name, sub, TouchButton::BUTTON_ACTIVE);
      }
    }, [this](auto* b) { // clickedHandler
      // Cycle through all tracks: 12 patterns + 2 song mode tracks
      uint8_t newTrack = data.activeTrack + 1;
      if (newTrack >= MAX_TRACKS) {
        newTrack = 0;
      }
      // Clear screen and reset state
      display.console = true;
      display.fillRect(ROLL_WIDTH, 0, CONTENT_WIDTH, CONTENT_HEIGHT, COLOR_BACKGROUND);

      // Update track and mode
      data.activeTrack = newTrack;

      // Set song mode flag based on track selection
      if (newTrack >= SONG_MODE_NOTES_TRACK) {
        data.isSongMode = true;
        data.useGridSongMode = false;
      }
      else {
        data.isSongMode = false;
      }

      // Reset edit mode state
      if (mode == MODE_EDIT) {
        isSettingActivated = false;
        editMode = EDIT_LAYER;
        selectedMuteEvent = -1;
      }

      // Clear and reload
      notePairs.clear();
      muteAutomationEvents.clear();
      selectedNote = -1;
      selectedMuteEvent = -1;

      ui_pianoroll->reloadNotes(true);

      // Set appropriate selection
      if (data.activeTrack == SONG_MODE_MUTE_TRACK && muteAutomationEvents.size() > 0) {
        selectedMuteEvent = 0;
      }
      else if (notePairs.size() > 0) {
        selectedNote = 0;
      }

      // Update UI
      drawFlags |= DRAW_CONTENT_FULL;
      if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
        drawFlags |= DRAW_MUTE_AUTOMATION;
      }
      else {
        drawFlags |= DRAW_PIANOROLL;
      }

      buttonEdit->drawNow();
      ui_pianoroll->drawHeader(HEADER_DRAW_ALL);
      ui_pianoroll->drawLayersIndicator();
      liveSeq.setActiveTrack(data.activeTrack);
      b->drawNow();
      }));


  //pattern mode

  buttonInst = new TouchButton(GRID.X[2], y,
    [this](auto* b) { // drawHandler
      if (data.activeTrack == SONG_MODE_MUTE_TRACK)
        ;
      else if (data.activeTrack == SONG_MODE_NOTES_TRACK)
        ;
      else
      {
        const uint8_t device = data.trackSettings[data.activeTrack].device;
        const uint8_t instrument = data.trackSettings[data.activeTrack].instrument;
        char name[10];
        char sub[10];
        liveSeq.getInstrumentName(device, instrument, name, sub);
        b->draw(name, sub, TouchButton::BUTTON_ACTIVE);
      }
    }, [this](auto* b) { // clickedHandler

      if (data.activeTrack != SONG_MODE_MUTE_TRACK && data.activeTrack != SONG_MODE_NOTES_TRACK)
      {
        // open instrument settings
        if (data.tracks[data.activeTrack].screenSetupFn != nullptr) {
          SetupFn f = (SetupFn)data.tracks[data.activeTrack].screenSetupFn;
          f(0);
        }
        UI_LiveSequencer::openScreen(data.tracks[data.activeTrack].screen);
      }
      });

  buttons.push_back(buttonInst);

  buttonView = new TouchButton(GRID.X[3], y,
    [this](auto* b) { // drawHandler
      b->draw("VIEW", "MODE", (mode == MODE_VIEW) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
    }, [this](auto* b) { // clickedHandler
      ui_pianoroll->setMode(MODE_VIEW);
      b->drawNow();
      });
  buttons.push_back(buttonView);

  buttonEdit = new TouchButton(GRID.X[4], y,
    [this](auto* b) { // drawHandler
      std::string txt = "EDIT";
      std::string sub = "MODE";
      TouchButton::Color color = (mode == MODE_EDIT) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;

      if (mode == MODE_EDIT && isSettingActivated) {
        // For mute automation track: show DELETE for ANY parameter (SEL, LAYR, Time, Mute State)
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {

          txt = "MUTE";
          sub = "DELETE";
          color = TouchButton::BUTTON_RED;

        }
        // For pattern mode: show DELETE only when in SELECT_NOTE mode
        else if (editMode == EDIT_SELECT_NOTE) {
          txt = "NOTE";
          sub = "DELETE";
          color = TouchButton::BUTTON_RED;
        }
      }

      b->draw(txt.c_str(), sub.c_str(), color);
    },
    [this](auto* b) { // clickedHandler
      ui_pianoroll->setMode(MODE_EDIT);

      // Check if we should delete
      if (isSettingActivated) {
        // For mute automation track: delete works for any parameter
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          ui_pianoroll->deleteSelectedNote();
        }
        // For pattern mode: delete only works when in SELECT_NOTE mode
        else if (editMode == EDIT_SELECT_NOTE) {
          ui_pianoroll->deleteSelectedNote();
        }
      }
      b->drawNow();
    });

  buttons.push_back(buttonEdit);

  buttonStep = new TouchButton(GRID.X[5], y,
    [this](auto* b) { // drawHandler
      TouchButton::Color color = (mode == MODE_STEP) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;
      if (data.isRunning) {
        color = TouchButton::BUTTON_NORMAL;
      }
      b->draw("STEP", "RECORD", color);
    },
    [this](auto* b) { // clickedHandler
      if (data.isRunning == false) {
        const bool modeChanged = ui_pianoroll->setMode(MODE_STEP);
        if ((modeChanged == false) && (stepMode == STEP_RECORD)) {
          ui_pianoroll->step(+1);
        }
        b->drawNow();
      }
    });
  buttons.push_back(buttonStep);
}

FLASHMEM bool UI_LiveSeq_PianoRoll::step(int8_t diff) {
  const bool stepCursorChanged = hasConstrainedChanged(stepCursorPositionIndex, diff, uint16_t(0), uint16_t(view.numBars * stepRecordSteps - 1));
  
  if (stepCursorChanged) {
    if (stepCursorPositionIndex == view.numBars * stepRecordSteps) {
      stepCursorPositionIndex = 0;
    }
    
    // Store previous position for clearing
    stepCursorPositionPrev = stepCursorPosition;
    
    // Calculate new position
    stepCursorPosition = ROLL_WIDTH + round(stepCursorPositionIndex * stepRecordStepSizeMs * view.msToPix);
    
    // Queue redraw for BOTH old and new cursor positions
    queueDrawNote({ stepCursorPositionPrev, stepCursorPositionPrev, {} });
    queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
    
    drawFlags |= DRAW_STEP_CURSOR | DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    drawHeader(1 << STEP_RECORD);
  }
  return stepCursorChanged;
}
