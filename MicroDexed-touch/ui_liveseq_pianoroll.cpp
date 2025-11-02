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

    // Look for matching event by content
    for (const auto& event : events) {
      if (event.track == note.noteOn.track &&
        event.layer == note.noteOn.layer &&
        event.note_in == note.noteOn.note_in &&
        event.event == note.noteOn.event &&
        event.patternMs == note.noteOn.patternMs &&
        event.note_in_velocity == note.noteOn.note_in_velocity) {
        // Found it! Calculate absolute bar
        return (songPatternIndex * data.numberOfBars) + event.patternNumber;
      }
    }
  }

  // Fallback: use the pattern number from the note (for editing context)
  return note.noteOn.patternNumber;
}


FLASHMEM uint32_t UI_LiveSeq_PianoRoll::getPatternRelativeTime(const LiveSequencer::MidiEvent& e) const {
  if (data.isSongMode && !data.useGridSongMode) {
    // Relative to 4-bar cycle: bar index * barLength + time within that bar
    return (e.patternNumber * data.patternLengthMs) + e.patternMs;
  }
  else {
    return getEventTime(e);
  }
}


FLASHMEM void UI_LiveSeq_PianoRoll::setPatternRelativeTime(LiveSequencer::MidiEvent& e, uint32_t time, uint16_t songPatternIndex) {
  if (data.isSongMode && !data.useGridSongMode) {
    // time is 0..(4*barLength)
    uint32_t absoluteTime = (songPatternIndex * data.numberOfBars * data.patternLengthMs) + time;

    e.patternNumber = (absoluteTime / data.patternLengthMs) % data.numberOfBars; // 0..3
    e.patternMs = absoluteTime % data.patternLengthMs;                       // offset inside bar
  }
  else {
    setEventTime(e, time);
  }
}


FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getSongPatternForEvent(const LiveSequencer::MidiEvent& e) const {
  if (!data.isSongMode || data.useGridSongMode) {
    return 0;
  }

  // Find which song pattern this event belongs to
  for (const auto& patternEntry : data.songEvents) {
    const uint16_t songPatternIndex = patternEntry.first;
    const auto& events = patternEntry.second;

    for (const auto& event : events) {
      if (event.track == e.track &&
        event.layer == e.layer &&
        event.note_in == e.note_in &&
        event.event == e.event &&
        event.patternMs == e.patternMs &&
        event.note_in_velocity == e.note_in_velocity) {
        return songPatternIndex;
      }
    }
  }
  return 0;
}

FLASHMEM UI_LiveSeq_PianoRoll::UI_LiveSeq_PianoRoll(LiveSequencer& sequencer, LiveSequencer::LiveSeqData& d) :
  liveSeq(sequencer),
  data(d),
  isRunningHere(d.isRunning),
  selectedLayer(0) {
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


  if (data.activeTrack < SONG_MODE_NOTES_TRACK) {
  // Pattern mode: show layers for current pattern track
  for (uint8_t l = 0; l < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; l++) {
    const uint16_t x = GRID.X[0] + (l % 2) * (layerViewSizeX);
    const uint16_t y = layerViewY + (l / 2) * (HEADER_HEIGHT / 2);
    const bool isMuted = data.tracks[data.activeTrack].layerMutes & (1 << l);
    const bool hasNotes = data.trackSettings[data.activeTrack].layerCount > l;
    
    // ADD THIS: Check if this is the active layer (for filtering)
    bool isActiveLayer = (l == selectedLayer);
    
    display.console = true;
    if (hasNotes) {
      if (isMuted) {
        display.fillRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), COLOR_BACKGROUND);
        display.drawRect(x, y, layerViewSizeX, (HEADER_HEIGHT / 2), layerColors[l]);
        display.setTextColor(layerColors[l], COLOR_BACKGROUND);
      }
      else if (isActiveLayer) {
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
     bool isActiveLayer = (l == selectedLayer);
      // if (selectedNote > -1) {
      //   isActiveLayer = (notePairs.at(selectedNote).noteOn.layer == l);
      // }

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
  // For song mode, events are stored inside data.songEvents map keyed by songPatternIndex.
  // We must find the stored event and use the map key to compute the true absolute time.
  if (data.isSongMode && !data.useGridSongMode) {
    for (const auto& patternEntry : data.songEvents) {
      const uint16_t songPatternIndex = patternEntry.first;
      for (const auto& ev : patternEntry.second) {
        if (ev.track == e.track &&
          ev.layer == e.layer &&
          ev.note_in == e.note_in &&
          ev.event == e.event &&
          ev.patternMs == e.patternMs &&
          ev.note_in_velocity == e.note_in_velocity) {
          // absolute bar = (mapKey * numberOfBars) + cyclic patternNumber
          const uint32_t absoluteBar = uint32_t(songPatternIndex) * data.numberOfBars + ev.patternNumber;
          return absoluteBar * data.patternLengthMs + ev.patternMs;
        }
      }
    }
  }

  // Fallback: treat patternNumber as an absolute bar index (pattern-mode-like).
  return uint32_t(e.patternNumber) * data.patternLengthMs + e.patternMs;
}



FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(UpdateRange coords) {
  drawRegionsX.push_back(coords);
  drawFlags |= DRAW_NOTES;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note, bool isSelected, bool drawInGrey) const {
  uint32_t timeOn, timeOff;
  uint16_t x;
  int w;

  if (data.isSongMode && !data.useGridSongMode) {
    // SONG MODE: Handle boundary crossings properly
    // Use the existing getAbsoluteEventTime function which should handle this correctly
    timeOn = getAbsoluteEventTime(note.noteOn);
    timeOff = getAbsoluteEventTime(note.noteOff);
    
    // Ensure timeOff is always after timeOn
    if (timeOff <= timeOn) {
      timeOff = timeOn + data.patternLengthMs / 16; // Default to 1/16 note length
    }
    
    x = getNoteCoord(timeOn);
    
    // Calculate note length in viewport pixels
    uint32_t lenMs = timeOff - timeOn;
    w = std::max(1, int(roundf(lenMs * view.msToPix)));
    
    // Clamp the end position to prevent drawing beyond the display
    uint16_t endX = x + w;
    if (endX > DISPLAY_WIDTH) {
      w = DISPLAY_WIDTH - x;
    }
    
    // Ensure minimum visible width for very short notes
    if (w < 1) w = 1;
  }
  else {
    // PATTERN MODE: Original working calculation
    timeOn = getEventTime(note.noteOn);
    timeOff = getEventTime(note.noteOff);

    x = getNoteCoord(timeOn);
    const uint32_t lenMs = (timeOff > timeOn) ? (timeOff - timeOn) : 1;
    w = std::max(1, int(roundf(lenMs * view.msToPix)));
  }

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

  if ((data.isSongMode && !data.useGridSongMode) || data.activeTrack == SONG_MODE_MUTE_TRACK) {
    // Song mode and mute automation: use 1:1 bar mapping (no 4-bar compression)
    totalTimeMs = uint32_t(maxBars) * data.patternLengthMs;
    viewTimeMs = uint32_t(view.numBars) * data.patternLengthMs;
    startTimeMs = uint32_t(view.startBar) * data.patternLengthMs;
  }
  else {
    // Pattern mode
    totalTimeMs = uint32_t(maxBars) * data.patternLengthMs;
    viewTimeMs = uint32_t(view.numBars) * data.patternLengthMs;
    startTimeMs = uint32_t(view.startBar) * data.patternLengthMs;
  }

  if (totalTimeMs == 0) return;

  const uint16_t x = ROLL_WIDTH + 1 + roundf((float)wTotal * (float)startTimeMs / (float)totalTimeMs);

  // Calculate height and Y position - full height for mute automation
  uint16_t h, y;
  if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    // Mute automation: full height (no Y scrolling/zooming)
    h = hTotal;
    y = layerViewY + 1;
  }
  else {
    // Pattern/song mode: normal height based on octave view
    h = roundf(hTotal * view.numOctaves / float(TOTAL_OCTAVES));
    y = layerViewY + 1 + hTotal - h -
      roundf(hTotal * view.startOctave / float(TOTAL_OCTAVES));
  }

  int w = int(roundf((float)wTotal * (float)viewTimeMs / (float)totalTimeMs));
  if (w < 1) w = 1;
  if (x + w > ROLL_WIDTH + wTotal) w = (ROLL_WIDTH + wTotal) - x;

  display.fillRect(x, y, uint16_t(w), h, MIDDLEGREEN);

  // EXTENDED: Clear text areas with background color before drawing new text
  display.console = true;
  display.fillRect(GRID.X[1], layerViewY + 2, 80, CHAR_height_small, COLOR_BACKGROUND);
  display.console = true;
  display.fillRect(GRID.X[1], layerViewY + CHAR_height_small + 4, 80, CHAR_height_small, COLOR_BACKGROUND);

  // Show bar info
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setTextSize(1);
  display.setCursor(GRID.X[1], layerViewY + 3);

  if ((data.isSongMode && !data.useGridSongMode) || data.activeTrack == SONG_MODE_MUTE_TRACK) {
    // Song mode and mute automation: show absolute bar numbers
    uint16_t startBar = view.startBar + 1;
    uint16_t endBar = view.startBar + view.numBars;
    display.printf("BAR:%i-%i/%i", startBar, endBar, maxBars);
  }
  else {
    // Pattern mode: show relative bar numbers
    display.printf("BAR:%i-%i", view.startBar + 1, view.startBar + view.numBars);
  }

  display.setCursor(GRID.X[1], layerViewY + CHAR_height_small + 5);

  // Show octave info only for pattern/song notes, not for mute automation
  if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    //display.print("");
    ;
  }
  else {
    display.printf("OCT:%i-%i", view.startOctave, view.startOctave + view.numOctaves - 1);
  }
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

  // Use the same timing calculation as drawNote
  uint32_t timeOn, timeOff;
  if (data.isSongMode && !data.useGridSongMode) {
    // SONG MODE: Use getAbsoluteEventTime for consistent boundary handling
    timeOn = getAbsoluteEventTime(note.noteOn);
    timeOff = getAbsoluteEventTime(note.noteOff);
    
    if (timeOff <= timeOn) {
      timeOff = timeOn + data.patternLengthMs / 16;
    }
  }
  else {
    // PATTERN MODE: Use original working calculation
    timeOn = getEventTime(note.noteOn);
    timeOff = getEventTime(note.noteOff);
  }

  coords.fromX = getNoteCoord(timeOn);
  coords.toX = getNoteCoord(timeOff);
  coords.notes = { note.noteOn.note_in };

  // Add padding for redraw regions, ensuring we don't go out of bounds
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

    if (param != 0xFF) {
      data.activeTrack = param;
      view.startBar = 0;
      view.numBars = 4;
      view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
    }

    //    else  {
    //      data.activeTrack = SONG_MODE_NOTES_TRACK;
    //     uint16_t maxBars = getMaxBarsForCurrentMode();
    //     view.startBar = 0;
    //     view.numBars = std::min(uint16_t(16), maxBars);
    //     view.msToPix = CONTENT_WIDTH / float(view.numBars * (data.patternLengthMs));
    //  }

    liveSeq.setActiveTrack(data.activeTrack);

    isVisible = true;
    registerTouchHandler(handle_touchscreen_pianoroll);

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

    // RESET Y ZOOM AND SCROLL WHEN IN MUTE EDITOR
    if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
      if (view.startOctave != 0 || view.numOctaves != 5) {
        view.startOctave = 0;
        view.numOctaves = 5;
        view.numNotes = view.numOctaves * 12;
        view.noteHeight = CONTENT_HEIGHT / float(view.numNotes);
        view.lowestNote = 24 + view.startOctave * 12;
        view.highestNote = (view.lowestNote + view.numNotes);
        view.startBar = 0;
        uint16_t maxBars = getMaxBarsForCurrentMode();
        view.numBars = std::min(uint16_t(8), maxBars); // Default to 8 bars or available max
        // Recalculate msToPix for the new view
        view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
        drawAreaIndicator();
        reloadNotes();
        drawFlags |= DRAW_CONTENT_FULL; // Force redraw
      }
    }

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
        // Simple counter for current song pattern
        static uint16_t currentSongPattern = 0;

        // Reset when playback starts or stops
        static bool wasRunning = false;
        if (data.isRunning && !wasRunning) {
          currentSongPattern = 0;  // Reset when song starts
        }
        wasRunning = data.isRunning;

        // Advance to next song pattern when we wrap around
        static uint8_t lastPattern = 0;
        if (data.currentPattern == 0 && lastPattern == 3) {
          // We just wrapped from pattern 3 to 0 - advance song pattern
          currentSongPattern++;
        }
        lastPattern = data.currentPattern;

        // Calculate absolute time
        currentTimeMs = (currentSongPattern * data.numberOfBars * data.patternLengthMs) +
          (data.currentPattern * data.patternLengthMs) + data.patternTimer;

        // Calculate viewport time range
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
  display.drawLine(stepCursorPosition, 0, stepCursorPosition, CONTENT_HEIGHT - 1, RED);
}




FLASHMEM void UI_LiveSeq_PianoRoll::removeEventByContent(const LiveSequencer::MidiEvent& event) {
    // Remove event by matching all content fields
    for (auto& patternEntry : data.songEvents) {
        auto& events = patternEntry.second;
        events.erase(
            std::remove_if(events.begin(), events.end(),
                [&event](const LiveSequencer::MidiEvent& e) {
                    return e.track == event.track && 
                           e.layer == event.layer &&
                           e.note_in == event.note_in && 
                           e.event == event.event &&
                           e.patternNumber == event.patternNumber && 
                           e.patternMs == event.patternMs &&
                           e.note_in_velocity == event.note_in_velocity;
                }),
            events.end()
        );
    }
}


FLASHMEM std::vector<LiveSequencer::NotePair>
UI_LiveSeq_PianoRoll::getNotePairsFromSongTrack(
  uint8_t track, uint8_t& lowestNote, uint8_t& highestNote,
  uint8_t patternFrom, uint8_t patternTo) {

  std::vector<LiveSequencer::NotePair> result;
  uint8_t lowestFound = 0xFF;
  uint8_t highestFound = 0x00;

  if (data.songEvents.empty()) {
    lowestNote = view.lowestNote;
    highestNote = view.highestNote;
    return result;
  }

  // Create a list of all note on events first
  std::vector<std::tuple<uint16_t, size_t, LiveSequencer::MidiEvent>> noteOnEvents;
  
  // Create a list of all note off events  
  std::vector<std::tuple<uint16_t, size_t, LiveSequencer::MidiEvent>> noteOffEvents;

  // First pass: collect all note on and note off events
  for (const auto& patternEntry : data.songEvents) {
    const uint16_t songPatternIndex = patternEntry.first;
    const auto& events = patternEntry.second;

    for (size_t i = 0; i < events.size(); i++) {
      const auto& event = events[i];
      
      if ((track == 0xFF || event.track == track) && 
          event.note_in >= view.lowestNote && event.note_in < view.highestNote) {
        
        if (event.event == midi::NoteOn) {
          noteOnEvents.emplace_back(songPatternIndex, i, event);
        } else if (event.event == midi::NoteOff) {
          noteOffEvents.emplace_back(songPatternIndex, i, event);
        }
      }
    }
  }

  // Sort both lists by absolute time
  auto getAbsoluteTime = [this](const auto& eventTuple) {
    uint16_t songPatternIndex = std::get<0>(eventTuple);
    const auto& event = std::get<2>(eventTuple);
    return (songPatternIndex * data.numberOfBars * data.patternLengthMs) + 
           (event.patternNumber * data.patternLengthMs) + event.patternMs;
  };

  std::sort(noteOnEvents.begin(), noteOnEvents.end(), 
    [&](const auto& a, const auto& b) { return getAbsoluteTime(a) < getAbsoluteTime(b); });

  std::sort(noteOffEvents.begin(), noteOffEvents.end(), 
    [&](const auto& a, const auto& b) { return getAbsoluteTime(a) < getAbsoluteTime(b); });

  // Pair note on events with their corresponding note off events
  std::set<std::tuple<uint16_t, size_t>> usedNoteOffs;

  for (const auto& noteOnTuple : noteOnEvents) {
    uint16_t onSongPattern = std::get<0>(noteOnTuple);
    size_t onIndex = std::get<1>(noteOnTuple);
    const auto& noteOnEvent = std::get<2>(noteOnTuple);

    uint32_t onAbsoluteTime = getAbsoluteTime(noteOnTuple);

    // Find the earliest note off event with same pitch that occurs after note on
    int bestOffIndex = -1;
    uint32_t bestOffTime = UINT32_MAX;

    for (size_t i = 0; i < noteOffEvents.size(); i++) {
      const auto& noteOffTuple = noteOffEvents[i];
      
      if (usedNoteOffs.count({std::get<0>(noteOffTuple), std::get<1>(noteOffTuple)})) {
        continue; // Already used
      }

      const auto& noteOffEvent = std::get<2>(noteOffTuple);
      
      if (noteOffEvent.track == noteOnEvent.track &&
          noteOffEvent.layer == noteOnEvent.layer &&
          noteOffEvent.note_in == noteOnEvent.note_in) {
        
        uint32_t offAbsoluteTime = getAbsoluteTime(noteOffTuple);
        
        // Note off must be after note on
        if (offAbsoluteTime > onAbsoluteTime && offAbsoluteTime < bestOffTime) {
          bestOffIndex = i;
          bestOffTime = offAbsoluteTime;
        }
      }
    }

    if (bestOffIndex != -1) {
      const auto& noteOffTuple = noteOffEvents[bestOffIndex];
      uint16_t offSongPattern = std::get<0>(noteOffTuple);
      size_t offIndex = std::get<1>(noteOffTuple);
      const auto& noteOffEvent = std::get<2>(noteOffTuple);

      // Check if this pair is within our view time range
      uint32_t viewStartTime = uint32_t(patternFrom) * uint32_t(data.patternLengthMs);
      uint32_t viewEndTime = uint32_t(patternTo + 1) * uint32_t(data.patternLengthMs);

      // Include note if either start or end is within view, or if it spans the view
      if ((onAbsoluteTime < viewEndTime && bestOffTime > viewStartTime)) {
        LiveSequencer::NotePair p = {
          const_cast<LiveSequencer::MidiEvent&>(noteOnEvent),
          const_cast<LiveSequencer::MidiEvent&>(noteOffEvent),
          false
        };

        result.emplace_back(p);
        usedNoteOffs.insert({offSongPattern, offIndex});
        
        highestFound = std::max(highestFound, noteOnEvent.note_in);
        lowestFound = std::min(lowestFound, noteOnEvent.note_in);
      }
    }
  }

  lowestNote = (lowestFound == 0xFF) ? view.lowestNote : lowestFound;
  highestNote = (highestFound == 0x00) ? view.highestNote : highestFound;
  return result;
}

FLASHMEM std::vector<LiveSequencer::MidiEvent>
UI_LiveSeq_PianoRoll::getMuteAutomationEvents(
  uint8_t track, uint8_t patternFrom, uint8_t patternTo) {

  std::vector<LiveSequencer::MidiEvent> result;

  if (data.songEvents.empty()) {
    return result;
  }

  for (const auto& patternEntry : data.songEvents) {
    const uint16_t songPatternIndex = patternEntry.first;
    const uint16_t absoluteBarStart = songPatternIndex * data.numberOfBars;

    if (patternEntry.second.empty()) {
      continue;
    }

    for (const auto& e : patternEntry.second) {
      if ((track == 0xFF || e.track == track) &&
        e.event == midi::ControlChange &&
        (e.note_in == LiveSequencer::TYPE_MUTE_ON || e.note_in == LiveSequencer::TYPE_MUTE_OFF)) {

        const uint16_t eventAbsoluteBar = absoluteBarStart + e.patternNumber;

        if (eventAbsoluteBar >= patternFrom && eventAbsoluteBar <= patternTo) {
          // Keep original cyclic pattern number - don't modify!
          result.emplace_back(e);
        }
      }
    }
  }

  return result;
}

FLASHMEM void UI_LiveSeq_PianoRoll::removeEventFromSpecificPattern(LiveSequencer::MidiEvent& event, uint16_t patternIndex) {
    if (data.songEvents.count(patternIndex)) {
        auto& events = data.songEvents[patternIndex];
        events.erase(
            std::remove_if(events.begin(), events.end(),
                [&event](const LiveSequencer::MidiEvent& e) {
                    return e.track == event.track && 
                           e.layer == event.layer &&
                           e.note_in == event.note_in && 
                           e.event == event.event &&
                           e.patternNumber == event.patternNumber && 
                           e.patternMs == event.patternMs &&
                           e.note_in_velocity == event.note_in_velocity;
                }),
            events.end()
        );
    }
}

FLASHMEM void UI_LiveSeq_PianoRoll::reloadNotes(bool autoAdaptView) {

  // DISABLE EDIT MODE WHEN TRACK CHANGES
  static uint8_t lastActiveTrack = data.activeTrack;
  if (data.activeTrack != lastActiveTrack) {
    mode = MODE_VIEW; // Switch to view mode
    isSettingActivated = false;
    buttonEdit->drawNow();
    buttonView->drawNow();
    lastActiveTrack = data.activeTrack;
  }

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
    
    // FORCE ONE OCTAVE HIGHER FOR ALL MODES
    if (view.startOctave > 0) {
      view.startOctave = std::min(view.startOctave - 1, TOTAL_OCTAVES - view.numOctaves);
    }
    
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

    // Calculate absolute bar on the fly for this event
    uint16_t songPatternIndex = getSongPatternForEvent(event);
    uint16_t absoluteBar = (songPatternIndex * data.numberOfBars) + event.patternNumber;
    const uint32_t eventTime = absoluteBar * data.patternLengthMs + event.patternMs;

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

FLASHMEM void UI_LiveSeq_PianoRoll::drawBarNumbers(void) {
  display.console = true;
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setTextSize(1);

  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numBars * substeps);

  for (uint8_t bar = 0; bar <= view.numBars; bar++) {
    const uint16_t x = ROLL_WIDTH + round(bar * stepWidth * substeps);
    if (bar < view.numBars) {
      const uint16_t displayBar = view.startBar + bar;
      char barInfo[16];
      if (data.isSongMode && !data.useGridSongMode) {
        snprintf(barInfo, sizeof(barInfo), "%d", displayBar + 1);
      }
      else {
        snprintf(barInfo, sizeof(barInfo), "%d", (displayBar % data.numberOfBars) + 1);
      }
      display.setCursor(x - 4, 5);
      display.print(barInfo);
    }
  }
}



FLASHMEM uint16_t UI_LiveSeq_PianoRoll::findSongPatternByEvent(const LiveSequencer::MidiEvent& e) {
    // Search through all song patterns to find which one contains this event
    for (const auto& patternEntry : data.songEvents) {
        const uint16_t songPatternIndex = patternEntry.first;
        const auto& events = patternEntry.second;
        
        for (const auto& event : events) {
            // Compare by all content fields to find the exact event
            if (event.track == e.track &&
                event.layer == e.layer &&
                event.note_in == e.note_in &&
                event.event == e.event &&
                event.patternNumber == e.patternNumber &&
                event.patternMs == e.patternMs &&
                event.note_in_velocity == e.note_in_velocity) {
                return songPatternIndex;
            }
        }
    }
    return 0; // Default to first pattern if not found
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

//debug
// if (selectedNote > -1 && data.isSongMode && !data.useGridSongMode) {
//     char s[32] = { "\0" }; 
//     LiveSequencer::NotePair& p = notePairs.at(selectedNote);
    
//     // Calculate song patterns in real-time
//     uint16_t debugSongPatternOn = findSongPatternByEvent(p.noteOn);
//     uint16_t debugSongPatternOff = findSongPatternByEvent(p.noteOff);
    
//     display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
//     display.setTextSize(1);
//     display.setCursor(100, 33);
    
//     sprintf(s, "On:P%u S%u", p.noteOn.patternNumber, debugSongPatternOn);
//     display.print(s);
//      display.print(" ");
//     display.setCursor(100, 43);
//     sprintf(s, "Off:P%u S%u", p.noteOff.patternNumber, debugSongPatternOff);
//     display.print(s);
//        display.print(" ");
    
//     // Also show absolute times for debugging
//     display.setCursor(100, 53);
//     uint32_t timeOn = getEventTime(p.noteOn);
//     uint32_t timeOff = getEventTime(p.noteOff);
//     sprintf(s, "T:%lu->%lu", timeOn, timeOff);
//     display.print(s);
//        display.print(" ");
// }

  // Draw bar information on top of everything
  // Only draw when we're actually drawing content, not just UI elements

  // if (drawFlags & (DRAW_CONTENT_FULL | DRAW_BACKGROUND | DRAW_NOTES | DRAW_VERTICAL_BARS)) {
  //   drawBarInfoOnTop();
  // }

 //  draw bar numbers last, after everything else
  if (!data.isRunning)
    drawBarNumbers();

  // Clear all draw flags after processing
  drawFlags = 0;
  drawRegionsX.clear();
}

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
    if ((data.isRunning == false) && (mode == MODE_STEP)) {
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

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::findSongPatternForEvent(const LiveSequencer::MidiEvent& e) {
    for (const auto& patternEntry : data.songEvents) {
        const uint16_t songPatternIndex = patternEntry.first;
        const auto& events = patternEntry.second;
        
        for (const auto& event : events) {
            // Compare by memory address or by content to find the exact event
            if (&event == &e) { // Compare by memory address
                return songPatternIndex;
            }
        }
    }
    
    // Fallback: search by content if address comparison fails
    for (const auto& patternEntry : data.songEvents) {
        const uint16_t songPatternIndex = patternEntry.first;
        const auto& events = patternEntry.second;
        
        for (const auto& event : events) {
            if (event.track == e.track &&
                event.layer == e.layer &&
                event.note_in == e.note_in &&
                event.event == e.event &&
                event.patternNumber == e.patternNumber &&
                event.patternMs == e.patternMs) {
                return songPatternIndex;
            }
        }
    }
    
    return 0;
}

FLASHMEM void UI_LiveSeq_PianoRoll::removeEventFromSongEvents(LiveSequencer::MidiEvent& event) {
    // Remove by content matching
    for (auto& patternEntry : data.songEvents) {
        auto& events = patternEntry.second;
        events.erase(
            std::remove_if(events.begin(), events.end(),
                [&event](const LiveSequencer::MidiEvent& e) {
                    return e.track == event.track && 
                           e.layer == event.layer &&
                           e.note_in == event.note_in && 
                           e.event == event.event &&
                           e.patternNumber == event.patternNumber && 
                           e.patternMs == event.patternMs;
                }),
            events.end()
        );
    }
}


FLASHMEM void UI_LiveSeq_PianoRoll::moveEventToSongPattern(LiveSequencer::MidiEvent& e, uint16_t targetSongPattern) {
    uint16_t currentSongPattern = findSongPatternForEvent(e);
    
    if (currentSongPattern == targetSongPattern) {
        return; // Already in correct pattern
    }
    
    // Remove from current pattern
    auto& currentEvents = data.songEvents[currentSongPattern];
    currentEvents.erase(
        std::remove_if(currentEvents.begin(), currentEvents.end(),
            [&e](const LiveSequencer::MidiEvent& event) {
                return &event == &e; // Remove by address
            }),
        currentEvents.end()
    );
    
    // Add to target pattern
    data.songEvents[targetSongPattern].push_back(e);
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

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getSongPatternForNote(const LiveSequencer::NotePair& note) const {
  if (!data.isSongMode || data.useGridSongMode) {
    return 0;
  }

  // note.noteOn may be a copy inside notePairs, so pointer comparisons can fail.
  // Reuse the content-based event matcher already implemented.
  return getSongPatternForEvent(note.noteOn);
}


FLASHMEM void UI_LiveSeq_PianoRoll::removeNoteFromSongEvents(LiveSequencer::MidiEvent& noteOn, LiveSequencer::MidiEvent& noteOff) {
    // Remove by content matching since we don't have direct references
    for (auto& patternEntry : data.songEvents) {
        auto& events = patternEntry.second;
        events.erase(
            std::remove_if(events.begin(), events.end(),
                [&noteOn, &noteOff](const LiveSequencer::MidiEvent& e) {
                    return (e.track == noteOn.track && 
                            e.layer == noteOn.layer &&
                            e.note_in == noteOn.note_in && 
                            e.event == midi::NoteOn &&
                            e.patternNumber == noteOn.patternNumber && 
                            e.patternMs == noteOn.patternMs) ||
                           (e.track == noteOff.track && 
                            e.layer == noteOff.layer &&
                            e.note_in == noteOff.note_in && 
                            e.event == midi::NoteOff &&
                            e.patternNumber == noteOff.patternNumber && 
                            e.patternMs == noteOff.patternMs);
                }),
            events.end()
        );
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
          view.startOctave += e.dir; // zoom octave-centered
          setNumOctaves(view.numOctaves);
          drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL; // Include piano roll in redraw
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

            // Force full content redraw including piano roll
            drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
            drawAreaIndicator();
          }
        }
        break;

      case VIEW_ZOOM_X:
        if ((data.isSongMode && !data.useGridSongMode) || data.activeTrack == SONG_MODE_MUTE_TRACK) {
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
        if ((data.isSongMode && !data.useGridSongMode) || data.activeTrack == SONG_MODE_MUTE_TRACK) {
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
          // For mute automation: scroll through events sorted by screen position
          if (!muteAutomationEvents.empty()) {
            // Build list of events with their screen positions
            std::vector<std::pair<int16_t, uint16_t>> eventsWithPos; // (index, screenX)

            for (size_t i = 0; i < muteAutomationEvents.size(); i++) {
              auto& event = muteAutomationEvents[i];

              // Calculate screen position using absolute time
              uint16_t songPatternIndex = getSongPatternForEvent(event);
              uint32_t absoluteTime = (songPatternIndex * data.numberOfBars * data.patternLengthMs) +
                (event.patternNumber * data.patternLengthMs) + event.patternMs;
              const uint16_t screenX = getNoteCoord(absoluteTime);

              // Only include events that are actually visible on screen
              if (screenX >= ROLL_WIDTH && screenX < DISPLAY_WIDTH) {
                eventsWithPos.push_back({ (int16_t)i, screenX });
              }
            }

            // Sort by screen position
            std::sort(eventsWithPos.begin(), eventsWithPos.end(),
              [](const auto& a, const auto& b) {
                return a.second < b.second;
              });

            // Create the final sorted list
            std::vector<int16_t> sortedEvents;
            for (const auto& pair : eventsWithPos) {
              sortedEvents.push_back(pair.first);
            }

            if (!sortedEvents.empty()) {
              int16_t newSelection;

              if (selectedMuteEvent == -1) {
                // Start with leftmost event on screen
                newSelection = sortedEvents[0];
              }
              else {
                // Find current position in sorted list
                int16_t currentPos = -1;
                for (size_t i = 0; i < sortedEvents.size(); i++) {
                  if (sortedEvents[i] == selectedMuteEvent) {
                    currentPos = i;
                    break;
                  }
                }

                if (currentPos == -1) {
                  // Current selection not visible - start with leftmost
                  newSelection = sortedEvents[0];
                }
                else {
                  // Move within sorted list
                  int16_t newPos = currentPos + e.dir;
                  if (newPos < 0) newPos = sortedEvents.size() - 1;
                  else if (newPos >= static_cast<int16_t>(sortedEvents.size())) newPos = 0;

                  newSelection = sortedEvents[newPos];
                }
              }

              if (newSelection != selectedMuteEvent) {
                // Queue redraw for OLD selected event
                if (selectedMuteEvent > -1) {
                  const auto& oldEvent = muteAutomationEvents.at(selectedMuteEvent);
                  uint16_t oldSongPattern = getSongPatternForEvent(oldEvent);
                  uint32_t oldAbsoluteTime = (oldSongPattern * data.numberOfBars * data.patternLengthMs) +
                    (oldEvent.patternNumber * data.patternLengthMs) + oldEvent.patternMs;
                  UpdateRange oldRegion;
                  oldRegion.fromX = getNoteCoord(oldAbsoluteTime) - 5;
                  oldRegion.toX = getNoteCoord(oldAbsoluteTime) + 5;
                  drawRegionsX.push_back(oldRegion);
                }

                selectedMuteEvent = newSelection;

                // Queue redraw for NEW selected event  
                if (selectedMuteEvent > -1) {
                  const auto& newEvent = muteAutomationEvents.at(selectedMuteEvent);
                  uint16_t newSongPattern = getSongPatternForEvent(newEvent);
                  uint32_t newAbsoluteTime = (newSongPattern * data.numberOfBars * data.patternLengthMs) +
                    (newEvent.patternNumber * data.patternLengthMs) + newEvent.patternMs;
                  UpdateRange newRegion;
                  newRegion.fromX = getNoteCoord(newAbsoluteTime) - 5;
                  newRegion.toX = getNoteCoord(newAbsoluteTime) + 5;
                  drawRegionsX.push_back(newRegion);
                }

                headerFlags = (1 << EDIT_SELECT_NOTE);
                drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_MUTE_AUTOMATION;
              }
            }
            else {
              // No visible events - clear selection
              if (selectedMuteEvent > -1) {
                const auto& oldEvent = muteAutomationEvents.at(selectedMuteEvent);
                uint16_t oldSongPattern = getSongPatternForEvent(oldEvent);
                uint32_t oldAbsoluteTime = (oldSongPattern * data.numberOfBars * data.patternLengthMs) +
                  (oldEvent.patternNumber * data.patternLengthMs) + oldEvent.patternMs;
                UpdateRange oldRegion;
                oldRegion.fromX = getNoteCoord(oldAbsoluteTime) - 5;
                oldRegion.toX = getNoteCoord(oldAbsoluteTime) + 5;
                drawRegionsX.push_back(oldRegion);

                selectedMuteEvent = -1;
                drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_MUTE_AUTOMATION;
              }
            }
          }
          break;
        }
        else {
          if (!notePairs.empty()) {
      // First, check if the selectedLayer actually has any notes
      bool selectedLayerHasNotes = false;
      for (const auto& note : notePairs) {
        if (note.noteOn.layer == selectedLayer) {
          selectedLayerHasNotes = true;
          break;
        }
      }

      // If selected layer has no notes, find the first layer that does
      if (!selectedLayerHasNotes) {
        for (int8_t layer = 0; layer < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; layer++) {
          for (const auto& note : notePairs) {
            if (note.noteOn.layer == layer) {
              selectedLayer = layer;
              selectedLayerHasNotes = true;
              break;
            }
          }
          if (selectedLayerHasNotes) break;
        }
      }

      // Build list of notes with their screen positions - ONLY FROM SELECTED LAYER
      std::vector<std::pair<int16_t, uint16_t>> notesWithPos;

      for (size_t i = 0; i < notePairs.size(); i++) {
        auto& note = notePairs[i];

        // Only include notes from the selected layer
        if (note.noteOn.layer != selectedLayer) {
          continue;
        }

        // Calculate screen position
        uint32_t timeOn;
        if (data.isSongMode && !data.useGridSongMode) {
          uint16_t absoluteBarOn = getActualBarForNote(note);
          timeOn = absoluteBarOn * data.patternLengthMs + note.noteOn.patternMs;
        }
        else {
          timeOn = getEventTime(note.noteOn);
        }

        const uint16_t screenX = getNoteCoord(timeOn);

        // Only include notes that are actually visible on screen
        if (screenX >= ROLL_WIDTH && screenX < DISPLAY_WIDTH) {
          notesWithPos.push_back({ (int16_t)i, screenX });
        }
      }

      if (!notesWithPos.empty()) {
        int16_t newSelection;

        if (selectedNote == -1) {
          // Start with leftmost note on screen from SELECTED LAYER
          newSelection = notesWithPos[0].first;
        }
        else {
          // Find current position in sorted list
          int16_t currentPos = -1;
          for (size_t i = 0; i < notesWithPos.size(); i++) {
            if (notesWithPos[i].first == selectedNote) {
              currentPos = i;
              break;
            }
          }

          if (currentPos == -1) {
            // Current selection not in selected layer - start with leftmost
            newSelection = notesWithPos[0].first;
          }
          else {
            // Move within sorted list
            int16_t newPos = currentPos + e.dir;
            if (newPos < 0) newPos = notesWithPos.size() - 1;
            else if (newPos >= static_cast<int16_t>(notesWithPos.size())) newPos = 0;

            newSelection = notesWithPos[newPos].first;
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
        // No visible notes in selected layer - clear selection
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
              // Queue redraw for OLD state
              uint16_t songPattern = getSongPatternForEvent(event);
              uint32_t absoluteTime = (songPattern * data.numberOfBars * data.patternLengthMs) +
                (event.patternNumber * data.patternLengthMs) + event.patternMs;
              UpdateRange region;
              region.fromX = getNoteCoord(absoluteTime) - 5;
              region.toX = getNoteCoord(absoluteTime) + 5;
              drawRegionsX.push_back(region);

              event.note_in_velocity = layer;
              updateSongEvent(event);

              // Queue redraw for NEW state
              drawRegionsX.push_back(region); // Same region since position didn't change

              headerFlags |= (1 << EDIT_LAYER);
              drawFlags |= DRAW_MUTE_AUTOMATION | DRAW_BACKGROUND;
            }
          }
        }
        else {
          // Layer editing for both pattern mode AND song mode
    
    // Find available layers with notes
    std::set<int8_t> availableLayers;
    for (const auto& note : notePairs) {
      availableLayers.insert(note.noteOn.layer);
    }
    
    // If no layers have notes, use all layers
    if (availableLayers.empty()) {
      for (int8_t i = 0; i < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; i++) {
        availableLayers.insert(i);
      }
    }

    // Find next/previous available layer
    int8_t newSelectedLayer = selectedLayer;
    if (e.dir > 0) {
      // Move to next available layer
      auto it = availableLayers.upper_bound(selectedLayer);
      if (it == availableLayers.end()) {
        newSelectedLayer = *availableLayers.begin(); // Wrap around to first
      }
      else {
        newSelectedLayer = *it;
      }
    }
    else if (e.dir < 0) {
      // Move to previous available layer
      auto it = availableLayers.lower_bound(selectedLayer);
      if (it == availableLayers.begin()) {
        newSelectedLayer = *availableLayers.rbegin(); // Wrap around to last
      }
      else {
        --it;
        newSelectedLayer = *it;
      }
    }

    if (newSelectedLayer != selectedLayer) {
      // Clear note selection when changing layers
      if (selectedNote > -1) {
        queueDrawNote(notePairs.at(selectedNote));
        selectedNote = -1;
      }
      
      selectedLayer = newSelectedLayer;
      reloadNotes();
      headerFlags |= (1 << EDIT_LAYER);
      drawFlags |= DRAW_NOTES;
     queueDrawNote({ ROLL_WIDTH, DISPLAY_WIDTH, {} }); 
      drawLayersIndicator();
    }
  }
  break;

      case EDIT_NOTEON:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          if (selectedMuteEvent > -1 && selectedMuteEvent < static_cast<int16_t>(muteAutomationEvents.size())) {
            LiveSequencer::MidiEvent& eventCopy = muteAutomationEvents.at(selectedMuteEvent);

            // Queue redraw for OLD position
            uint16_t oldSongPattern = getSongPatternForEvent(eventCopy);
            uint32_t oldAbsoluteTime = (oldSongPattern * data.numberOfBars * data.patternLengthMs) +
              (eventCopy.patternNumber * data.patternLengthMs) + eventCopy.patternMs;
            UpdateRange oldRegion;
            oldRegion.fromX = getNoteCoord(oldAbsoluteTime) - 5;
            oldRegion.toX = getNoteCoord(oldAbsoluteTime) + 5;
            drawRegionsX.push_back(oldRegion);

            // FIND CANONICAL EVENT in data.songEvents (same as notes)
            LiveSequencer::MidiEvent* realEvent = nullptr;
            uint16_t songPatternIndex = 0;
            for (auto& patternEntry : data.songEvents) {
              for (auto& ev : patternEntry.second) {
                if (ev.track == eventCopy.track &&
                  ev.layer == eventCopy.layer &&
                  ev.note_in == eventCopy.note_in &&
                  ev.event == eventCopy.event &&
                  ev.patternMs == eventCopy.patternMs &&
                  ev.note_in_velocity == eventCopy.note_in_velocity) {
                  realEvent = &ev;
                  songPatternIndex = patternEntry.first;
                  break;
                }
              }
              if (realEvent) break;
            }
            if (!realEvent) {
              songPatternIndex = getSongPatternForEvent(eventCopy);
            }

            // Use pattern-relative timing (same as notes)
            uint32_t time = getPatternRelativeTime(eventCopy);
            const uint32_t gridStep = data.patternLengthMs / 16;
            uint32_t newTime = time + (e.dir * e.speed * gridStep);

            // Constrain to valid range (same as notes)
            uint32_t maxTime = uint32_t(data.numberOfBars) * data.patternLengthMs;
            newTime = constrain(newTime, 0U, maxTime);

            if (newTime != time) {
              // Convert back to cyclic patternNumber/patternMs (SAME AS NOTES)
              uint32_t absoluteTime = (songPatternIndex * data.numberOfBars * data.patternLengthMs) + newTime;
              uint8_t newPatternNumber = (absoluteTime / data.patternLengthMs) % data.numberOfBars;
              uint32_t newPatternMs = absoluteTime % data.patternLengthMs;

              // Update the CANONICAL event in data.songEvents (same as notes)
              if (realEvent) {
                realEvent->patternNumber = newPatternNumber;
                realEvent->patternMs = newPatternMs;

                // Also update the local copy for immediate display
                eventCopy.patternNumber = newPatternNumber;
                eventCopy.patternMs = newPatternMs;
              }
              else {
                // Fallback: update via the copy
                eventCopy.patternNumber = newPatternNumber;
                eventCopy.patternMs = newPatternMs;
                updateSongEvent(eventCopy);
              }

              headerFlags |= (1 << EDIT_NOTEON);

              // Queue redraw for NEW position
              uint32_t newAbsoluteTime = (songPatternIndex * data.numberOfBars * data.patternLengthMs) +
                (newPatternNumber * data.patternLengthMs) + newPatternMs;
              UpdateRange newRegion;
              newRegion.fromX = getNoteCoord(newAbsoluteTime) - 5;
              newRegion.toX = getNoteCoord(newAbsoluteTime) + 5;
              drawRegionsX.push_back(newRegion);

              //drawFlags |= DRAW_MUTE_AUTOMATION | DRAW_BACKGROUND;
              drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_MUTE_AUTOMATION;
            }
          }
        }

////////////////////////////////////////////////////
        
  else if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
  if (selectedNote > -1) {
    LiveSequencer::NotePair &note = notePairs.at(selectedNote);

    // Original canonical events and their song-pattern indices
    LiveSequencer::MidiEvent originalOn  = note.noteOn;
    LiveSequencer::MidiEvent originalOff = note.noteOff;
    uint16_t originalSongPatternOn  = findSongPatternByEvent(originalOn);
    uint16_t originalSongPatternOff = findSongPatternByEvent(originalOff);

    const uint32_t gridStep    = data.patternLengthMs / 16;
    const uint32_t totalSongMs = uint32_t(getMaxBarsForCurrentMode()) * data.patternLengthMs;

    // Absolute times (full-song)
    uint32_t oldAbsOn  = getAbsoluteEventTime(originalOn);
    uint32_t oldAbsOff = getAbsoluteEventTime(originalOff);
    uint32_t noteLength = (oldAbsOff > oldAbsOn) ? (oldAbsOff - oldAbsOn) : gridStep;

    int32_t totalGridSteps = int32_t(totalSongMs / gridStep);
    int32_t currentGrid    = int32_t(oldAbsOn / gridStep);
    int32_t newGrid        = currentGrid + (e.dir * e.speed);
    newGrid = constrain(newGrid, 0, std::max<int32_t>(0, totalGridSteps - 1));

    uint32_t newAbsOn = uint32_t(newGrid) * gridStep;

    // Clamp so that note end stays inside song
    if (newAbsOn + noteLength > totalSongMs) {
      newAbsOn = (totalSongMs > noteLength) ? (totalSongMs - noteLength) : 0;
    }

    // Signed delta shift
    int32_t delta         = int32_t(newAbsOn) - int32_t(oldAbsOn);
    int32_t newAbsOffTime = int32_t(oldAbsOff) + delta;

    // Keep minimum note length
    if (newAbsOffTime <= int32_t(newAbsOn))
      newAbsOffTime = int32_t(newAbsOn) + int32_t(gridStep);

    if (newAbsOn != oldAbsOn) {
      queueDrawNote(note); // erase old

      // Build new NoteOn
      LiveSequencer::MidiEvent newOn = originalOn;
      uint32_t barOn     = newAbsOn / data.patternLengthMs;
      uint16_t songPatOn = barOn / data.numberOfBars;
      newOn.patternNumber = barOn % data.numberOfBars;
      newOn.patternMs      = newAbsOn % data.patternLengthMs;

      // Build new NoteOff
      LiveSequencer::MidiEvent newOff = originalOff;
      uint32_t barOff     = newAbsOffTime / data.patternLengthMs;
      uint16_t songPatOff = barOff / data.numberOfBars;
      newOff.patternNumber = barOff % data.numberOfBars;
      newOff.patternMs      = newAbsOffTime % data.patternLengthMs;

      // Update canonical data
      removeEventFromSpecificPattern(originalOn,  originalSongPatternOn);
      removeEventFromSpecificPattern(originalOff, originalSongPatternOff);
      data.songEvents[songPatOn].push_back(newOn);
      data.songEvents[songPatOff].push_back(newOff);

      // Update local UI copy
      note.noteOn  = newOn;
      note.noteOff = newOff;
      memcpy(&currentNote, &note.noteOn, sizeof(currentNote));

      // Redraw new position
      queueDrawNote(note);
      headerFlags |= (1 << EDIT_NOTEON);
      drawFlags   |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    }
  }
}
  ///////////////////////////////

  else {
    // PATTERN MODE - existing working code
     if (selectedNote > -1) {
      LiveSequencer::MidiEvent& on = notePairs.at(selectedNote).noteOn;
      LiveSequencer::MidiEvent& off = notePairs.at(selectedNote).noteOff;

      uint32_t oldNoteOnTime = getEventTime(on);
      uint32_t noteOffTime = getEventTime(off);

      // Calculate the actual note length from the current noteOff time
      uint32_t noteLength = (noteOffTime > oldNoteOnTime) ? (noteOffTime - oldNoteOnTime) : 1;

      const uint32_t gridStep = data.patternLengthMs / 16;
      const uint32_t minTime = 0;

      // Calculate maximum allowed start time
      uint32_t viewEndMs = uint32_t(view.startBar + view.numBars) * uint32_t(data.patternLengthMs);
      uint32_t maxTimeFromScreen = (viewEndMs > noteLength) ? (viewEndMs - noteLength) : 0;
      uint32_t maxTime = maxTimeFromScreen;

      // Snap to grid
      int32_t gridSteps = roundf(float(oldNoteOnTime) / float(gridStep));
      gridSteps = constrain(gridSteps + e.dir * e.speed,
        roundf(float(minTime) / float(gridStep)),
        roundf(float(maxTime) / float(gridStep)));
      uint32_t newTime = gridSteps * gridStep;

      if (newTime != oldNoteOnTime) {
        // Queue redraw for old position
        queueDrawNote(notePairs.at(selectedNote));

        // Update the note times while preserving the note length
        setEventTime(on, newTime);
        
        // Use the ACTUAL note length, not a stored static value
        uint32_t newNoteOffTime = newTime + noteLength;
        setEventTime(off, newNoteOffTime);

        // Update currentNote
        memcpy(&currentNote, &on, sizeof(currentNote));

        // Queue redraw for new position
        queueDrawNote(notePairs.at(selectedNote));

        headerFlags |= (1 << EDIT_NOTEON);
        drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
      }
    }
  }
  break;
        

      case EDIT_NOTEOFF:
  if (selectedNote > -1 && data.activeTrack == SONG_MODE_NOTES_TRACK) {
  LiveSequencer::NotePair &note = notePairs.at(selectedNote);

  LiveSequencer::MidiEvent originalOff = note.noteOff;
  uint16_t originalSongPatternOff = findSongPatternByEvent(originalOff);

  const uint32_t gridStep    = data.patternLengthMs / 16;
  const uint32_t totalSongMs = uint32_t(getMaxBarsForCurrentMode()) * data.patternLengthMs;

  uint32_t absOn  = getAbsoluteEventTime(note.noteOn);
  uint32_t absOff = getAbsoluteEventTime(originalOff);

  int32_t totalGridSteps = int32_t(totalSongMs / gridStep);
  int32_t currentGrid    = int32_t(absOff / gridStep);
  int32_t newGrid        = currentGrid + (e.dir * e.speed);
  newGrid = constrain(newGrid, 0, std::max<int32_t>(0, totalGridSteps - 1));

  uint32_t newAbsOff = uint32_t(newGrid) * gridStep;

  // Keep note end at least one gridStep after start
  if (newAbsOff <= absOn) newAbsOff = absOn + gridStep;
  if (newAbsOff > totalSongMs) newAbsOff = totalSongMs;

  if (newAbsOff != absOff) {
    queueDrawNote(note); // erase old

    LiveSequencer::MidiEvent newOff = originalOff;
    uint32_t barOff     = newAbsOff / data.patternLengthMs;
    uint16_t songPatOff = barOff / data.numberOfBars;
    newOff.patternNumber = barOff % data.numberOfBars;
    newOff.patternMs      = newAbsOff % data.patternLengthMs;

    removeEventFromSpecificPattern(originalOff, originalSongPatternOff);
    data.songEvents[songPatOff].push_back(newOff);

    note.noteOff = newOff;
    memcpy(&currentNote, &note.noteOn, sizeof(currentNote));

    queueDrawNote(note); // draw new
    headerFlags |= (1 << EDIT_NOTEOFF);
    drawFlags   |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
  }
}
  /////
  else if (selectedNote > -1) {
    // PATTERN MODE - existing working code
    LiveSequencer::MidiEvent& on = notePairs.at(selectedNote).noteOn;
    LiveSequencer::MidiEvent& off = notePairs.at(selectedNote).noteOff;

    uint32_t noteOnTime = getEventTime(on);
    uint32_t oldNoteOffTime = getEventTime(off);

    const uint32_t gridStep = data.patternLengthMs / 16;
    const uint32_t minTime = noteOnTime + gridStep; // minimum note length = 1 grid step

    // Calculate maximum allowed end time
    uint32_t viewEndMs = uint32_t(view.startBar + view.numBars) * uint32_t(data.patternLengthMs);
    uint32_t maxTime = viewEndMs;

    // Snap to grid
    int32_t gridSteps = roundf(float(oldNoteOffTime) / float(gridStep));
    gridSteps = constrain(gridSteps + e.dir * e.speed,
      roundf(float(minTime) / float(gridStep)),
      roundf(float(maxTime) / float(gridStep)));
    uint32_t newTime = gridSteps * gridStep;

    if (newTime != oldNoteOffTime) {
      // PATTERN MODE APPROACH: queue redraw for old position
      queueDrawNote(notePairs.at(selectedNote));

      // Update the note end time
      setEventTime(off, newTime);

      // Update currentNote
      memcpy(&currentNote, &on, sizeof(currentNote));

      // PATTERN MODE APPROACH: queue redraw for new position
      queueDrawNote(notePairs.at(selectedNote));

      headerFlags |= (1 << EDIT_NOTEOFF);
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
            LiveSequencer::MidiEvent& eventCopy = muteAutomationEvents.at(selectedMuteEvent);

            // Queue redraw for OLD state
            uint16_t songPattern = getSongPatternForEvent(eventCopy);
            uint32_t absoluteTime = (songPattern * data.numberOfBars * data.patternLengthMs) +
              (eventCopy.patternNumber * data.patternLengthMs) + eventCopy.patternMs;
            UpdateRange region;
            region.fromX = getNoteCoord(absoluteTime) - 5;
            region.toX = getNoteCoord(absoluteTime) + 5;
            drawRegionsX.push_back(region);

            // FIND CANONICAL EVENT first (same as time editing)
            LiveSequencer::MidiEvent* realEvent = nullptr;
            //uint16_t songPatternIndex = 0;
            for (auto& patternEntry : data.songEvents) {
              for (auto& ev : patternEntry.second) {
                if (ev.track == eventCopy.track &&
                  ev.layer == eventCopy.layer &&
                  ev.note_in == eventCopy.note_in &&
                  ev.event == eventCopy.event &&
                  ev.patternMs == eventCopy.patternMs &&
                  ev.note_in_velocity == eventCopy.note_in_velocity) {
                  realEvent = &ev;
                  //songPatternIndex = patternEntry.first;
                  break;
                }
              }
              if (realEvent) break;
            }

            if (e.dir != 0) {
              uint8_t newMuteState = (eventCopy.note_in == LiveSequencer::TYPE_MUTE_ON) ?
                LiveSequencer::TYPE_MUTE_OFF : LiveSequencer::TYPE_MUTE_ON;

              // Update the CANONICAL event first
              if (realEvent) {
                realEvent->note_in = newMuteState;
                // Also update the local copy
                eventCopy.note_in = newMuteState;
              }
              else {
                // Fallback
                eventCopy.note_in = newMuteState;
                updateSongEvent(eventCopy);
              }

              // Queue redraw for NEW state
              drawRegionsX.push_back(region); // Same region since position didn't change

              headerFlags |= (1 << EDIT_NOTE);
              drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_MUTE_AUTOMATION;
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
      if (data.activeTrack == SONG_MODE_MUTE_TRACK)
        newMode = constrain(viewMode + e.dir, 0, VIEW_NUM - 3); // no y scrolling
      else
        newMode = constrain(viewMode + e.dir, 0, VIEW_NUM - 1);
      viewMode = newMode;
      buttonView->drawNow();
      break;

    case MODE_EDIT:
      oldMode = editMode;
      if (data.activeTrack == SONG_MODE_MUTE_TRACK)
      {
        // For mute automation, cycle through only these 3 parameters (no LAYER):
        const uint8_t muteEditModes[] = { EDIT_SELECT_NOTE, EDIT_NOTEON, EDIT_NOTE };
        const uint8_t numMuteModes = 3;

        // Find current position in mute modes array
        uint8_t currentPos = 0;
        for (uint8_t i = 0; i < numMuteModes; i++) {
          if (muteEditModes[i] == editMode) {
            currentPos = i;
            break;
          }
        }

        // If current mode is not in mute modes (like LAYER), start from first
        if (currentPos == 0 && muteEditModes[0] != editMode) {
          currentPos = 0;
          editMode = muteEditModes[0]; // Force to valid mode
        }

        // Calculate new position
        int8_t newPos = currentPos + e.dir;
        if (newPos < 0) newPos = numMuteModes - 1;
        else if (newPos >= numMuteModes) newPos = 0;

        newMode = muteEditModes[newPos];
      }
      else {
        newMode = constrain(editMode + e.dir, 0, EDIT_NUM - 1);
      }
      editMode = newMode;
      buttonEdit->drawNow();
      break;

    case MODE_STEP:
    {
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
  drawFlags |= DRAW_PIANOROLL; // Force piano roll redraw when start octave changes
}

FLASHMEM void UI_LiveSeq_PianoRoll::setNumOctaves(int8_t num) {
  view.numOctaves = num;
  view.numNotes = view.numOctaves * 12;
  view.noteHeight = CONTENT_HEIGHT / float(view.numNotes);
  view.lowestNote = 24 + view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
  reloadNotes();
  drawAreaIndicator();
  drawFlags |= DRAW_PIANOROLL; // Force piano roll redraw when octaves change
}

// FLASHMEM void UI_LiveSeq_PianoRoll::setEventTime(LiveSequencer::MidiEvent& e, uint32_t time) {
//   if (data.isSongMode && !data.useGridSongMode) {
//     // Song mode: use existing pattern-relative timing
//     uint16_t songPatternIndex = getSongPatternForEvent(e);
//     setPatternRelativeTime(e, time, songPatternIndex);
//   }
//   else {
//     // Pattern mode: wrap patternNumber within numberOfBars
//     // This allows cross-pattern movement while maintaining cyclic storage
//     e.patternNumber = (time / data.patternLengthMs) % data.numberOfBars;
//     e.patternMs = time % data.patternLengthMs;
//   }
// }

// FLASHMEM void UI_LiveSeq_PianoRoll::setEventTime(LiveSequencer::MidiEvent& e, uint32_t time) {
//     if (data.isSongMode && !data.useGridSongMode) {
//         // For song mode: convert absolute time to song pattern structure
//         uint32_t absoluteBar = time / data.patternLengthMs;
//         uint16_t songPatternIndex = absoluteBar / data.numberOfBars;
//         uint8_t patternNumber = absoluteBar % data.numberOfBars;
//         uint16_t patternMs = time % data.patternLengthMs;
        
//         // Update the event directly
//         e.patternNumber = patternNumber;
//         e.patternMs = patternMs;
//     }
//     else {
//         // Pattern mode: wrap patternNumber within numberOfBars
//         e.patternNumber = (time / data.patternLengthMs) % data.numberOfBars;
//         e.patternMs = time % data.patternLengthMs;
//     }
// }

FLASHMEM void UI_LiveSeq_PianoRoll::setEventTime(LiveSequencer::MidiEvent& e, uint32_t time) {
    // Basic conversion - song pattern movement is handled separately
    uint32_t absoluteBar = time / data.patternLengthMs;
    e.patternNumber = absoluteBar % data.numberOfBars;
    e.patternMs = time % data.patternLengthMs;
}


FLASHMEM void UI_LiveSeq_PianoRoll::drawHeader(uint16_t modeFlags) {
  display.setTextSize(1);
  char s[16] = { "\0" };

  if (modeFlags == HEADER_DRAW_ALL) {
    display.console = true;
    display.fillRect(132, CONTENT_HEIGHT + 4, DISPLAY_WIDTH, HEADER_HEIGHT - 6, HEADER_BG_COLOR);
  }

  switch (mode) {
  case MODE_VIEW:
    if (modeFlags & (1 << VIEW_ZOOM_X)) {
      drawHeaderSetting(GRID.X[3], "ZOOM X", "", (viewMode == VIEW_ZOOM_X), false);
    }
    if (modeFlags & (1 << VIEW_SCROLL_X)) {
      drawHeaderSetting(GRID.X[4], "SCROLL X", "", (viewMode == VIEW_SCROLL_X), false);
    }
    if (data.activeTrack != SONG_MODE_MUTE_TRACK) {
      if (modeFlags & (1 << VIEW_ZOOM_Y)) {
        drawHeaderSetting(GRID.X[3], "", "ZOOM Y ", false, (viewMode == VIEW_ZOOM_Y));
      }
      if (modeFlags & (1 << VIEW_SCROLL_Y)) {
        drawHeaderSetting(GRID.X[4], "", "SCROLL Y", false, (viewMode == VIEW_SCROLL_Y));
      }
    }
    break;

  case MODE_EDIT:
    if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
      // Mute automation header - only show relevant parameters
      if (modeFlags & (1 << EDIT_SELECT_NOTE)) {
        if (selectedMuteEvent > -1 && selectedMuteEvent < static_cast<int16_t>(muteAutomationEvents.size())) {
          sprintf(s, "%03i/%03i", selectedMuteEvent + 1, muteAutomationEvents.size());
        }
        else {
          sprintf(s, "---/%03i", muteAutomationEvents.size());
        }
        drawHeaderSetting(GRID.X[3], "SELECT", s, false, (editMode == EDIT_SELECT_NOTE));
      }
      if (modeFlags & (1 << EDIT_NOTEON)) {
        if (selectedMuteEvent > -1 && selectedMuteEvent < static_cast<int16_t>(muteAutomationEvents.size())) {
          auto& event = muteAutomationEvents[selectedMuteEvent];
          // Use the EXACT same pattern as song mode notes
          uint32_t patternRelativeTime = getPatternRelativeTime(event);
          sprintf(s, "%lu.%04lu",
            patternRelativeTime / data.patternLengthMs,
            patternRelativeTime % data.patternLengthMs);
        }
        else {
          sprintf(s, "-.----");
        }
        drawHeaderSetting(GRID.X[4], "POS", s, false, (editMode == EDIT_NOTEON));
      }
      if (modeFlags & (1 << EDIT_NOTE)) {
        if (selectedMuteEvent > -1 && selectedMuteEvent < static_cast<int16_t>(muteAutomationEvents.size())) {
          auto& event = muteAutomationEvents[selectedMuteEvent];
          sprintf(s, "%s", event.note_in == LiveSequencer::TYPE_MUTE_ON ? "ON " : "OFF");
        }
        else {
          sprintf(s, "---");
        }
        drawHeaderSetting(GRID.X[5], "MUTE", s, false, (editMode == EDIT_NOTE));
      }
    }
    if (selectedNote > -1 || modeFlags & (1 << EDIT_LAYER)) {
      
      // For pattern mode, show values even when no note is selected
      std::string noteString = "";
      if (selectedNote > -1) {
        LiveSequencer::NotePair& p = notePairs.at(selectedNote);
        noteString = getNoteString(p.noteOn.note_in);
        if (noteString.length() == 2) {
          noteString.append(" ");
        }
      }
    }
     if (modeFlags & (1 << EDIT_SELECT_NOTE)) {
        // Count only notes in the selected layer
        int16_t layerNoteIndex = 0;
        int16_t totalLayerNotes = 0;

        for (size_t i = 0; i < notePairs.size(); i++) {
          if (notePairs[i].noteOn.layer == selectedLayer) {
            totalLayerNotes++;
            if (selectedNote > -1 && i < static_cast<size_t>(selectedNote)) {
              layerNoteIndex++;
            }
          }
        }
        
        if (selectedNote > -1) {
          layerNoteIndex++; // Current selection is also counted
          sprintf(s, "%03i/%03i", layerNoteIndex, totalLayerNotes);
        } else {
          sprintf(s, "---/%03i", totalLayerNotes);
        }
        drawHeaderSetting(GRID.X[3], "SELECT", s, false, (editMode == EDIT_SELECT_NOTE));
      }
      
      if (modeFlags & (1 << EDIT_LAYER)) {
        sprintf(s, "%i", selectedLayer + 1);
        drawHeaderSetting(GRID.X[2] + 25, "LAYR", s, false, (editMode == EDIT_LAYER));
      }

     if (modeFlags & (1 << EDIT_NOTEON)) {
  if (selectedNote > -1) {
    LiveSequencer::NotePair& p = notePairs.at(selectedNote);
    if (data.isSongMode && !data.useGridSongMode) {
      uint32_t patternRelativeTime = getPatternRelativeTime(p.noteOn);
      sprintf(s, "/ %lu.%04lu",
        patternRelativeTime / data.patternLengthMs,
        patternRelativeTime % data.patternLengthMs);
    }
    else {
      sprintf(s, "/ %i.%04i", p.noteOn.patternNumber, p.noteOn.patternMs);
    }
  } else {
    sprintf(s, "/ --.----");
  }
  drawHeaderSetting(GRID.X[4], s, "", (editMode == EDIT_NOTEON), false);
}
if (modeFlags & (1 << EDIT_NOTEOFF)) {
  if (selectedNote > -1) {
    LiveSequencer::NotePair& p = notePairs.at(selectedNote);
    if (data.isSongMode && !data.useGridSongMode) {
      uint32_t patternRelativeTime = getPatternRelativeTime(p.noteOff);
      sprintf(s, "\\ %lu.%04lu",
        patternRelativeTime / data.patternLengthMs,
        patternRelativeTime % data.patternLengthMs);
    }
    else {
      sprintf(s, "\\ %i.%04i", p.noteOff.patternNumber, p.noteOff.patternMs);
    }
  } else {
    sprintf(s, "\\ --.----");
  }
  drawHeaderSetting(GRID.X[4], "", s, false, (editMode == EDIT_NOTEOFF));
}
if (modeFlags & (1 << EDIT_VELOCITY)) {
  if (selectedNote > -1) {
    LiveSequencer::NotePair& p = notePairs.at(selectedNote);
    sprintf(s, "%i", p.noteOn.note_in_velocity);
  } else {
    sprintf(s, "---");
  }
  drawHeaderSetting(GRID.X[5], "VEL", s, false, (editMode == EDIT_VELOCITY));
}
if (modeFlags & (1 << EDIT_NOTE)) {
  if (selectedNote > -1) {
    LiveSequencer::NotePair& p = notePairs.at(selectedNote);
    std::string noteString = getNoteString(p.noteOn.note_in);
    sprintf(s, "%s", noteString.c_str());
  } else {
    sprintf(s, "---");
  }
  drawHeaderSetting(GRID.X[5] + 4 * CHAR_width_small, "NOTE", s, false, (editMode == EDIT_NOTE));
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

  // Draw white key separators - these scale with zoom Y
  for (uint8_t i = 1; i < numWhiteKeys; i++) {
    const uint16_t y = round(i * keyHeight);
    display.console = true;
    display.drawLine(0, y, ROLL_WIDTH, y, COLOR_BACKGROUND);
  }

  // Draw black keys - these scale with zoom Y
  for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
    const uint8_t noteIndex = view.highestNote - note - 1;
    const uint16_t y = ceil(noteIndex * view.noteHeight); // ceil looks best rounded
    switch (note % 12) {
    case 1:  // C#
    case 3:  // D#
    case 6:  // F#
    case 8:  // G#
    case 10: // A#
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

FLASHMEM void UI_LiveSeq_PianoRoll::drawVerticalBars(void) {
  // Draw vertical pattern lines first
  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numBars * substeps);

  // Draw vertical lines (full height)
  for (uint8_t step = 0; step < view.numBars * substeps; step++) {
    const uint16_t x = ROLL_WIDTH + round(step * stepWidth);
    const uint16_t lineColor = (step % substeps == 0) ? GREY2 : GREY3;

    for (auto& u : drawRegionsX) {
      const bool drawLine = ((x >= u.fromX) && (x <= u.toX));
      if (drawLine) {
        if (u.notes.empty()) {
          // Full height lines for background updates
          display.console = true;
          display.drawFastVLine(x, 0, CONTENT_HEIGHT, lineColor);
        }
        else {
          // Only draw lines where single note update is needed and key *not* pressed
          for (uint8_t note : u.notes) {
            if (keysPressed.find(note) == keysPressed.end()) {
              const uint8_t noteIndex = view.highestNote - note - 1;
              const uint16_t y = round(noteIndex * view.noteHeight);
              display.console = true;
              display.drawFastVLine(x, y, view.noteHeight, lineColor);
            }
          }
        }
        break; // Only draw once per line
      }
    }
  }

  // Draw horizontal lines - respect note filter for horizontal lines
  const uint16_t numWhiteKeys = view.numOctaves * 7;
  const float whiteKeyHeight = CONTENT_HEIGHT / float(numWhiteKeys);
  
  for (uint8_t whiteKey = 0; whiteKey <= numWhiteKeys; whiteKey++) {
    const uint16_t y = (uint16_t)(whiteKey * whiteKeyHeight + 0.5f);
    const uint16_t lineColor = (whiteKey % 7 == 0) ? GREY2 : GREY3;

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
  if (time >= viewEndMs) {  // Changed from > to >= to prevent drawing at DISPLAY_WIDTH
    return DISPLAY_WIDTH - 1;
  }

  // Calculate position within viewport
  float ratio = float(time - viewStartMs) / float(viewEndMs - viewStartMs);
  uint16_t px = ROLL_WIDTH + roundf(ratio * CONTENT_WIDTH);

  // Clamp to display bounds (ensure we don't draw at DISPLAY_WIDTH)
  if (px < ROLL_WIDTH) px = ROLL_WIDTH;
  if (px >= DISPLAY_WIDTH) px = DISPLAY_WIDTH - 1;  // Changed from > to >=

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
  int8_t activeLayer = selectedLayer; // Use the selectedLayer variable

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

  // Calculate Y position - use the same calculation as old file
  const uint8_t noteIndex = view.highestNote - note.noteOn.note_in - 1;
  const uint16_t y = round(noteIndex * view.noteHeight);
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

        // SORT NOTES when exiting edit mode to ensure proper order for playback
        liveSeq.requestSortEvents();
        onEventsChanged(); // Re-find the selected note after sorting
      }

      // Also handle mute automation track when exiting edit mode
      if (data.activeTrack == SONG_MODE_MUTE_TRACK && selectedMuteEvent > -1) {
        // For mute automation, ensure proper sorting
        liveSeq.requestSortEvents();
        reloadNotes();
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
        // Find the actual leftmost visible note instead of just notePairs[0]
        int16_t leftmostNote = -1;
        uint16_t leftmostX = DISPLAY_WIDTH;

        for (size_t i = 0; i < notePairs.size(); i++) {
          auto& note = notePairs[i];

          // ONLY consider notes from the selectedLayer
          if (note.noteOn.layer != selectedLayer) {
            continue;
          }

          // Calculate screen position
          uint32_t timeOn;
          if (data.isSongMode && !data.useGridSongMode) {
            uint16_t absoluteBarOn = getActualBarForNote(note);
            timeOn = absoluteBarOn * data.patternLengthMs + note.noteOn.patternMs;
          }
          else {
            timeOn = getEventTime(note.noteOn);
          }

          const uint16_t x = getNoteCoord(timeOn);
          if (x >= ROLL_WIDTH && x < DISPLAY_WIDTH && x < leftmostX) {
            leftmostNote = i;
            leftmostX = x;
          }
        }

        // If we found a visible note in selectedLayer, select it
        if (leftmostNote != -1) {
          selectedNote = leftmostNote;
        }
        else {
          // Fallback: find first note in selectedLayer (even if not visible)
          for (size_t i = 0; i < notePairs.size(); i++) {
            if (notePairs[i].noteOn.layer == selectedLayer) {
              selectedNote = i;
              break;
            }
          }
        }

        if (selectedNote > -1) {
          queueDrawNote(notePairs.at(selectedNote));
          drawFlags |= DRAW_NOTES;
        }
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
  // For mute automation track
  if (data.activeTrack == SONG_MODE_MUTE_TRACK && selectedMuteEvent > -1 && 
      selectedMuteEvent < static_cast<int16_t>(muteAutomationEvents.size())) {
    
    LiveSequencer::MidiEvent eventToRemove = muteAutomationEvents.at(selectedMuteEvent);

    for (auto& patternEntry : data.songEvents) {
      auto& events = patternEntry.second;
      for (auto it = events.begin(); it != events.end(); ) {
        if (it->track == eventToRemove.track &&
            it->layer == eventToRemove.layer &&
            it->patternNumber == eventToRemove.patternNumber &&
            it->patternMs == eventToRemove.patternMs &&
            it->note_in == eventToRemove.note_in &&
            it->event == eventToRemove.event) {
          it = events.erase(it);
        } else {
          ++it;
        }
      }
    }

    muteAutomationEvents.erase(muteAutomationEvents.begin() + selectedMuteEvent);

    if (muteAutomationEvents.empty()) {
      selectedMuteEvent = -1;
    } else {
      selectedMuteEvent = std::min(selectedMuteEvent, static_cast<int16_t>(muteAutomationEvents.size() - 1));
    }

    drawFlags |= DRAW_CONTENT_FULL | DRAW_MUTE_AUTOMATION;
    drawHeader(HEADER_DRAW_ALL);
    return;
  }
  
  // For note deletion
  if (selectedNote < 0 || selectedNote >= static_cast<int16_t>(notePairs.size())) {
    return;
  }

  // Get the event info
  LiveSequencer::MidiEvent noteOnInfo = notePairs.at(selectedNote).noteOn;
  LiveSequencer::MidiEvent noteOffInfo = notePairs.at(selectedNote).noteOff;

  if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
    // Song mode - mark as invalid (soft delete)
    // The references in notePairs point to actual events in data.songEvents
    notePairs.at(selectedNote).noteOn.event = midi::InvalidType;
    notePairs.at(selectedNote).noteOff.event = midi::InvalidType;
  } else {
    // Pattern mode - mark as invalid (soft delete)
    // This works because notePairs contains references to actual events in data.eventsList
    // When reloadNotes() is called, getNotePairsFromTrack() will skip InvalidType events
    notePairs.at(selectedNote).noteOn.event = midi::InvalidType;
    notePairs.at(selectedNote).noteOff.event = midi::InvalidType;
  }

  // Store previous selection
  int16_t prevSelectedNote = selectedNote;

  // Reload notes
  reloadNotes();

  // Update selection
  if (notePairs.empty()) {
    selectedNote = -1;
  } else {
    selectedNote = std::min(prevSelectedNote, static_cast<int16_t>(notePairs.size() - 1));
  }

  // Reset setting activation
  isSettingActivated = false;
  buttonEdit->drawNow();

  drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
  drawHeader(HEADER_DRAW_ALL);
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

      viewMode = VIEW_ZOOM_X;   // Default first item
      editMode = EDIT_SELECT_NOTE;

      // Update track and mode
      data.activeTrack = newTrack;

      // Set song mode flag based on track selection
      if (newTrack >= SONG_MODE_NOTES_TRACK) {
        data.isSongMode = true;
        data.useGridSongMode = false;
        
        // FIX: For song mode, calculate the proper view to show all content
        uint16_t maxBars = getMaxBarsForCurrentMode();
        view.startBar = 0;
        view.numBars = std::min(uint16_t(16), maxBars); // Show up to 16 bars or available max
        view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
      }
      else {
        data.isSongMode = false;
        
        // Pattern mode: reset to show all bars
        view.startBar = 0;
        view.numBars = data.numberOfBars; // Reset to 4 bars
        view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
      }

      // FIX: Shift everything up by one octave for BOTH song and pattern mode
      view.startOctave = 1; // Was 0, now 1 - shifts everything up one octave
      view.numOctaves = 5;
      view.numNotes = view.numOctaves * 12;
      view.noteHeight = CONTENT_HEIGHT / float(view.numNotes);
      view.lowestNote = 24 + view.startOctave * 12;
      view.highestNote = (view.lowestNote + view.numNotes);

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
      ui_pianoroll->drawAreaIndicator(); // FIX: Update area indicator with new view
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
