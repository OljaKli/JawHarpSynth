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


FLASHMEM uint32_t UI_LiveSeq_PianoRoll::getQuantizationGridStep() const {
  switch (quantizeLevel) {
  case QUANTIZE_OFF:
    return 2; // 2ms = effectively no quantization (better for smooth movement)
  case QUANTIZE_64:
    return data.patternLengthMs / 64;
  case QUANTIZE_32:
    return data.patternLengthMs / 32;
  case QUANTIZE_16:
    return data.patternLengthMs / 16;
  case QUANTIZE_8:
    return data.patternLengthMs / 8;
  case QUANTIZE_4:
    return data.patternLengthMs / 4;  // 1/4 note
  default:
    return 2; // Fallback to no quantization
  }
}

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
  if (data.isSongMode && !data.useGridSongMode) {


    // If we're currently doing a tentative edit, prefer the tracked absolute times
    // for the edited note instead of attempting to recompute from potentially
    // stale notePairs values.
    if (tentativeEdit.active) {
      // Note-on special case (compare track/layer/note/value/velocity)
      if (e.event == midi::NoteOn &&
        e.track == tentativeEdit.tentativeNoteOn.track &&
        e.layer == tentativeEdit.tentativeNoteOn.layer &&
        e.note_in == tentativeEdit.tentativeNoteOn.note_in &&
        e.note_in_velocity == tentativeEdit.tentativeNoteOn.note_in_velocity) {
        return tentativeEdit.currentAbsTimeOn;
      }


      // Note-off special case (compare track/layer/note)
      if (e.event == midi::NoteOff &&
        e.track == tentativeEdit.tentativeNoteOff.track &&
        e.layer == tentativeEdit.tentativeNoteOff.layer &&
        e.note_in == tentativeEdit.tentativeNoteOff.note_in) {
        return tentativeEdit.currentAbsTimeOff;
      }
    }


    // Find the event in data.songEvents to get its songPatternIndex
    for (const auto& patternEntry : data.songEvents) {
      const uint16_t songPatternIndex = patternEntry.first;
      const auto& events = patternEntry.second;


      for (const auto& ev : events) {
        // Pointer comparison first
        if (&ev == &e) {
          uint32_t absoluteBar = (songPatternIndex * data.numberOfBars) + ev.patternNumber;
          return (absoluteBar * data.patternLengthMs) + ev.patternMs;
        }
      }
    }


    // Fallback to content comparison
    for (const auto& patternEntry : data.songEvents) {
      const uint16_t songPatternIndex = patternEntry.first;
      const auto& events = patternEntry.second;


      for (const auto& ev : events) {
        if (ev.track == e.track &&
          ev.layer == e.layer &&
          ev.note_in == e.note_in &&
          ev.event == e.event &&
          ev.patternNumber == e.patternNumber &&
          ev.patternMs == e.patternMs &&
          ev.note_in_velocity == e.note_in_velocity) {
          uint32_t absoluteBar = (songPatternIndex * data.numberOfBars) + ev.patternNumber;
          return (absoluteBar * data.patternLengthMs) + ev.patternMs;
        }
      }
    }
  }


  // Fallback (relative to current 4-bar cycle)
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
    display.printf("BAR:%i-%i", startBar, endBar);
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
        // Use the same calculation as piano roll
        const uint8_t noteIndex = view.highestNote - note - 1;
        const uint16_t y = round(noteIndex * view.noteHeight);

        // alternating pattern: every other note gets gray background
        uint16_t bgColor = (note % 2) ? GREY4 : COLOR_BACKGROUND;

        display.console = true;

        const uint16_t width = u.toX - u.fromX;
        const uint16_t xFrom = std::max(ROLL_WIDTH, u.fromX);

        // Draw background with same height as piano key
        if (width > 1) {
          display.fillRect(xFrom, y, u.toX - u.fromX, ceil(view.noteHeight), bgColor);
        }
        else {
          display.drawLine(xFrom, y, xFrom, y + ceil(view.noteHeight) - 1, bgColor);
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

  struct EventRef {
    uint16_t songPatternIndex;
    LiveSequencer::MidiEvent* eventPtr;
    uint32_t absoluteTime;
    size_t eventIndex;
  };

  std::vector<EventRef> noteOnEvents;
  std::vector<EventRef> noteOffEvents;

  auto calcAbsoluteTime = [this](uint16_t songPatternIndex, const LiveSequencer::MidiEvent& event) -> uint32_t {
    uint32_t absoluteBar = (songPatternIndex * data.numberOfBars) + event.patternNumber;
    return (absoluteBar * data.patternLengthMs) + event.patternMs;
    };

  // Collect POINTERS to actual events in data.songEvents
  for (auto& patternEntry : data.songEvents) {
    const uint16_t songPatternIndex = patternEntry.first;
    auto& events = patternEntry.second;

    for (size_t i = 0; i < events.size(); i++) {
      LiveSequencer::MidiEvent* eventPtr = &events[i];

      // Skip invalid events
      if (eventPtr->event == midi::InvalidType) {
        continue;
      }

      if ((track == 0xFF || eventPtr->track == track) &&
        eventPtr->note_in >= view.lowestNote && eventPtr->note_in < view.highestNote) {

        uint32_t absTime = calcAbsoluteTime(songPatternIndex, *eventPtr);

        if (eventPtr->event == midi::NoteOn) {
          noteOnEvents.push_back({ songPatternIndex, eventPtr, absTime, i });
        }
        else if (eventPtr->event == midi::NoteOff) {
          noteOffEvents.push_back({ songPatternIndex, eventPtr, absTime, i });
        }
      }
    }
  }

  // Sort both by absolute time
  std::sort(noteOnEvents.begin(), noteOnEvents.end(),
    [](const auto& a, const auto& b) { return a.absoluteTime < b.absoluteTime; });

  std::sort(noteOffEvents.begin(), noteOffEvents.end(),
    [](const auto& a, const auto& b) { return a.absoluteTime < b.absoluteTime; });

  // Track which note-offs have been used
  std::vector<bool> noteOffUsed(noteOffEvents.size(), false);

  // Calculate view range
  uint32_t viewStartTime = uint32_t(patternFrom) * uint32_t(data.patternLengthMs);
  uint32_t viewEndTime = uint32_t(patternTo + 1) * uint32_t(data.patternLengthMs);

  // For each note-on, find the CLOSEST matching note-off
  for (const auto& noteOn : noteOnEvents) {
    int bestOffIndex = -1;
    uint32_t bestTimeDiff = UINT32_MAX;

    for (size_t i = 0; i < noteOffEvents.size(); i++) {
      if (noteOffUsed[i]) continue;

      const auto& noteOff = noteOffEvents[i];

      // Must match track, layer, and pitch
      if (noteOff.eventPtr->track != noteOn.eventPtr->track ||
        noteOff.eventPtr->layer != noteOn.eventPtr->layer ||
        noteOff.eventPtr->note_in != noteOn.eventPtr->note_in) {
        continue;
      }

      // Note-off must be after note-on
      if (noteOff.absoluteTime <= noteOn.absoluteTime) {
        continue;
      }

      uint32_t timeDiff = noteOff.absoluteTime - noteOn.absoluteTime;

      // Sanity check: skip unreasonably long notes
      if (timeDiff > 16 * data.patternLengthMs) {
        continue;
      }

      if (timeDiff < bestTimeDiff) {
        bestTimeDiff = timeDiff;
        bestOffIndex = i;
      }
    }

    if (bestOffIndex != -1) {
      const auto& noteOff = noteOffEvents[bestOffIndex];
      noteOffUsed[bestOffIndex] = true;

      if ((noteOn.absoluteTime < viewEndTime && noteOff.absoluteTime > viewStartTime)) {
        LiveSequencer::NotePair p = {
          *noteOn.eventPtr,
          *noteOff.eventPtr,
          false
        };

        result.emplace_back(p);

        // Store the original event locations
        SongEventLocation loc;
        loc.noteOnPattern = noteOn.songPatternIndex;
        loc.noteOnIndex = noteOn.eventIndex;
        loc.noteOffPattern = noteOff.songPatternIndex;
        loc.noteOffIndex = noteOff.eventIndex;
        eventLocations.push_back(loc);

        highestFound = std::max(highestFound, noteOn.eventPtr->note_in);
        lowestFound = std::min(lowestFound, noteOn.eventPtr->note_in);
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



FLASHMEM void UI_LiveSeq_PianoRoll::reloadNotes(bool autoAdaptView) {
  // Clear previous locations
  eventLocations.clear();
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

  //  draw bar numbers last, after everything else
  if (!data.isRunning)
    drawBarNumbers();

  // Clear all draw flags after processing
  drawFlags = 0;
  drawRegionsX.clear();
}

template<typename T>
FLASHMEM bool UI_LiveSeq_PianoRoll::hasConstrainedChanged(T& value, int16_t diff, T min, T max) {
  const T newValue = constrain(value + diff, min, max);
  const bool changed = newValue != value;
  value = newValue;
  return changed;
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
      queueDrawNote({ ROLL_WIDTH, DISPLAY_WIDTH, { key } });
    }

    if ((data.isRunning == false) && (mode == MODE_STEP)) {
      if (event == midi::NoteOn) {
        // Calculate absolute time in milliseconds from viewport position
        uint32_t absoluteTimeMs = view.startBar * data.patternLengthMs +
          static_cast<uint32_t>(stepCursorPositionIndex * stepRecordStepSizeMs);

        // Create note on event
        LiveSequencer::MidiEvent on;
        on.source = data.isSongMode ? LiveSequencer::EVENT_SONG : LiveSequencer::EVENT_PATTERN;
        on.event = midi::NoteOn;
        on.note_in = key;
        on.note_in_velocity = velocity;

        if (data.isSongMode && !data.useGridSongMode && data.activeTrack >= SONG_MODE_NOTES_TRACK) {
          on.track = data.stepRecordTargetTrack;
        }
        else {
          on.track = data.activeTrack;
        }

        on.layer = stepRecordLayer;

        // Create note off event (copy from note on)
        LiveSequencer::MidiEvent off = on;
        off.event = midi::NoteOff;

        if (data.isSongMode && !data.useGridSongMode) {
          // ====== SONG MODE: Convert absolute time to chunk-relative storage ======
          const uint32_t chunkLengthMs = data.numberOfBars * data.patternLengthMs;

          // Calculate note ON position
          uint16_t songPatternIndexOn = absoluteTimeMs / chunkLengthMs;
          uint32_t timeInChunkOn = absoluteTimeMs % chunkLengthMs;

          // Convert to cyclic pattern number (0-3) and offset within that bar
          on.patternNumber = (timeInChunkOn / data.patternLengthMs) % data.numberOfBars;
          on.patternMs = timeInChunkOn % data.patternLengthMs;

          // Bounds check for patternMs (should never exceed patternLengthMs)
          if (on.patternMs >= data.patternLengthMs) {
            on.patternMs = data.patternLengthMs - 1;
          }

          // Calculate note OFF position (half step length later)
          uint32_t noteOffTimeAbs = absoluteTimeMs + static_cast<uint32_t>(stepRecordStepSizeMs / 2);
          uint16_t songPatternIndexOff = noteOffTimeAbs / chunkLengthMs;
          uint32_t timeInChunkOff = noteOffTimeAbs % chunkLengthMs;

          off.patternNumber = (timeInChunkOff / data.patternLengthMs) % data.numberOfBars;
          off.patternMs = timeInChunkOff % data.patternLengthMs;

          // Bounds check for patternMs
          if (off.patternMs >= data.patternLengthMs) {
            off.patternMs = data.patternLengthMs - 1;
          }

          // Ensure target chunks exist in songEvents map
          if (data.songEvents.find(songPatternIndexOn) == data.songEvents.end()) {
            data.songEvents[songPatternIndexOn]; // Auto-creates empty vector with correct allocator
          }
          if (songPatternIndexOn != songPatternIndexOff &&
            data.songEvents.find(songPatternIndexOff) == data.songEvents.end()) {
            data.songEvents[songPatternIndexOff]; // Auto-creates empty vector with correct allocator
          }

          // Add both events to their respective chunks
          data.songEvents[songPatternIndexOn].push_back(on);
          data.songEvents[songPatternIndexOff].push_back(off);

          // Request event sorting for proper playback order
          liveSeq.requestSortEvents();

        }
        else {
          // ====== PATTERN MODE: Simple relative time calculation ======
          on.patternNumber = absoluteTimeMs / data.patternLengthMs;
          on.patternMs = absoluteTimeMs % data.patternLengthMs;

          uint32_t noteOffTime = absoluteTimeMs + static_cast<uint32_t>(stepRecordStepSizeMs / 2);
          off.patternNumber = noteOffTime / data.patternLengthMs;
          off.patternMs = noteOffTime % data.patternLengthMs;

          liveSeq.addNotePair(on, off);
        }

        // Force full redraw if this is the first note
        if (notePairs.empty()) {
          drawFlags |= DRAW_CONTENT_FULL;
        }

        // Update layer count if needed (use the actual instrument track, not SONG_MODE_NOTES_TRACK)
        uint8_t targetTrack = on.track; // Use the track we actually assigned to the event
        if (stepRecordLayer + 1 > data.trackSettings[targetTrack].layerCount) {
          data.trackSettings[targetTrack].layerCount++;
          drawLayersIndicator();
        }

        // Reload notes to show the newly added note
        reloadNotes();
      }
      else if (keysPressed.empty()) {
        // Advance to next step when all keys released
        step(+1);
      }
    }
  }
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
  if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
    if (isSettingActivated && tentativeEdit.active) {
      // Committing changes
      commitTentativeEdit();
    }
    else if (!isSettingActivated && tentativeEdit.active) {
      // Should not happen, but cancel just in case
      cancelTentativeEdit();
    }
  }

  isSettingActivated = !isSettingActivated;

  if (mode == MODE_EDIT) {
    buttonEdit->drawNow();
  }
  drawHeader(HEADER_DRAW_ALL);
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getSongPatternForNote(const LiveSequencer::NotePair& note) const {
  if (!data.isSongMode || data.useGridSongMode) {
    return 0;
  }

  // note.noteOn may be a copy inside notePairs, so pointer comparisons can fail.
  // Reuse the content-based event matcher already implemented.
  return getSongPatternForEvent(note.noteOn);
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
    handleSettingActivatedEncoder(e);
  }
  else {
    handleNormalEncoder(e);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleSettingActivatedEncoder(EncoderEvents e) {
  uint16_t headerFlags = 0;

  switch (mode) {
  case MODE_VIEW:
    headerFlags = handleViewModeSettingActivated(e);
    break;

  case MODE_EDIT:
    headerFlags = handleEditModeSettingActivated(e);
    break;

  case MODE_STEP:
    headerFlags = handleStepModeSettingActivated(e);
    break;
  }

  if (drawFlags && data.isRunning) {
    drawFlags |= DRAW_CURSOR; // handle cursor off on viewport changed redraw
  }

  drawHeader(headerFlags);
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleNormalEncoder(EncoderEvents e) {
  uint8_t oldMode = 0;
  uint8_t newMode = 0;

  switch (mode) {
  case MODE_VIEW:
    handleViewModeNormal(e, oldMode, newMode);
    break;

  case MODE_EDIT:
    handleEditModeNormal(e, oldMode, newMode);
    break;

  case MODE_STEP:
    handleStepModeNormal(e, oldMode, newMode);
    break;
  }

  if (oldMode != newMode) {
    if (mode == MODE_VIEW || mode == MODE_EDIT) {
      drawHeader((1 << oldMode) | (1 << newMode));
    }
  }
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleViewModeSettingActivated(EncoderEvents e) {
  uint16_t headerFlags = 0;

  switch (viewMode) {
  case VIEW_ZOOM_Y:
    if (hasConstrainedChanged(view.numOctaves, -2 * e.dir, int8_t(1), int8_t(5))) {
      view.startOctave += e.dir; // zoom octave-centered
      setNumOctaves(view.numOctaves);
      drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
    }
    break;

  case VIEW_SCROLL_Y:
    handleViewScrollY(e);
    break;

  case VIEW_ZOOM_X:
    handleViewZoomX(e);
    break;

  case VIEW_SCROLL_X:
    handleViewScrollX(e);
    break;
  }

  return headerFlags;
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleViewScrollY(EncoderEvents e) {
  int8_t newStartOctave = view.startOctave + e.dir;
  int8_t maxStartOctave = TOTAL_OCTAVES - view.numOctaves;
  if (maxStartOctave < 0) maxStartOctave = 0;

  if (newStartOctave < 0) newStartOctave = 0;
  if (newStartOctave > maxStartOctave) newStartOctave = maxStartOctave;

  if (newStartOctave != view.startOctave) {
    view.startOctave = newStartOctave;
    view.lowestNote = 24 + view.startOctave * 12;
    view.highestNote = (view.lowestNote + view.numNotes);

    drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
    drawAreaIndicator();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleViewZoomX(EncoderEvents e) {
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
    int16_t newNumBars = view.numBars + e.dir;
    if (newNumBars < 1) newNumBars = 1;
    if (newNumBars > int16_t(data.numberOfBars)) newNumBars = data.numberOfBars;
    if (newNumBars != view.numBars) {
      setNumBars(newNumBars);
      drawFlags |= DRAW_CONTENT_FULL;
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleViewScrollX(EncoderEvents e) {
  if ((data.isSongMode && !data.useGridSongMode) || data.activeTrack == SONG_MODE_MUTE_TRACK) {
    uint16_t maxBars = getMaxBarsForCurrentMode();

    int16_t newStartBar = view.startBar + e.dir;

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
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleEditModeSettingActivated(EncoderEvents e) {
  uint16_t headerFlags = (1 << editMode);

  switch (editMode) {
  case EDIT_SELECT_NOTE:
    headerFlags = handleEditSelectNote(e, headerFlags);
    break;

  case EDIT_LAYER:
    headerFlags = handleEditLayer(e, headerFlags);
    break;

  case EDIT_NOTEON:
    headerFlags = handleEditNoteOn(e, headerFlags);
    break;

  case EDIT_NOTEOFF:
    headerFlags = handleEditNoteOff(e, headerFlags);
    break;

  case EDIT_VELOCITY:
    headerFlags = handleEditVelocity(e, headerFlags);
    break;

  case EDIT_NOTE:
    headerFlags = handleEditNote(e, headerFlags);
    break;
  }

  return headerFlags;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleEditSelectNote(EncoderEvents e, uint16_t headerFlags) {
  if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    return handleMuteAutomationSelection(e, headerFlags);
  }
  else {
    return handleNoteSelection(e, headerFlags);
  }
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleMuteAutomationSelection(EncoderEvents e, uint16_t headerFlags) {
  if (!muteAutomationEvents.empty()) {
    std::vector<std::pair<int16_t, uint16_t>> eventsWithPos;

    for (size_t i = 0; i < muteAutomationEvents.size(); i++) {
      auto& event = muteAutomationEvents[i];
      uint16_t songPatternIndex = getSongPatternForEvent(event);
      uint32_t absoluteTime = (songPatternIndex * data.numberOfBars * data.patternLengthMs) +
        (event.patternNumber * data.patternLengthMs) + event.patternMs;
      const uint16_t screenX = getNoteCoord(absoluteTime);

      if (screenX >= ROLL_WIDTH && screenX < DISPLAY_WIDTH) {
        eventsWithPos.push_back({ (int16_t)i, screenX });
      }
    }

    std::sort(eventsWithPos.begin(), eventsWithPos.end(),
      [](const auto& a, const auto& b) {
        return a.second < b.second;
      });

    std::vector<int16_t> sortedEvents;
    for (const auto& pair : eventsWithPos) {
      sortedEvents.push_back(pair.first);
    }

    if (!sortedEvents.empty()) {
      int16_t newSelection;

      if (selectedMuteEvent == -1) {
        newSelection = sortedEvents[0];
      }
      else {
        int16_t currentPos = -1;
        for (size_t i = 0; i < sortedEvents.size(); i++) {
          if (sortedEvents[i] == selectedMuteEvent) {
            currentPos = i;
            break;
          }
        }

        if (currentPos == -1) {
          newSelection = sortedEvents[0];
        }
        else {
          int16_t newPos = currentPos + e.dir;
          if (newPos < 0) newPos = sortedEvents.size() - 1;
          else if (newPos >= static_cast<int16_t>(sortedEvents.size())) newPos = 0;

          newSelection = sortedEvents[newPos];
        }
      }

      if (newSelection != selectedMuteEvent) {
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
  return headerFlags;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleNoteSelection(EncoderEvents e, uint16_t headerFlags) {
  if (!notePairs.empty()) {
    bool selectedLayerHasNotes = false;
    for (const auto& note : notePairs) {
      if (note.noteOn.layer == selectedLayer) {
        selectedLayerHasNotes = true;
        break;
      }
    }

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

    std::vector<std::pair<int16_t, uint16_t>> notesWithPos;

    for (size_t i = 0; i < notePairs.size(); i++) {
      auto& note = notePairs[i];
      if (note.noteOn.layer != selectedLayer) {
        continue;
      }

      uint32_t timeOn;
      if (data.isSongMode && !data.useGridSongMode) {
        uint16_t absoluteBarOn = getActualBarForNote(note);
        timeOn = absoluteBarOn * data.patternLengthMs + note.noteOn.patternMs;
      }
      else {
        timeOn = getEventTime(note.noteOn);
      }

      const uint16_t screenX = getNoteCoord(timeOn);

      if (screenX >= ROLL_WIDTH && screenX < DISPLAY_WIDTH) {
        notesWithPos.push_back({ (int16_t)i, screenX });
      }
    }

    if (!notesWithPos.empty()) {
      int16_t newSelection;

      if (selectedNote == -1) {
        newSelection = notesWithPos[0].first;
      }
      else {
        int16_t currentPos = -1;
        for (size_t i = 0; i < notesWithPos.size(); i++) {
          if (notesWithPos[i].first == selectedNote) {
            currentPos = i;
            break;
          }
        }

        if (currentPos == -1) {
          newSelection = notesWithPos[0].first;
        }
        else {
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
      if (selectedNote > -1) {
        queueDrawNote(notePairs.at(selectedNote));
        selectedNote = -1;
        drawFlags |= DRAW_NOTES;
      }
    }
  }
  return headerFlags;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleEditLayer(EncoderEvents e, uint16_t headerFlags) {
  if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
      LiveSequencer::MidiEvent& event = muteAutomationEvents.at(selectedMuteEvent);
      int8_t layer = event.note_in_velocity;
      if (hasConstrainedChanged(layer, e.dir, int8_t(0), int8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1))) {
        uint16_t songPattern = getSongPatternForEvent(event);
        uint32_t absoluteTime = (songPattern * data.numberOfBars * data.patternLengthMs) +
          (event.patternNumber * data.patternLengthMs) + event.patternMs;
        UpdateRange region;
        region.fromX = getNoteCoord(absoluteTime) - 5;
        region.toX = getNoteCoord(absoluteTime) + 5;
        drawRegionsX.push_back(region);

        event.note_in_velocity = layer;
        updateSongEvent(event);

        drawRegionsX.push_back(region);
        headerFlags |= (1 << EDIT_LAYER);
        drawFlags |= DRAW_MUTE_AUTOMATION | DRAW_BACKGROUND;
      }
    }
  }
  else {
    std::set<int8_t> availableLayers;
    for (const auto& note : notePairs) {
      availableLayers.insert(note.noteOn.layer);
    }

    if (availableLayers.empty()) {
      for (int8_t i = 0; i < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; i++) {
        availableLayers.insert(i);
      }
    }

    int8_t newSelectedLayer = selectedLayer;
    if (e.dir > 0) {
      auto it = availableLayers.upper_bound(selectedLayer);
      if (it == availableLayers.end()) {
        newSelectedLayer = *availableLayers.begin();
      }
      else {
        newSelectedLayer = *it;
      }
    }
    else if (e.dir < 0) {
      auto it = availableLayers.lower_bound(selectedLayer);
      if (it == availableLayers.begin()) {
        newSelectedLayer = *availableLayers.rbegin();
      }
      else {
        --it;
        newSelectedLayer = *it;
      }
    }

    if (newSelectedLayer != selectedLayer) {
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
  return headerFlags;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleEditNoteOn(EncoderEvents e, uint16_t headerFlags) {

  if (data.activeTrack == SONG_MODE_NOTES_TRACK && selectedNote > -1) {
    LiveSequencer::NotePair& note = notePairs.at(selectedNote);

    // Start tentative edit if not already active
    if (!tentativeEdit.active) {
      startTentativeEdit(selectedNote);
    }

    // Use the tracked absolute times from tentativeEdit instead of recalculating
    uint32_t oldAbsOn = tentativeEdit.currentAbsTimeOn;
    uint32_t oldAbsOff = tentativeEdit.currentAbsTimeOff;
    uint32_t noteLength = (oldAbsOff > oldAbsOn) ? (oldAbsOff - oldAbsOn) : getQuantizationGridStep();

    // Calculate movement
    uint32_t gridStep = getQuantizationGridStep();
    int64_t movement64 = (int64_t)e.dir * (int64_t)e.speed * (int64_t)gridStep;

    // Calculate viewport boundaries
    const uint32_t viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
    const uint32_t viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);

    // Calculate new times constrained to viewport
    int64_t tentativeOn = (int64_t)oldAbsOn + movement64;
    int64_t minOn = (int64_t)viewStartMs;
    int64_t maxOn = (int64_t)(viewEndMs - noteLength);

    if (maxOn < minOn) maxOn = minOn;

    if (tentativeOn < minOn) tentativeOn = minOn;
    if (tentativeOn > maxOn) tentativeOn = maxOn;

    uint32_t newAbsOn = (uint32_t)tentativeOn;
    uint32_t newAbsOff = newAbsOn + noteLength;

    // Final check that note off doesn't exceed viewport
    if (newAbsOff > viewEndMs) {
      newAbsOff = viewEndMs;
      newAbsOn = (newAbsOff > noteLength) ? (newAbsOff - noteLength) : viewStartMs;
    }

    // Only update if changed
    if (newAbsOn != oldAbsOn) {
      queueDrawNote(note); // Clear old position

      // Update tentative edit - this will calculate correct chunks and store new absolute times
      updateTentativeEdit(newAbsOn, newAbsOff);

      // Queue redraw for new position
      queueDrawNote(note);

      headerFlags |= (1 << EDIT_NOTEON) | (1 << EDIT_NOTEOFF);
      drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    }
  }

  //pattern mode
  else if (selectedNote > -1) {
    LiveSequencer::MidiEvent& on = notePairs.at(selectedNote).noteOn;
    LiveSequencer::MidiEvent& off = notePairs.at(selectedNote).noteOff;

    uint32_t oldNoteOnTime = getEventTime(on);
    uint32_t noteOffTime = getEventTime(off);
    uint32_t noteLength = (noteOffTime > oldNoteOnTime) ? (noteOffTime - oldNoteOnTime) : 1;

    uint32_t gridStep = getQuantizationGridStep();
    const uint32_t minTime = 0;
    uint32_t viewEndMs = uint32_t(view.startBar + view.numBars) * uint32_t(data.patternLengthMs);
    uint32_t maxTimeFromScreen = (viewEndMs > noteLength) ? (viewEndMs - noteLength) : 0;
    uint32_t maxTime = maxTimeFromScreen;

    int32_t gridSteps = roundf(float(oldNoteOnTime) / float(gridStep));
    gridSteps = constrain(gridSteps + e.dir * e.speed,
      roundf(float(minTime) / float(gridStep)),
      roundf(float(maxTime) / float(gridStep)));
    uint32_t newTime = gridSteps * gridStep;

    if (newTime != oldNoteOnTime) {
      queueDrawNote(notePairs.at(selectedNote));
      setEventTime(on, newTime);
      uint32_t newNoteOffTime = newTime + noteLength;
      setEventTime(off, newNoteOffTime);
      memcpy(&currentNote, &on, sizeof(currentNote));
      queueDrawNote(notePairs.at(selectedNote));
      headerFlags |= (1 << EDIT_NOTEON) | (1 << EDIT_NOTEOFF);
      drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    }
  }
  return headerFlags;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleEditNoteOff(EncoderEvents e, uint16_t headerFlags) {

  if (data.activeTrack == SONG_MODE_NOTES_TRACK && selectedNote > -1) {
    LiveSequencer::NotePair& note = notePairs.at(selectedNote);

    // Start tentative edit if not already active
    if (!tentativeEdit.active) {
      startTentativeEdit(selectedNote);
    }

    // Use the tracked absolute times from tentativeEdit
    uint32_t absOn = tentativeEdit.currentAbsTimeOn;
    uint32_t oldAbsOff = tentativeEdit.currentAbsTimeOff;

    uint32_t gridStep = getQuantizationGridStep();
    int64_t movement64 = (int64_t)e.dir * (int64_t)e.speed * (int64_t)gridStep;

    // Calculate viewport boundaries
    const uint32_t viewStartMs = uint32_t(view.startBar) * uint32_t(data.patternLengthMs);
    const uint32_t viewEndMs = viewStartMs + uint32_t(view.numBars) * uint32_t(data.patternLengthMs);

    int64_t tentativeOff = (int64_t)oldAbsOff + movement64;
    int64_t minOff = (int64_t)absOn + (int64_t)gridStep;
    int64_t maxOff = (int64_t)viewEndMs; // Hard stop at viewport end

    if (tentativeOff < minOff) tentativeOff = minOff;
    if (tentativeOff > maxOff) tentativeOff = maxOff;

    uint32_t newAbsOff = (uint32_t)tentativeOff;

    if (newAbsOff != oldAbsOff) {
      queueDrawNote(note); // Clear old position

      // Update tentative edit - note on stays the same, only note off changes
      updateTentativeEdit(absOn, newAbsOff);

      // Queue redraw for new position
      queueDrawNote(note);

      headerFlags |= (1 << EDIT_NOTEOFF);
      drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    }
  }

  //pattern mode
  else if (selectedNote > -1) {
    LiveSequencer::MidiEvent& on = notePairs.at(selectedNote).noteOn;
    LiveSequencer::MidiEvent& off = notePairs.at(selectedNote).noteOff;

    uint32_t noteOnTime = getEventTime(on);
    uint32_t oldNoteOffTime = getEventTime(off);

    uint32_t gridStep = getQuantizationGridStep();
    const uint32_t minTime = noteOnTime + gridStep;
    uint32_t viewEndMs = uint32_t(view.startBar + view.numBars) * uint32_t(data.patternLengthMs);
    uint32_t maxTime = viewEndMs;

    int32_t gridSteps = roundf(float(oldNoteOffTime) / float(gridStep));
    gridSteps = constrain(gridSteps + e.dir * e.speed,
      roundf(float(minTime) / float(gridStep)),
      roundf(float(maxTime) / float(gridStep)));
    uint32_t newTime = gridSteps * gridStep;

    if (newTime != oldNoteOffTime) {
      queueDrawNote(notePairs.at(selectedNote));
      setEventTime(off, newTime);
      memcpy(&currentNote, &on, sizeof(currentNote));
      queueDrawNote(notePairs.at(selectedNote));
      headerFlags |= (1 << EDIT_NOTEOFF);
      drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    }
  }
  return headerFlags;
}

FLASHMEM void UI_LiveSeq_PianoRoll::startTentativeEdit(int16_t noteIndex) {
  if (noteIndex < 0 || noteIndex >= static_cast<int16_t>(notePairs.size())) {
    return;
  }

  if (static_cast<size_t>(noteIndex) >= eventLocations.size()) {
    return; // No location stored for this note
  }

  SongEventLocation& loc = eventLocations[noteIndex];

  tentativeEdit.active = true;
  tentativeEdit.originalSongPatternOn = loc.noteOnPattern;
  tentativeEdit.originalNoteOnIndex = loc.noteOnIndex;
  tentativeEdit.originalSongPatternOff = loc.noteOffPattern;
  tentativeEdit.originalNoteOffIndex = loc.noteOffIndex;

  // Get the actual events
  if (data.songEvents.count(loc.noteOnPattern) &&
    loc.noteOnIndex < data.songEvents[loc.noteOnPattern].size()) {
    tentativeEdit.tentativeNoteOn = data.songEvents[loc.noteOnPattern][loc.noteOnIndex];
  }

  if (data.songEvents.count(loc.noteOffPattern) &&
    loc.noteOffIndex < data.songEvents[loc.noteOffPattern].size()) {
    tentativeEdit.tentativeNoteOff = data.songEvents[loc.noteOffPattern][loc.noteOffIndex];
  }

  // Calculate absolute times
  const uint32_t chunkLengthMs = data.numberOfBars * data.patternLengthMs;
  tentativeEdit.currentAbsTimeOn = (loc.noteOnPattern * chunkLengthMs) +
    (tentativeEdit.tentativeNoteOn.patternNumber * data.patternLengthMs) +
    tentativeEdit.tentativeNoteOn.patternMs;
  tentativeEdit.currentAbsTimeOff = (loc.noteOffPattern * chunkLengthMs) +
    (tentativeEdit.tentativeNoteOff.patternNumber * data.patternLengthMs) +
    tentativeEdit.tentativeNoteOff.patternMs;
}

FLASHMEM void UI_LiveSeq_PianoRoll::updateTentativeEdit(uint32_t newAbsTimeOn, uint32_t newAbsTimeOff) {
  if (!tentativeEdit.active) return;

  tentativeEdit.currentAbsTimeOn = newAbsTimeOn;
  tentativeEdit.currentAbsTimeOff = newAbsTimeOff;

  // Update the display only - don't modify data.songEvents until commit
  if (selectedNote >= 0 && selectedNote < static_cast<int16_t>(notePairs.size())) {
    const uint32_t chunkLengthMs = data.numberOfBars * data.patternLengthMs;

    // Update note-on display
    uint32_t timeInChunkOn = newAbsTimeOn % chunkLengthMs;
    notePairs[selectedNote].noteOn.patternNumber = (timeInChunkOn / data.patternLengthMs) % data.numberOfBars;
    notePairs[selectedNote].noteOn.patternMs = timeInChunkOn % data.patternLengthMs;

    // Update note-off display
    uint32_t timeInChunkOff = newAbsTimeOff % chunkLengthMs;
    notePairs[selectedNote].noteOff.patternNumber = (timeInChunkOff / data.patternLengthMs) % data.numberOfBars;
    notePairs[selectedNote].noteOff.patternMs = timeInChunkOff % data.patternLengthMs;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::commitTentativeEdit() {
  if (!tentativeEdit.active) return;
  if (tentativeEdit.originalSongPatternOn == UINT16_MAX ||
    tentativeEdit.originalSongPatternOff == UINT16_MAX) {
    tentativeEdit.active = false;
    return;
  }

  const uint32_t chunkLengthMs = data.numberOfBars * data.patternLengthMs;

  // Calculate new positions
  uint16_t newSongPatternOn = tentativeEdit.currentAbsTimeOn / chunkLengthMs;
  uint32_t timeInChunkOn = tentativeEdit.currentAbsTimeOn % chunkLengthMs;
  uint8_t newPatternNumOn = (timeInChunkOn / data.patternLengthMs) % data.numberOfBars;
  uint32_t newPatternMsOn = timeInChunkOn % data.patternLengthMs;

  uint16_t newSongPatternOff = tentativeEdit.currentAbsTimeOff / chunkLengthMs;
  uint32_t timeInChunkOff = tentativeEdit.currentAbsTimeOff % chunkLengthMs;
  uint8_t newPatternNumOff = (timeInChunkOff / data.patternLengthMs) % data.numberOfBars;
  uint32_t newPatternMsOff = timeInChunkOff % data.patternLengthMs;

  // MODIFY EXISTING EVENTS IN PLACE
  if (data.songEvents.count(tentativeEdit.originalSongPatternOn) &&
    tentativeEdit.originalNoteOnIndex < data.songEvents[tentativeEdit.originalSongPatternOn].size()) {

    auto& eventOn = data.songEvents[tentativeEdit.originalSongPatternOn][tentativeEdit.originalNoteOnIndex];

    // If moving to different chunk, we need to move the event
    if (newSongPatternOn != tentativeEdit.originalSongPatternOn) {
      // Remove from old chunk
      LiveSequencer::MidiEvent movedOn = eventOn; // Take a copy
      data.songEvents[tentativeEdit.originalSongPatternOn].erase(
        data.songEvents[tentativeEdit.originalSongPatternOn].begin() + tentativeEdit.originalNoteOnIndex);

      // Update and add to new chunk
      movedOn.patternNumber = newPatternNumOn;
      movedOn.patternMs = newPatternMsOn;

      if (data.songEvents.find(newSongPatternOn) == data.songEvents.end()) {
        data.songEvents[newSongPatternOn] = {};
      }
      data.songEvents[newSongPatternOn].push_back(movedOn);
    }
    else {
      // Same chunk, just modify in place
      eventOn.patternNumber = newPatternNumOn;
      eventOn.patternMs = newPatternMsOn;
    }
  }

  // Same for note-off
  if (data.songEvents.count(tentativeEdit.originalSongPatternOff) &&
    tentativeEdit.originalNoteOffIndex < data.songEvents[tentativeEdit.originalSongPatternOff].size()) {

    auto& eventOff = data.songEvents[tentativeEdit.originalSongPatternOff][tentativeEdit.originalNoteOffIndex];

    if (newSongPatternOff != tentativeEdit.originalSongPatternOff) {
      // Remove from old chunk
      LiveSequencer::MidiEvent movedOff = eventOff;
      data.songEvents[tentativeEdit.originalSongPatternOff].erase(
        data.songEvents[tentativeEdit.originalSongPatternOff].begin() + tentativeEdit.originalNoteOffIndex);

      // Update and add to new chunk
      movedOff.patternNumber = newPatternNumOff;
      movedOff.patternMs = newPatternMsOff;

      if (data.songEvents.find(newSongPatternOff) == data.songEvents.end()) {
        data.songEvents[newSongPatternOff] = {};
      }
      data.songEvents[newSongPatternOff].push_back(movedOff);
    }
    else {
      // Same chunk, modify in place
      eventOff.patternNumber = newPatternNumOff;
      eventOff.patternMs = newPatternMsOff;
    }
  }

  // Clean up empty chunks
  if (data.songEvents[tentativeEdit.originalSongPatternOn].empty()) {
    data.songEvents.erase(tentativeEdit.originalSongPatternOn);
  }
  if (tentativeEdit.originalSongPatternOn != tentativeEdit.originalSongPatternOff &&
    data.songEvents[tentativeEdit.originalSongPatternOff].empty()) {
    data.songEvents.erase(tentativeEdit.originalSongPatternOff);
  }

  tentativeEdit.active = false;
  reloadNotes();
  liveSeq.requestSortEvents();
}

FLASHMEM size_t findEventIndex(const std::vector<LiveSequencer::MidiEvent>& events, const LiveSequencer::MidiEvent& target) {
  for (size_t i = 0; i < events.size(); i++) {
    const auto& event = events[i];
    if (event.track == target.track &&
      event.layer == target.layer &&
      event.note_in == target.note_in &&
      event.event == target.event &&
      event.patternNumber == target.patternNumber &&
      event.patternMs == target.patternMs &&
      event.note_in_velocity == target.note_in_velocity) {
      return i;
    }
  }
  return SIZE_MAX;
}


FLASHMEM uint32_t UI_LiveSeq_PianoRoll::getMaxSongTime() const {
  if (!data.isSongMode || data.useGridSongMode) {
    return data.numberOfBars * data.patternLengthMs;
  }

  // Find the highest chunk index in use
  uint16_t maxChunk = 0;
  for (const auto& patternEntry : data.songEvents) {
    if (patternEntry.first > maxChunk) {
      maxChunk = patternEntry.first;
    }
  }

  // Return time at the end of the highest chunk
  return (maxChunk + 1) * data.numberOfBars * data.patternLengthMs;
}

FLASHMEM void UI_LiveSeq_PianoRoll::cancelTentativeEdit() {
  if (!tentativeEdit.active) return;

  // Just reload notes to restore original state
  // Since we never modified the original events, they're still intact
  reloadNotes();

  tentativeEdit.active = false;
}


FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleEditVelocity(EncoderEvents e, uint16_t headerFlags) {
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
  return headerFlags;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleEditNote(EncoderEvents e, uint16_t headerFlags) {
  if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
      LiveSequencer::MidiEvent& eventCopy = muteAutomationEvents.at(selectedMuteEvent);

      uint16_t songPattern = getSongPatternForEvent(eventCopy);
      uint32_t absoluteTime = (songPattern * data.numberOfBars * data.patternLengthMs) +
        (eventCopy.patternNumber * data.patternLengthMs) + eventCopy.patternMs;
      UpdateRange region;
      region.fromX = getNoteCoord(absoluteTime) - 5;
      region.toX = getNoteCoord(absoluteTime) + 5;
      drawRegionsX.push_back(region);

      LiveSequencer::MidiEvent* realEvent = nullptr;
      for (auto& patternEntry : data.songEvents) {
        for (auto& ev : patternEntry.second) {
          if (ev.track == eventCopy.track &&
            ev.layer == eventCopy.layer &&
            ev.note_in == eventCopy.note_in &&
            ev.event == eventCopy.event &&
            ev.patternMs == eventCopy.patternMs &&
            ev.note_in_velocity == eventCopy.note_in_velocity) {
            realEvent = &ev;
            break;
          }
        }
        if (realEvent) break;
      }

      if (e.dir != 0) {
        uint8_t newMuteState = (eventCopy.note_in == LiveSequencer::TYPE_MUTE_ON) ?
          LiveSequencer::TYPE_MUTE_OFF : LiveSequencer::TYPE_MUTE_ON;

        if (realEvent) {
          realEvent->note_in = newMuteState;
          eventCopy.note_in = newMuteState;
        }
        else {
          eventCopy.note_in = newMuteState;
          updateSongEvent(eventCopy);
        }

        drawRegionsX.push_back(region);
        headerFlags |= (1 << EDIT_NOTE);
        drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_MUTE_AUTOMATION;
      }
    }
  }

  else if (data.activeTrack == SONG_MODE_NOTES_TRACK && selectedNote > -1) {
    LiveSequencer::NotePair& np = notePairs[selectedNote];

    int16_t oldPitch = np.noteOn.note_in;
    int16_t newPitch = constrain(
      oldPitch + (e.dir * e.speed),
      view.lowestNote,
      view.highestNote - 1
    );

    if (newPitch != oldPitch) {
      queueDrawNote(np); // Erase old

      // Find and update the ACTUAL events in data.songEvents
      for (auto& patternEntry : data.songEvents) {
        for (auto& ev : patternEntry.second) {
          // Match note on by ALL fields except pitch
          if (ev.track == np.noteOn.track &&
            ev.layer == np.noteOn.layer &&
            ev.event == midi::NoteOn &&
            ev.patternNumber == np.noteOn.patternNumber &&
            ev.patternMs == np.noteOn.patternMs &&
            ev.note_in_velocity == np.noteOn.note_in_velocity &&
            ev.note_in == oldPitch) { // Match old pitch
            ev.note_in = newPitch;
          }

          // Match note off by ALL fields except pitch
          if (ev.track == np.noteOff.track &&
            ev.layer == np.noteOff.layer &&
            ev.event == midi::NoteOff &&
            ev.patternNumber == np.noteOff.patternNumber &&
            ev.patternMs == np.noteOff.patternMs &&
            ev.note_in == oldPitch) { // Match old pitch
            ev.note_in = newPitch;
          }
        }
      }

      // Update local notePairs for display
      np.noteOn.note_in = newPitch;
      np.noteOff.note_in = newPitch;

      // Redraw
      queueDrawNote(np);
      drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
      headerFlags |= (1 << EDIT_NOTE);
    }
  }

  //pattern mode  
  else if (selectedNote > -1) {
    LiveSequencer::NotePair& thisNote = notePairs.at(selectedNote);
    int8_t note = thisNote.noteOn.note_in;
    if (hasConstrainedChanged(note, e.dir, int8_t(view.lowestNote), int8_t(view.highestNote))) {
      uint8_t oldNote = thisNote.noteOn.note_in;

      thisNote.noteOn.note_in = note;
      thisNote.noteOff.note_in = note;
      headerFlags |= (1 << EDIT_NOTE);

      const uint32_t timeOn = getEventTime(thisNote.noteOn);
      const uint32_t timeOff = getEventTime(thisNote.noteOff);
      uint16_t fromX = getNoteCoord(timeOn);
      uint16_t toX = getNoteCoord(timeOff);

      fromX = (fromX < 2 + ROLL_WIDTH) ? ROLL_WIDTH : (fromX - 2);
      toX = (toX > DISPLAY_WIDTH - 2) ? DISPLAY_WIDTH : (toX + 2);

      UpdateRange updateRange;
      updateRange.fromX = fromX;
      updateRange.toX = toX;
      updateRange.notes = { oldNote, static_cast<uint8_t>(note) };
      drawRegionsX.push_back(updateRange);

      drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    }
  }
  return headerFlags;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::handleStepModeSettingActivated(EncoderEvents e) {
  uint16_t headerFlags = 0;

  switch (stepMode) {
  case STEP_TARGET_TRACK:
    // Only used in song mode
    if (data.isSongMode && !data.useGridSongMode) {
      if (hasConstrainedChanged(data.stepRecordTargetTrack, e.dir,
        uint8_t(0), uint8_t(11))) { // 0-11 for tracks 1-12
        headerFlags |= (1 << STEP_TARGET_TRACK);
      }
    }
    break;

  case STEP_RECORD:
    if (hasConstrainedChanged(stepCursorPositionIndex, e.dir * e.speed,
      uint16_t(0), uint16_t(view.numBars * stepRecordSteps - 1))) {
      stepCursorPositionPrev = stepCursorPosition;
      stepCursorPosition = ROLL_WIDTH + round(stepCursorPositionIndex * stepRecordStepSizeMs * view.msToPix);
      queueDrawNote({ stepCursorPositionPrev, stepCursorPositionPrev, {} });
      queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
      drawFlags |= DRAW_STEP_CURSOR | DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
      headerFlags |= (1 << STEP_RECORD);
    }
    break;

  case STEP_SIZE:
    if (hasConstrainedChanged(stepRecordSteps, e.dir, uint8_t(1), uint8_t(32))) {
      stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);
      stepCursorPositionIndex = 0;
      stepCursorPosition = ROLL_WIDTH;
      drawFlags |= DRAW_STEP_CURSOR;
      headerFlags |= (1 << STEP_SIZE);
    }
    break;

  case STEP_LAYER:
    if (hasConstrainedChanged(stepRecordLayer, e.dir,
      int8_t(0), int8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1))) {
      headerFlags |= (1 << STEP_LAYER);
    }
    break;
  }

  return headerFlags;
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleViewModeNormal(EncoderEvents e, uint8_t& oldMode, uint8_t& newMode) {
  oldMode = viewMode;
  if (data.activeTrack == SONG_MODE_MUTE_TRACK)
    newMode = constrain(viewMode + e.dir, 0, VIEW_NUM - 3); // no y scrolling
  else
    newMode = constrain(viewMode + e.dir, 0, VIEW_NUM - 1);
  viewMode = newMode;
  buttonView->drawNow();
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleEditModeNormal(EncoderEvents e, uint8_t& oldMode, uint8_t& newMode) {
  oldMode = editMode;
  if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    const uint8_t muteEditModes[] = { EDIT_SELECT_NOTE, EDIT_NOTEON, EDIT_NOTE };
    const uint8_t numMuteModes = 3;

    uint8_t currentPos = 0;
    for (uint8_t i = 0; i < numMuteModes; i++) {
      if (muteEditModes[i] == editMode) {
        currentPos = i;
        break;
      }
    }

    if (currentPos == 0 && muteEditModes[0] != editMode) {
      currentPos = 0;
      editMode = muteEditModes[0];
    }

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
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleStepModeNormal(EncoderEvents e, uint8_t& oldMode, uint8_t& newMode) {
  uint8_t oldStepMode = stepMode;

  if (data.isSongMode && !data.useGridSongMode) {
    // Song mode: cycle through all step modes including target track
    stepMode = constrain(stepMode + e.dir, 0, STEP_NUM - 1);
  }
  else {
    // Pattern mode: skip STEP_TARGET_TRACK (start from STEP_RECORD)
    int8_t newStepMode = stepMode + e.dir;
    if (newStepMode < STEP_RECORD) {
      newStepMode = STEP_NUM - 1;
    }
    else if (newStepMode >= STEP_NUM) {
      newStepMode = STEP_RECORD;
    }
    stepMode = newStepMode;
  }

  if ((data.isRunning == false) && (stepMode == STEP_RECORD)) {
    drawFlags |= DRAW_STEP_CURSOR;
  }
  queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
  buttonStep->drawNow();

  if (oldStepMode != stepMode) {
    drawHeader(HEADER_DRAW_ALL);
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
    else {
      // Pattern and song notes header
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
        }
        else {
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
        }
        else {
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
        }
        else {
          sprintf(s, "\\ --.----");
        }
        drawHeaderSetting(GRID.X[4], "", s, false, (editMode == EDIT_NOTEOFF));
      }

      if (modeFlags & (1 << EDIT_VELOCITY)) {
        if (selectedNote > -1) {
          LiveSequencer::NotePair& p = notePairs.at(selectedNote);
          sprintf(s, "%i", p.noteOn.note_in_velocity);
        }
        else {
          sprintf(s, "---");
        }
        drawHeaderSetting(GRID.X[5], "VEL", s, false, (editMode == EDIT_VELOCITY));
      }

      if (modeFlags & (1 << EDIT_NOTE)) {
        if (selectedNote > -1) {
          LiveSequencer::NotePair& p = notePairs.at(selectedNote);
          std::string noteString = getNoteString(p.noteOn.note_in);
          sprintf(s, "%s", noteString.c_str());
        }
        else {
          sprintf(s, "---");
        }
        drawHeaderSetting(GRID.X[5] + 4 * CHAR_width_small, "NOTE", s, false, (editMode == EDIT_NOTE));
      }
    }

    break;

  case MODE_STEP:
    // Show target track field ONLY in song mode
    if (data.isSongMode && !data.useGridSongMode) {
      if (modeFlags & (1 << STEP_TARGET_TRACK)) {
        sprintf(s, "TRACK %02i", data.stepRecordTargetTrack + 1); // Display as 1-12
        drawHeaderSetting(GRID.X[2], "PLAY BY", s, false, (stepMode == STEP_TARGET_TRACK));
      }
    }

    // Always show all step modes, highlight the active one (like view/edit modes)
    if (modeFlags & (1 << STEP_RECORD)) {
      const uint8_t pattern = (stepCursorPositionIndex / stepRecordSteps);
      sprintf(s, "P%i %02i/%02i", pattern, stepCursorPositionIndex % stepRecordSteps, stepRecordSteps);
      drawHeaderSetting(GRID.X[3], "REC/ADV", s, false, (stepMode == STEP_RECORD));
    }
    if (modeFlags & (1 << STEP_SIZE)) {
      sprintf(s, "1/%02i", stepRecordSteps);
      drawHeaderSetting(GRID.X[4], "STEPSIZE", s, false, (stepMode == STEP_SIZE));
    }
    if (modeFlags & (1 << STEP_LAYER)) {
      sprintf(s, "%i", stepRecordLayer + 1);
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
  display.console = true;

  // Only draw background when no specific notes are specified
  bool drawBackground = false;
  for (auto& u : drawRegionsX) {
    if (u.notes.empty()) {
      drawBackground = true;
      break;
    }
  }
  if (!drawBackground) return;

  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numBars * substeps);

  // Draw vertical grid lines
  for (uint8_t step = 0; step <= view.numBars * substeps; step++) {
    const uint16_t x = ROLL_WIDTH + round(step * stepWidth);
    uint16_t color = (step % substeps == 0) ? GREY2 : GREY3;
    display.drawLine(x, 0, x, CONTENT_HEIGHT, color);
  }

  // Draw horizontal grid lines (only at octave boundaries)
  for (uint8_t note = view.lowestNote; note <= view.highestNote; note++) {
    if (note % 12 == 0) { // Only every C (octave boundaries)
      const uint8_t noteIndex = view.highestNote - note;
      const uint16_t y = round(noteIndex * view.noteHeight);
      display.drawLine(ROLL_WIDTH, y, DISPLAY_WIDTH, y, GREY2);
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

FLASHMEM void UI_LiveSeq_PianoRoll::onEventsChanged(void) {
  if (isVisible) {
    reloadNotes();

    // Re-locate selected note as it may have changed order (index)
    if (selectedNote > -1 && selectedNote < static_cast<int16_t>(notePairs.size())) {
      for (int16_t i = 0; i < int16_t(notePairs.size()); i++) {
        if (memcmp(&notePairs.at(i).noteOn, &currentNote, sizeof(currentNote)) == 0) {
          selectedNote = i;
          break;
        }
      }
    }
  }
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

    // Redraw the combined instrument/quantization button when mode changes
    if (buttonInstQuant) {
      buttonInstQuant->drawNow();
    }

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
        liveSeq.requestSortEvents();
        onEventsChanged();
      }

      if (data.activeTrack == SONG_MODE_MUTE_TRACK && selectedMuteEvent > -1) {
        liveSeq.requestSortEvents();
        reloadNotes();
      }
      break;

    case MODE_STEP:
      stepMode = STEP_RECORD;
      buttonStep->drawNow();
      queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
      stepCursorPositionIndex = 0;
      stepCursorPosition = ROLL_WIDTH;
      stepCursorPositionPrev = ROLL_WIDTH;
      drawFlags |= DRAW_CONTENT_AREA;
      break;
    }

    // entering new mode actions
    switch (newMode) {
    case MODE_VIEW:
      break;

    case MODE_EDIT:
      if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
        display.console = true;
        display.fillRect(GRID.X[2], 183, 23, 20, COLOR_BACKGROUND);
      }

      if ((selectedNote == -1) && notePairs.size()) {
        int16_t leftmostNote = -1;
        uint16_t leftmostX = DISPLAY_WIDTH;

        for (size_t i = 0; i < notePairs.size(); i++) {
          auto& note = notePairs[i];
          if (note.noteOn.layer != selectedLayer) {
            continue;
          }

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

        if (leftmostNote != -1) {
          selectedNote = leftmostNote;
        }
        else {
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
        }
        else {
          ++it;
        }
      }
    }

    muteAutomationEvents.erase(muteAutomationEvents.begin() + selectedMuteEvent);

    if (muteAutomationEvents.empty()) {
      selectedMuteEvent = -1;
    }
    else {
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
  //LiveSequencer::MidiEvent noteOnInfo = notePairs.at(selectedNote).noteOn;
  //LiveSequencer::MidiEvent noteOffInfo = notePairs.at(selectedNote).noteOff;

  if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
    // Song mode - mark as invalid (soft delete)
    // The references in notePairs point to actual events in data.songEvents
    notePairs.at(selectedNote).noteOn.event = midi::InvalidType;
    notePairs.at(selectedNote).noteOff.event = midi::InvalidType;
  }
  else {
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
  }
  else {
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
    [this](auto* b) {
      const TouchButton::Color playColor = data.isRunning ? TouchButton::BUTTON_RED : TouchButton::BUTTON_ACTIVE;
      b->draw((data.isRunning ? "STOP" : "START"), "", playColor);
    }, [this](auto* b) {
      const bool isRunning = data.isRunning;
      if (isRunning) {
        liveSeq.stop();
      }
      else {
        liveSeq.start();
      }
      if (mode == MODE_STEP && stepMode == STEP_RECORD) {
        drawFlags |= isRunning ? DRAW_STEP_CURSOR : DRAW_CONTENT_AREA;
        ui_pianoroll->queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
      }
      });
  buttons.push_back(buttonPlay);

  // Track button
  buttons.push_back(new TouchButton(GRID.X[1], y,
    [this](auto* b) {
      char temp_char[4];
      if (data.activeTrack >= SONG_MODE_NOTES_TRACK) {
        if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
          b->draw("SONG", "NOTES", TouchButton::BUTTON_ACTIVE);
        }
        else {
          b->draw("SONG", "MUTES", TouchButton::BUTTON_ACTIVE);
        }
      }
      else {
        itoa(data.activeTrack + 1, temp_char, 10);
        b->draw("TRACK", temp_char, TouchButton::BUTTON_ACTIVE);
      }
    }, [this](auto* b) {
      uint8_t newTrack = data.activeTrack + 1;
      if (newTrack >= MAX_TRACKS) {
        newTrack = 0;
      }

      display.console = true;
      display.fillRect(ROLL_WIDTH, 0, CONTENT_WIDTH, CONTENT_HEIGHT, COLOR_BACKGROUND);

      viewMode = VIEW_ZOOM_X;
      editMode = EDIT_SELECT_NOTE;

      data.activeTrack = newTrack;


      // Redraw the instrument/quant button if we're in view mode
      if (mode == MODE_VIEW && buttonInstQuant) {
        buttonInstQuant->drawNow();
      }


      if (newTrack >= SONG_MODE_NOTES_TRACK) {
        data.isSongMode = true;
        data.useGridSongMode = false;
        uint16_t maxBars = getMaxBarsForCurrentMode();
        view.startBar = 0;
        view.numBars = maxBars;
        view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
      }
      else {
        data.isSongMode = false;
        view.startBar = 0;
        view.numBars = data.numberOfBars;
        view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
      }

      view.startOctave = 1;
      view.numOctaves = 5;
      view.numNotes = view.numOctaves * 12;
      view.noteHeight = CONTENT_HEIGHT / float(view.numNotes);
      view.lowestNote = 24 + view.startOctave * 12;
      view.highestNote = (view.lowestNote + view.numNotes);

      if (mode == MODE_EDIT) {
        isSettingActivated = false;
        editMode = EDIT_LAYER;
        selectedMuteEvent = -1;
      }

      notePairs.clear();
      muteAutomationEvents.clear();
      selectedNote = -1;
      selectedMuteEvent = -1;

      ui_pianoroll->reloadNotes(true);

      if (data.activeTrack == SONG_MODE_MUTE_TRACK && muteAutomationEvents.size() > 0) {
        selectedMuteEvent = 0;
      }
      else if (notePairs.size() > 0) {
        selectedNote = 0;
      }

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
      ui_pianoroll->drawAreaIndicator();
      liveSeq.setActiveTrack(data.activeTrack);
      b->drawNow();
      }));

  // Combined Instrument/Quantization button at GRID.X[2]
  buttonInstQuant = new TouchButton(GRID.X[2], y,
    [this](auto* b) {
      // This draw handler will only be called when we explicitly call drawNow()
      if (mode == MODE_VIEW) {
        // Draw instrument button in VIEW mode
        if (data.activeTrack == SONG_MODE_MUTE_TRACK || data.activeTrack == SONG_MODE_NOTES_TRACK) {
          b->draw("", "", TouchButton::BUTTON_INACTIVE);
        }
        else {
          const uint8_t device = data.trackSettings[data.activeTrack].device;
          const uint8_t instrument = data.trackSettings[data.activeTrack].instrument;
          char name[10];
          char sub[10];
          liveSeq.getInstrumentName(device, instrument, name, sub);
          b->draw(name, sub, TouchButton::BUTTON_ACTIVE);
        }
      }
      else {
        // Draw quantization button in EDIT/STEP modes
        const char* quantText = "";
        const char* subText = "QUANT";

        switch (quantizeLevel) {
        case QUANTIZE_OFF: quantText = "OFF"; break;
        case QUANTIZE_64: quantText = "1/64"; break;
        case QUANTIZE_32: quantText = "1/32"; break;
        case QUANTIZE_16: quantText = "1/16"; break;
        case QUANTIZE_8: quantText = "1/8"; break;
        case QUANTIZE_4: quantText = "1/4"; break;
        }

        b->draw(quantText, subText, TouchButton::BUTTON_ACTIVE);
      }
    },
    [this](auto* b) {
      if (mode == MODE_VIEW) {
        // Instrument button click - only in VIEW mode
        if (data.activeTrack != SONG_MODE_MUTE_TRACK && data.activeTrack != SONG_MODE_NOTES_TRACK) {
          if (data.tracks[data.activeTrack].screenSetupFn != nullptr) {
            SetupFn f = (SetupFn)data.tracks[data.activeTrack].screenSetupFn;
            f(0);
          }
          UI_LiveSequencer::openScreen(data.tracks[data.activeTrack].screen);
        }
      }
      else {
        // Quantization button click - only in EDIT/STEP modes
        quantizeLevel = (quantizeLevel + 1) % QUANTIZE_NUM;
        b->drawNow(); // Explicitly redraw after change

        if (mode == MODE_EDIT && (selectedNote > -1 || selectedMuteEvent > -1)) {
          drawHeader(HEADER_DRAW_ALL);
        }
      }
    });
  buttons.push_back(buttonInstQuant);

  buttonView = new TouchButton(GRID.X[3], y,
    [this](auto* b) {
      b->draw("VIEW", "MODE", (mode == MODE_VIEW) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
    }, [this](auto* b) {
      ui_pianoroll->setMode(MODE_VIEW);
      b->drawNow();
      });
  buttons.push_back(buttonView);

  buttonEdit = new TouchButton(GRID.X[4], y,
    [this](auto* b) {
      std::string txt = "EDIT";
      std::string sub = "MODE";
      TouchButton::Color color = (mode == MODE_EDIT) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;

      if (mode == MODE_EDIT && isSettingActivated) {
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          txt = "MUTE";
          sub = "DELETE";
          color = TouchButton::BUTTON_RED;
        }
        else if (editMode == EDIT_SELECT_NOTE) {
          txt = "NOTE";
          sub = "DELETE";
          color = TouchButton::BUTTON_RED;
        }
      }

      b->draw(txt.c_str(), sub.c_str(), color);
    },
    [this](auto* b) {
      ui_pianoroll->setMode(MODE_EDIT);

      if (isSettingActivated) {
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
          ui_pianoroll->deleteSelectedNote();
        }
        else if (editMode == EDIT_SELECT_NOTE) {
          ui_pianoroll->deleteSelectedNote();
        }
      }
      b->drawNow();
    });

  buttons.push_back(buttonEdit);

  buttonStep = new TouchButton(GRID.X[5], y,
    [this](auto* b) {
      TouchButton::Color color = (mode == MODE_STEP) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;
      if (data.isRunning) {
        color = TouchButton::BUTTON_NORMAL;
      }
      b->draw("STEP", "RECORD", color);
    },
    [this](auto* b) {
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

FLASHMEM void UI_LiveSeq_PianoRoll::quantizeSelectedNote(void) {
  if (quantizeLevel == QUANTIZE_OFF) return;

  // Calculate grid size based on quantization level
  uint32_t gridSize = 0;
  switch (quantizeLevel) {
  case QUANTIZE_64: gridSize = data.patternLengthMs / 64; break;
  case QUANTIZE_32: gridSize = data.patternLengthMs / 32; break;
  case QUANTIZE_16: gridSize = data.patternLengthMs / 16; break;
  case QUANTIZE_8: gridSize = data.patternLengthMs / 8; break;
  default: return;
  }

  // Handle note quantization
  if (selectedNote > -1 && selectedNote < static_cast<int16_t>(notePairs.size())) {
    LiveSequencer::NotePair& note = notePairs.at(selectedNote);

    // Store original positions for redraw
    uint32_t oldTimeOn = getEventTime(note.noteOn);
    uint32_t oldTimeOff = getEventTime(note.noteOff);

    // Quantize note on time
    uint32_t quantizedTimeOn = (oldTimeOn / gridSize) * gridSize;

    // Calculate note length and quantize note off time
    uint32_t noteLength = oldTimeOff - oldTimeOn;
    uint32_t quantizedTimeOff = quantizedTimeOn + noteLength;

    // Snap note off to grid if it's close
    uint32_t remainderOff = quantizedTimeOff % gridSize;
    if (remainderOff > gridSize / 2) {
      quantizedTimeOff += gridSize - remainderOff;
    }
    else {
      quantizedTimeOff -= remainderOff;
    }

    // Ensure minimum note length
    if (quantizedTimeOff <= quantizedTimeOn) {
      quantizedTimeOff = quantizedTimeOn + gridSize;
    }

    // Update the note times
    setEventTime(note.noteOn, quantizedTimeOn);
    setEventTime(note.noteOff, quantizedTimeOff);



    // Queue redraw
    queueDrawNote(note);
    drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;

    // Update header
    drawHeader((1 << EDIT_NOTEON) | (1 << EDIT_NOTEOFF));
  }

  // Handle mute automation event quantization
  if (selectedMuteEvent > -1 && selectedMuteEvent < static_cast<int16_t>(muteAutomationEvents.size())) {
    LiveSequencer::MidiEvent& event = muteAutomationEvents.at(selectedMuteEvent);

    // Find canonical event in data.songEvents
    LiveSequencer::MidiEvent* realEvent = nullptr;
    uint16_t songPatternIndex = 0;
    for (auto& patternEntry : data.songEvents) {
      for (auto& ev : patternEntry.second) {
        if (ev.track == event.track &&
          ev.layer == event.layer &&
          ev.note_in == event.note_in &&
          ev.event == event.event &&
          ev.patternMs == event.patternMs &&
          ev.note_in_velocity == event.note_in_velocity) {
          realEvent = &ev;
          songPatternIndex = patternEntry.first;
          break;
        }
      }
      if (realEvent) break;
    }

    if (!realEvent) {
      songPatternIndex = getSongPatternForEvent(event);
    }

    // Get current time and quantize
    uint32_t currentTime = getPatternRelativeTime(event);
    uint32_t quantizedTime = (currentTime / gridSize) * gridSize;

    if (quantizedTime != currentTime) {
      // Queue redraw for old position
      uint32_t oldAbsoluteTime = (songPatternIndex * data.numberOfBars * data.patternLengthMs) +
        (event.patternNumber * data.patternLengthMs) + event.patternMs;
      UpdateRange oldRegion;
      oldRegion.fromX = getNoteCoord(oldAbsoluteTime) - 5;
      oldRegion.toX = getNoteCoord(oldAbsoluteTime) + 5;
      drawRegionsX.push_back(oldRegion);

      // Update time
      setPatternRelativeTime(event, quantizedTime, songPatternIndex);

      // Update canonical event if found
      if (realEvent) {
        realEvent->patternNumber = event.patternNumber;
        realEvent->patternMs = event.patternMs;
      }
      else {
        updateSongEvent(event);
      }

      // Queue redraw for new position
      uint32_t newAbsoluteTime = (songPatternIndex * data.numberOfBars * data.patternLengthMs) +
        (event.patternNumber * data.patternLengthMs) + event.patternMs;
      UpdateRange newRegion;
      newRegion.fromX = getNoteCoord(newAbsoluteTime) - 5;
      newRegion.toX = getNoteCoord(newAbsoluteTime) + 5;
      drawRegionsX.push_back(newRegion);

      drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_MUTE_AUTOMATION;
    }
  }
}
