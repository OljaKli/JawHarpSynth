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



FLASHMEM UI_LiveSeq_PianoRoll::UI_LiveSeq_PianoRoll(LiveSequencer &sequencer, LiveSequencer::LiveSeqData &d) :
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

  recordSteps = new EditableValueVector<uint8_t>(stepRecordSteps, { 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64 }, 16, [](auto *v){});
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getMaxBarsForCurrentMode() const {
  if (data.isSongMode && !data.useGridSongMode) {
    uint16_t songBars = (uint16_t)data.lastSongEventPattern + 1;
    uint16_t patternBars = (uint16_t)data.numberOfBars;
    return (songBars > patternBars) ? songBars : patternBars;
  } else {
    return data.numberOfBars; // Pattern mode
  }
}

FLASHMEM void UI_func_liveseq_graphic(uint8_t param) {  // for Livesequencer
  ui_pianoroll->processLCDM(param);
}

FLASHMEM void handle_touchscreen_pianoroll(void) {
  ui_pianoroll->handleTouchScreen();
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleTouchScreen() {
  for (TouchButton *b : buttons) {
    b->processPressed();
  }
}


FLASHMEM void UI_LiveSeq_PianoRoll::drawLayersIndicator(void) {
  display.setTextSize(1);
  static constexpr uint16_t layerViewSizeX = ROLL_WIDTH / 2;
  static constexpr uint16_t layerViewY = CONTENT_HEIGHT + LINE_HEIGHT;
  
  // Only show layers for pattern mode
  if (data.activeTrack < SONG_MODE_NOTES_TRACK) {
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
        display.setTextColor(COLOR_BACKGROUND, COLOR_BACKGROUND); // no text
      }
      display.setCursor(x + 2, y + 2);
      display.print(l + 1);
    }
  } else {
    // Clear the layer indicator area for song mode
    display.fillRect(GRID.X[0], layerViewY, ROLL_WIDTH, HEADER_HEIGHT, COLOR_BACKGROUND);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(UpdateRange coords) {
  drawRegionsX.push_back(coords);
  drawFlags |= DRAW_NOTES;
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
  const uint32_t totalTimeMs = uint32_t(maxBars) * data.patternLengthMs;
  const uint32_t viewTimeMs  = uint32_t(view.numBars) * data.patternLengthMs;
  const uint32_t startTimeMs = uint32_t(view.startBar) * data.patternLengthMs;

  if (totalTimeMs == 0) return;

  const uint16_t x = ROLL_WIDTH + 1 + roundf((float)wTotal * (float)startTimeMs / (float)totalTimeMs);
  const uint16_t h = roundf(hTotal * view.numOctaves / float(TOTAL_OCTAVES));
  const uint16_t y = layerViewY + 1 + hTotal - h -
                     roundf(hTotal * view.startOctave / float(TOTAL_OCTAVES));

  int w = int(roundf((float)wTotal * (float)viewTimeMs / (float)totalTimeMs));
  if (w < 1) w = 1;
  if (x + w > ROLL_WIDTH + wTotal) w = (ROLL_WIDTH + wTotal) - x;

  display.fillRect(x, y, uint16_t(w), h, MIDDLEGREEN);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setTextSize(1);
  display.setCursor(GRID.X[1], layerViewY + 3);

  if (data.isSongMode && !data.useGridSongMode) {
    display.printf("BAR %i-%i/%i", view.startBar + 1, view.startBar + view.numBars, maxBars);
  } else {
    display.printf("BAR %i-%i", view.startBar + 1, view.startBar + view.numBars);
  }

  display.setCursor(GRID.X[1], layerViewY + CHAR_height_small + 5);
  display.printf("OCT %i-%i", view.startOctave, view.startOctave + view.numOctaves - 1);
}


FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(LiveSequencer::NotePair note) {
  UpdateRange coords;
  const uint32_t timeOn  = getEventTime(note.noteOn);
  const uint32_t timeOff = getEventTime(note.noteOff);
  coords.fromX = getNoteCoord(timeOn);
  coords.toX   = getNoteCoord(timeOff);
  coords.notes = { note.noteOn.note_in };

  coords.fromX = (coords.fromX < 2 + ROLL_WIDTH) ? ROLL_WIDTH : (coords.fromX - 2);
  coords.toX   = (coords.toX > DISPLAY_WIDTH - 2) ? DISPLAY_WIDTH : (coords.toX + 2);

  drawRegionsX.push_back(coords);
  drawFlags |= DRAW_NOTES;
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
    } else {
        // Start at first pattern
        data.activeTrack = 0;
    }
    
    liveSeq.setActiveTrack(data.activeTrack);

    isVisible = true;
    registerTouchHandler(handle_touchscreen_pianoroll);

    // Adjust view for song mode - USE OLD APPROACH
    if (data.isSongMode && !data.useGridSongMode) {
      // In song mode, show more bars to accommodate song length
      view.startBar = 0;
      view.numBars = std::max(static_cast<uint8_t>(data.lastSongEventPattern + 1), data.numberOfBars);
    } else {
      view.startBar = 0;
      view.numBars = data.numberOfBars;
    }

    view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
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

    for (auto *b : buttons) {
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
      const uint16_t patternTime = isRunningHere ? ((data.currentPattern - view.startBar) * data.patternLengthMs + data.patternTimer) : 0;
      view.cursorPos = ROLL_WIDTH + round(patternTime * view.msToPix);
      const bool offLeft = (data.currentPattern < view.startBar);
      const bool offRight = (data.currentPattern >= (view.startBar + view.numBars));
      view.cursorPos = offLeft ? ROLL_WIDTH : view.cursorPos;
      view.cursorPos = offRight ? DISPLAY_WIDTH - 1 : view.cursorPos;
      const uint16_t color = (offLeft || offRight) ? RED : MIDDLEGREEN;
      const bool colorChanged = color != view.cursorColor;
      const bool cursorPosChanged = (view.cursorPos != view.cursorPosPrev);

      if (justStopped || colorChanged || cursorPosChanged) {
        queueDrawNote({ view.cursorPosPrev, view.cursorPosPrev, {} });
        drawFlags |= DRAW_CONTENT_AREA;

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
  DBG_LOG(printf("draw step cursor...\n"));
  display.console = true;
  display.drawLine(stepCursorPosition, 0, stepCursorPosition, CONTENT_HEIGHT - 1, RED);
}


FLASHMEM std::vector<LiveSequencer::NotePair> UI_LiveSeq_PianoRoll::getNotePairsFromSongTrack(uint8_t track, uint8_t& lowestNote, uint8_t& highestNote, uint8_t patternFrom, uint8_t patternTo) {
  std::vector<LiveSequencer::NotePair> result;
  uint8_t lowestFound = 0xFF;
  uint8_t highestFound = 0x00;

  DBG_LOG(printf("getNotePairsFromSongTrack: Track %s, Patterns %d-%d\n", 
                 track == 0xFF ? "ALL" : std::to_string(track).c_str(), patternFrom, patternTo));
  
  // Calculate time range for visible area
  const uint32_t timeFrom = patternFrom * data.patternLengthMs;
  const uint32_t timeTo = (patternTo + 1) * data.patternLengthMs;
  
  // Iterate through ALL song events, not just visible patterns
  for (const auto& patternEntry : data.songEvents) {
    const auto& events = patternEntry.second;
    
    // Pair NoteOn events with their corresponding NoteOff
    for (auto itOn = events.begin(); itOn != events.end(); itOn++) {
      // Filter by track if not showing all (0xFF = show all)
      if ((track == 0xFF || itOn->track == track) && itOn->event == midi::NoteOn) {
        // Calculate note time
        uint32_t noteTime = itOn->patternNumber * data.patternLengthMs + itOn->patternMs;
        
        // Find matching NoteOff
        for (auto itOff = itOn; itOff != events.end(); itOff++) {
          if (itOff->track == itOn->track && 
              itOff->layer == itOn->layer &&
              itOff->note_in == itOn->note_in &&
              itOff->event == midi::NoteOff) {
            
            uint32_t noteOffTime = itOff->patternNumber * data.patternLengthMs + itOff->patternMs;
            
            // Only include if note overlaps with visible time range
            if (noteTime < timeTo && noteOffTime > timeFrom) {
              LiveSequencer::NotePair p = {
                .noteOn = const_cast<LiveSequencer::MidiEvent&>(*itOn),
                .noteOff = const_cast<LiveSequencer::MidiEvent&>(*itOff),
                .isMuted = false
              };
              highestFound = std::max(highestFound, itOn->note_in);
              lowestFound = std::min(lowestFound, itOn->note_in);
              result.emplace_back(p);
            }
            break;
          }
        }
      }
    }
  }
  
  DBG_LOG(printf("Found %zu note pairs\n", result.size()));
  
  lowestNote = lowestFound;
  highestNote = highestFound;
  return result;
}

FLASHMEM std::vector<LiveSequencer::MidiEvent> UI_LiveSeq_PianoRoll::getMuteAutomationEvents(uint8_t track, uint8_t patternFrom, uint8_t patternTo) {
  std::vector<LiveSequencer::MidiEvent> result;
  
  // Calculate time range for visible area
  const uint32_t timeFrom = patternFrom * data.patternLengthMs;
  const uint32_t timeTo = (patternTo + 1) * data.patternLengthMs;
  
  // Iterate through ALL patterns
  for (const auto& patternEntry : data.songEvents) {
    for (const auto& e : patternEntry.second) {
      // Filter by track if not showing all (0xFF = show all)
      if ((track == 0xFF || e.track == track) &&
          e.event == midi::ControlChange &&
          (e.note_in == LiveSequencer::TYPE_MUTE_ON || e.note_in == LiveSequencer::TYPE_MUTE_OFF)) {
        
        // Calculate event time
        uint32_t eventTime = e.patternNumber * data.patternLengthMs + e.patternMs;
        
        // Only include if event falls within visible time range
        if (eventTime >= timeFrom && eventTime < timeTo) {
          result.emplace_back(e);
        }
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
  } 
  else if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
    // Song mode: show only automation events
    notePairs.clear();
    muteAutomationEvents = getMuteAutomationEvents(0xFF, patternFrom, patternTo);
    selectedNote = -1;
    selectedMuteEvent = (muteAutomationEvents.size() > 0) ? 0 : -1;
  }
  else {
    // Pattern mode: use the existing method for specific track
    notePairs = liveSeq.getNotePairsFromTrack(data.activeTrack, lowestNote, highestNote, patternFrom, patternTo);
    muteAutomationEvents.clear();
    selectedMuteEvent = -1;
  }
  
  // Only set draw flags if data actually changed
  bool dataChanged = (prevNotePairsCount != notePairs.size()) || 
                     (prevMuteEventsCount != muteAutomationEvents.size());
  
  if (autoAdaptView && notePairs.size()) {
    view.startOctave = std::min((lowestNote - 24) / 12, TOTAL_OCTAVES - view.numOctaves);
    setNumOctaves(5);
    drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
  } else if (dataChanged) {
    // Only redraw if data actually changed
    drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
  }
  
  // Always update header when reloading
  drawHeader(HEADER_DRAW_ALL);
}


FLASHMEM void UI_LiveSeq_PianoRoll::drawMuteAutomation(void) {
 if (!data.isSongMode || data.useGridSongMode) return;
  
  // Only draw if we're in automation view
  if (data.activeTrack != SONG_MODE_MUTE_TRACK) return;  // Fixed!
  
  display.setTextSize(1);
  display.console = true;
  
  // Fix: Use size_t for loop counter to match vector size type
  for (size_t i = 0; i < muteAutomationEvents.size(); i++) {
    const auto& event = muteAutomationEvents.at(i);
    const uint16_t time = getEventTime(event);
    const uint16_t x = getNoteCoord(time);
    const uint16_t y = CONTENT_HEIGHT - 12; // Draw at bottom of view
    
    // Check if this is the selected mute event
    const bool isSelected = (static_cast<int16_t>(i) == selectedMuteEvent);
    
    // Use yellow for selected events, normal colors for others
    uint16_t arrowColor = isSelected ? YELLOW : 
                         (event.note_in == LiveSequencer::TYPE_MUTE_ON ? RED : GREEN);
    
    // Draw the appropriate arrow based on mute state
    if (event.note_in == LiveSequencer::TYPE_MUTE_ON) {
      // MUTE ON: Red down arrow (muting) - character 0x1F
      display.setTextColor(arrowColor, COLOR_BACKGROUND);
      display.setCursor(x-2, y);
      display.write(0x1F); // Down arrow character
    } else if (event.note_in == LiveSequencer::TYPE_MUTE_OFF) {
      // MUTE OFF: Green up arrow (unmuting) - character 0x1E
      display.setTextColor(arrowColor, COLOR_BACKGROUND);
      display.setCursor(x-2, y);
      display.write(0x1E); // Up arrow character
    }
    
    // Draw layer number above the arrow
    display.setTextColor(isSelected ? YELLOW : COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setCursor(x-2, y-8);
    display.printf("%d", event.note_in_velocity + 1); // layer number
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

    if ((data.isRunning == false) && (mode == MODE_STEP) && (stepMode == STEP_RECORD)) { // step record will draw itself
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
FLASHMEM bool UI_LiveSeq_PianoRoll::hasConstrainedChanged(T &value, int16_t diff, T min, T max) {
  const T newValue = constrain(value + diff, min, max);
  const bool changed = newValue != value;
  value = newValue;
  return changed;
}

FLASHMEM void UI_LiveSeq_PianoRoll::setNumBars(int8_t num) {
  if (num == view.numBars) return; // No change
  
  uint16_t maxBars = getMaxBarsForCurrentMode();
  view.numBars = constrain(num, 1, (int8_t)maxBars);

  // Adjust start bar if needed
  if (view.startBar > (int8_t)(maxBars - view.numBars)) {
    setBarStart((int8_t)(maxBars - view.numBars));
  }
  
  view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
  reloadNotes();
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setBarStart(int8_t num) {
  if (num == view.startBar) return; // No change
  
  uint16_t maxBars = getMaxBarsForCurrentMode();
  int8_t maxStartBar = (int8_t)(maxBars - view.numBars);
  if (maxStartBar < 0) maxStartBar = 0;
  view.startBar = constrain(num, 0, maxStartBar);
  
  DBG_LOG(printf("pattern from %i to %i\n", view.startBar, view.startBar + view.numBars - 1));
  reloadNotes();
  drawAreaIndicator();
}

// FLASHMEM void UI_LiveSeq_PianoRoll::toggleSettingActivated(void) {
//   isSettingActivated = !isSettingActivated;
//   if ((mode == MODE_EDIT) && (editMode == EDIT_SELECT_NOTE || editMode == EDIT_MUTE_AUTOMATION)) {
//     buttonEdit->drawNow(); // Redraw Edit button to show/hide DELETE text
//   }
//   drawHeader(HEADER_DRAW_ALL);
// }

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

FLASHMEM void UI_LiveSeq_PianoRoll::updateSongEvent(LiveSequencer::MidiEvent &updatedEvent) {
  // Find and update the event in songEvents
  for (auto &patternEntry : data.songEvents) {
    for (auto &songEvent : patternEntry.second) {
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
        } else {
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
    bool viewChanged = false;

    switch (mode) {
    case MODE_VIEW:
      switch (viewMode) {
      case VIEW_ZOOM_Y:
        if (hasConstrainedChanged(view.numOctaves, -2 * e.dir, int8_t(1), int8_t(5))) {
          setNumOctaves(view.numOctaves);
          viewChanged = true;
        }
        break;

      case VIEW_SCROLL_Y:
        if (hasConstrainedChanged(view.startOctave, -e.dir, int8_t(0), int8_t(TOTAL_OCTAVES - view.numOctaves))) {
          setStartOctave(view.startOctave); // THIS WAS MISSING!
          viewChanged = true;
        }
        break;

      case VIEW_ZOOM_X:
        if (data.isSongMode && !data.useGridSongMode) {
          uint16_t songBars = uint16_t(data.lastSongEventPattern) + 1;
          uint16_t patternBars = uint16_t(data.numberOfBars);
          uint16_t maxBars = (songBars > patternBars) ? songBars : patternBars;

          int16_t newNumBars = view.numBars - (2 * e.dir);
          if (newNumBars < 1) newNumBars = 1;
          if (newNumBars > int16_t(maxBars)) newNumBars = int16_t(maxBars);

          if (newNumBars != view.numBars) {
            setNumBars(newNumBars);
            viewChanged = true;
          }
        } else {
          // pattern mode: finer change
          int16_t newNumBars = view.numBars - e.dir;
          if (newNumBars < 1) newNumBars = 1;
          if (newNumBars > int16_t(data.numberOfBars)) newNumBars = data.numberOfBars;
          if (newNumBars != view.numBars) {
            setNumBars(newNumBars);
            viewChanged = true;
          }
        }
        break;

      case VIEW_SCROLL_X:
        if (data.isSongMode && !data.useGridSongMode) {
          uint16_t songBars = uint16_t(data.lastSongEventPattern) + 1;
          uint16_t patternBars = uint16_t(data.numberOfBars);
          uint16_t maxBars = (songBars > patternBars) ? songBars : patternBars;

          int16_t newStartBar = view.startBar + e.dir;
          int16_t maxStartBar = int16_t(maxBars) - view.numBars;
          if (maxStartBar < 0) maxStartBar = 0;
          if (newStartBar < 0) newStartBar = 0;
          if (newStartBar > maxStartBar) newStartBar = maxStartBar;

          if (newStartBar != view.startBar) {
            setBarStart(newStartBar);
            viewChanged = true;
          }
        } else {
          int16_t newStartBar = view.startBar + e.dir;
          int16_t maxStartBar = int16_t(data.numberOfBars) - view.numBars;
          if (maxStartBar < 0) maxStartBar = 0;
          if (newStartBar < 0) newStartBar = 0;
          if (newStartBar > maxStartBar) newStartBar = maxStartBar;
          if (newStartBar != view.startBar) {
            setBarStart(newStartBar);
            viewChanged = true;
          }
        }
        break;
      } // end switch(viewMode)

      if (viewChanged) {
        drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
        if (data.isRunning) drawFlags |= DRAW_CURSOR;
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
                    headerFlags |= (1 << EDIT_MUTE_AUTOMATION);
                    drawFlags |= DRAW_MUTE_AUTOMATION;
                }
            }
        } else {
            // Original note selection logic for pattern mode
            if (selectedNote > -1 && !notePairs.empty()) {
                int16_t newSelection = selectedNote + e.dir;
                if (newSelection < 0) newSelection = notePairs.size() - 1;
                else if (newSelection >= static_cast<int16_t>(notePairs.size())) newSelection = 0;

                if (newSelection != selectedNote) {
                    if (selectedNote > -1) queueDrawNote(notePairs.at(selectedNote));
                    selectedNote = newSelection;
                    if (selectedNote > -1) queueDrawNote(notePairs.at(selectedNote));
                    drawFlags |= DRAW_NOTES;
                    headerFlags = (1 << EDIT_SELECT_NOTE);
                }
            }
        }
        break;

     case EDIT_LAYER:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
            // For mute automation: change layer
            if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
                LiveSequencer::MidiEvent &event = muteAutomationEvents.at(selectedMuteEvent);
                int8_t layer = event.note_in_velocity;
                if (hasConstrainedChanged(layer, e.dir, int8_t(0), int8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1))) {
                    event.note_in_velocity = layer;
                    updateSongEvent(event);
                    headerFlags |= (1 << EDIT_MUTE_AUTOMATION);
                    drawFlags |= DRAW_MUTE_AUTOMATION;
                }
            }
        } else {
            // Original layer editing for pattern mode
            if (selectedNote > -1) {
                int8_t layer = notePairs.at(selectedNote).noteOn.layer;
                const int8_t maxLayer = (data.isSongMode && !data.useGridSongMode) ?
                                       int8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1) :
                                       int8_t(data.trackSettings[data.activeTrack].layerCount - 1);
                if (hasConstrainedChanged(layer, e.dir, int8_t(0), maxLayer)) {
                    notePairs.at(selectedNote).noteOn.layer = layer;
                    notePairs.at(selectedNote).noteOff.layer = layer;
                    if (data.isSongMode && !data.useGridSongMode) {
                        updateSongEvent(notePairs.at(selectedNote).noteOn);
                        updateSongEvent(notePairs.at(selectedNote).noteOff);
                        reloadNotes();
                    }
                    headerFlags |= (1 << EDIT_LAYER);
                    drawFlags |= DRAW_CONTENT_FULL;
                }
            }
        }
        break;

      case EDIT_NOTEON:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
            // For mute automation: change timing
            if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
                LiveSequencer::MidiEvent &event = muteAutomationEvents.at(selectedMuteEvent);
                uint32_t time = getEventTime(event);
                uint32_t newTime = time + (e.dir * e.speed * 10); // Small timing adjustments
                if (newTime != time) {
                    setEventTime(event, newTime);
                    updateSongEvent(event);
                    headerFlags |= (1 << EDIT_MUTE_AUTOMATION);
                    drawFlags |= DRAW_MUTE_AUTOMATION;
                }
            }
        } else {
            // Original timing editing for pattern mode
            if (selectedNote > -1) {
                LiveSequencer::MidiEvent &on  = notePairs.at(selectedNote).noteOn;
                LiveSequencer::MidiEvent &off = notePairs.at(selectedNote).noteOff;

                const uint32_t noteOnTime  = getEventTime(on);
                const uint32_t noteOffTime = getEventTime(off);

                uint32_t time = noteOnTime;
                const uint32_t minTime = 0;
                const uint32_t maxTime = noteOffTime;

                if (hasConstrainedChanged(time, e.dir * e.speed, minTime, maxTime)) {
                    setEventTime(on, time);
                    memcpy(&currentNote, &on, sizeof(on));
                    if (data.isSongMode && !data.useGridSongMode) {
                        updateSongEvent(on);
                        reloadNotes();
                    } else {
                        liveSeq.requestSortEvents();
                    }
                    headerFlags |= (1 << EDIT_NOTEON);
                    drawFlags |= DRAW_CONTENT_FULL;
                }
            }
        }
        break;

      case EDIT_NOTEOFF:
        if (selectedNote > -1) {
          LiveSequencer::MidiEvent &on  = notePairs.at(selectedNote).noteOn;
          LiveSequencer::MidiEvent &off = notePairs.at(selectedNote).noteOff;

          const bool isNoteOn = editMode == EDIT_NOTEON;
          const uint32_t noteOnTime  = getEventTime(on);
          const uint32_t noteOffTime = getEventTime(off);

          // Use 32-bit time variable (fix truncation bug)
          uint32_t time = isNoteOn ? noteOnTime : noteOffTime;
          const uint32_t minTime = isNoteOn ? 0 : noteOnTime;
          const uint32_t maxTime = isNoteOn ? noteOffTime : uint32_t(data.numberOfBars) * data.patternLengthMs;

          if (hasConstrainedChanged(time, e.dir * e.speed, minTime, maxTime)) {
            if (isNoteOn) {
              setEventTime(on, time);
              memcpy(&currentNote, &on, sizeof(on));
              if (data.isSongMode && !data.useGridSongMode) {
                updateSongEvent(on);
                reloadNotes();
              } else {
                liveSeq.requestSortEvents();
              }
              headerFlags |= (1 << EDIT_NOTEON);
            } else {
              setEventTime(off, time);
              if (data.isSongMode && !data.useGridSongMode) {
                updateSongEvent(off);
                reloadNotes();
              }
              headerFlags |= (1 << EDIT_NOTEOFF);
            }
            drawFlags |= DRAW_CONTENT_FULL;
          }
        }
        break;

      case EDIT_VELOCITY:
        if (selectedNote > -1) {
          LiveSequencer::MidiEvent &event = notePairs.at(selectedNote).noteOn;
          int8_t velo = notePairs.at(selectedNote).noteOn.note_in_velocity;
          if (hasConstrainedChanged(velo, e.dir * e.speed, int8_t(0), int8_t(127))) {
            event.note_in_velocity = velo;
            if (data.isSongMode && !data.useGridSongMode) {
              updateSongEvent(event);
              reloadNotes();
            }
            headerFlags |= (1 << EDIT_VELOCITY);
           // drawFlags |= DRAW_CONTENT_FULL;  not necessary for velocity
          }
        }
        break;

case EDIT_NOTE:
    if (selectedNote > -1) {
        LiveSequencer::NotePair &thisNote = notePairs.at(selectedNote);
        int8_t note = thisNote.noteOn.note_in;
        if (hasConstrainedChanged(note, e.dir, int8_t(view.lowestNote), int8_t(view.highestNote))) {
            // Store the old note for targeted redraw
            uint8_t oldNote = thisNote.noteOn.note_in;
            
            // Create a temporary note pair for the old position to clear it
            LiveSequencer::NotePair oldNotePair = thisNote;
            oldNotePair.noteOn.note_in = oldNote;
            oldNotePair.noteOff.note_in = oldNote;
            
            thisNote.noteOn.note_in = note;
            thisNote.noteOff.note_in = note;
            if (data.isSongMode && !data.useGridSongMode) {
                updateSongEvent(thisNote.noteOn);
                updateSongEvent(thisNote.noteOff);
                reloadNotes();
            }
            headerFlags |= (1 << EDIT_NOTE);
            
            // Get precise coordinates for old note with proper boundaries
            const uint32_t timeOn = getEventTime(oldNotePair.noteOn);
            const uint32_t timeOff = getEventTime(oldNotePair.noteOff);
            uint16_t fromX = getNoteCoord(timeOn);
            uint16_t toX = getNoteCoord(timeOff);
            
            // Ensure we clear exactly the right area (match note drawing logic)
            fromX = (fromX < 2 + ROLL_WIDTH) ? ROLL_WIDTH : (fromX - 2);
            toX = (toX > DISPLAY_WIDTH - 2) ? DISPLAY_WIDTH : (toX + 2);
            
            // Queue background redraw for old note position
            UpdateRange clearRange;
            clearRange.fromX = fromX;
            clearRange.toX = toX;
            clearRange.notes = { oldNote };
            drawRegionsX.push_back(clearRange);
            
            // Queue drawing of new note position
            queueDrawNote(thisNote);
            drawFlags |= DRAW_NOTES | DRAW_BACKGROUND; // Redraw background and notes
        }
    }
    break;

    case EDIT_MUTE_AUTOMATION:
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
            // For mute automation: toggle mute state
            if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
                LiveSequencer::MidiEvent &event = muteAutomationEvents.at(selectedMuteEvent);
                if (e.dir != 0) {
                    event.note_in = (event.note_in == LiveSequencer::TYPE_MUTE_ON) ? LiveSequencer::TYPE_MUTE_OFF : LiveSequencer::TYPE_MUTE_ON;
                    updateSongEvent(event);
                    headerFlags |= (1 << EDIT_MUTE_AUTOMATION);
                    drawFlags |= DRAW_MUTE_AUTOMATION;
                }
            }
        } else {
            // This case shouldn't happen for pattern mode, but keep original logic
            if (selectedMuteEvent > -1 && !muteAutomationEvents.empty()) {
                LiveSequencer::MidiEvent &event = muteAutomationEvents.at(selectedMuteEvent);
                if (e.dir != 0) {
                    event.note_in = (event.note_in == LiveSequencer::TYPE_MUTE_ON) ? LiveSequencer::TYPE_MUTE_OFF : LiveSequencer::TYPE_MUTE_ON;
                    updateSongEvent(event);
                    headerFlags |= (1 << EDIT_MUTE_AUTOMATION);
                    drawFlags |= DRAW_MUTE_AUTOMATION;
                }
            }
        }
        break;
      } // end switch(editMode)
      break;

    case MODE_STEP:
      // existing step mode handling (unchanged)
      {
        uint8_t oldMode = stepMode;
        uint8_t newMode = constrain(stepMode + e.dir, 0, STEP_NUM - 1);
        stepMode = newMode;
        if ((data.isRunning == false) && (newMode == STEP_RECORD)) {
          drawFlags |= DRAW_STEP_CURSOR;
        }
        queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
        buttonStep->drawNow();
      }
      break;
    } // end switch(mode)

    drawHeader(headerFlags);
  } else {
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
    if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
        // For mute track, cycle through these specific parameters:
        // EDIT_SELECT_NOTE (SEL), EDIT_LAYER (LAYR), EDIT_NOTEON (Time), EDIT_MUTE_AUTOMATION (Mute State)
        newMode = editMode + e.dir;
        
        // Define the valid modes for mute automation in order
        uint8_t validModes[] = {EDIT_SELECT_NOTE, EDIT_LAYER, EDIT_NOTEON, EDIT_MUTE_AUTOMATION};
        uint8_t numValidModes = 4;
        
        // Find current position in valid modes
        uint8_t currentIndex = 0;
        for (uint8_t i = 0; i < numValidModes; i++) {
            if (validModes[i] == editMode) {
                currentIndex = i;
                break;
            }
        }
        
        // Calculate new index with wrap-around
        int8_t newIndex = currentIndex + e.dir;
        if (newIndex < 0) newIndex = numValidModes - 1;
        if (newIndex >= numValidModes) newIndex = 0;
        
        newMode = validModes[newIndex];
        
    } else {
        newMode = constrain(editMode + e.dir, 0, EDIT_NUM - 1);
    }
    editMode = newMode;
    buttonEdit->drawNow();
    
    // Force header redraw to show the new selection
    drawHeader(HEADER_DRAW_ALL);
    break;

    case MODE_STEP:
      oldMode = stepMode;
      newMode = constrain(stepMode + e.dir, 0, STEP_NUM - 1);
      stepMode = newMode;
      break;
    }
    // if (oldMode != newMode && mode != MODE_EDIT) {
    //   drawHeader((1 << oldMode) | (1 << newMode));
    // }
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
  reloadNotes();
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setEventTime(LiveSequencer::MidiEvent &e, uint32_t time) {
  e.patternNumber = time / data.patternLengthMs;
  e.patternMs = time % data.patternLengthMs;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawHeader(uint16_t modeFlags) {
  display.setTextSize(1);
  char s[16] = { "\0" };

  if (modeFlags == HEADER_DRAW_ALL) {
    display.fillRect(GRID.X[2], CONTENT_HEIGHT + LINE_HEIGHT, DISPLAY_WIDTH - GRID.X[3], HEADER_HEIGHT, HEADER_BG_COLOR);
    
    // Display mode indicator
    display.setCursor(GRID.X[2], CONTENT_HEIGHT + 4);
    display.setTextColor(COLOR_SYSTEXT, HEADER_BG_COLOR);
    if (data.isSongMode && !data.useGridSongMode) {
      display.print("SONG");
    } else if (data.useGridSongMode) {
      display.print("GRID");  
    } else {
      display.print("PATTERN");
    }
    display.setCursor(GRID.X[2], CONTENT_HEIGHT + 4+CHAR_height_small);
    display.print("MODE");
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
      drawHeaderSetting(GRID.X[3], "", "ZOOM Y", false, (viewMode == VIEW_ZOOM_Y));
    }
    if (modeFlags & (1 << VIEW_SCROLL_Y)) {
      drawHeaderSetting(GRID.X[4], "", "SCROLL Y", false, (viewMode == VIEW_SCROLL_Y));
    }
    break;

  case MODE_EDIT:
    if (data.activeTrack == SONG_MODE_MUTE_TRACK && selectedMuteEvent > -1 && muteAutomationEvents.size() > 0) {
      // Draw mute automation header - always show when on mute track
      LiveSequencer::MidiEvent &m = muteAutomationEvents.at(selectedMuteEvent);
      
        // Highlight based on current edit mode selection
    if (modeFlags & (1 << EDIT_MUTE_AUTOMATION)) {
        sprintf(s, "%03i", selectedMuteEvent + 1);
        drawHeaderSetting(GRID.X[3], "SEL", s, (editMode == EDIT_SELECT_NOTE), false);
    }
    if (modeFlags & (1 << EDIT_MUTE_AUTOMATION)) {
        sprintf(s, "%i", m.note_in_velocity + 1);
        drawHeaderSetting(GRID.X[3] + 25, "LAYR", s, false, (editMode == EDIT_LAYER));
    }
    if (modeFlags & (1 << EDIT_MUTE_AUTOMATION)) {
        sprintf(s, "/ %i.%04i", m.patternNumber, m.patternMs);
        drawHeaderSetting(GRID.X[4], s, "", (editMode == EDIT_NOTEON), false);
    }
    if (modeFlags & (1 << EDIT_MUTE_AUTOMATION)) {
        sprintf(s, "%s", m.note_in == LiveSequencer::TYPE_MUTE_ON ? "MUTE ON " : "MUTE OFF");
        drawHeaderSetting(GRID.X[4], "", s, false, (editMode == EDIT_MUTE_AUTOMATION));
    }
    } else if (selectedNote > -1) {
      LiveSequencer::NotePair &p = notePairs.at(selectedNote);
      std::string noteString = getNoteString(p.noteOn.note_in);
      if (noteString.length() == 2) {
        noteString.append(" ");
      }

      if (modeFlags & (1 << EDIT_SELECT_NOTE)) {
        sprintf(s, "%03i", selectedNote + 1);
        drawHeaderSetting(GRID.X[3], "SEL", s, false, (editMode == EDIT_SELECT_NOTE));
      }
      if (modeFlags & (1 << EDIT_LAYER)) {
        sprintf(s, "%i", p.noteOn.layer + 1);
        drawHeaderSetting(GRID.X[3] + 25, "LAYR", s, false, (editMode == EDIT_LAYER));
      }
      if (modeFlags & (1 << EDIT_NOTEON)) {
        sprintf(s, "/ %i.%04i", p.noteOn.patternNumber, p.noteOn.patternMs);
        drawHeaderSetting(GRID.X[4], s, "", (editMode == EDIT_NOTEON), false);
      }
      if (modeFlags & (1 << EDIT_NOTEOFF)) {
        sprintf(s, "\\ %i.%04i", p.noteOff.patternNumber, p.noteOff.patternMs);
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

FLASHMEM void UI_LiveSeq_PianoRoll::drawHeaderSetting(uint16_t x, const char *topText, const char *bottomText, bool topHighlighted, bool bottomHighlighted) {
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

// FLASHMEM void UI_LiveSeq_PianoRoll::drawBackground(void) {
//   display.console = true;
  
//   for (auto &u : drawRegionsX) {
//     if (u.notes.empty()) {
//       // Full height clear - for cursor updates
//       // Draw with proper alternating background colors
//       for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
//         const uint8_t noteIndex = view.highestNote - note - 1;
//         const uint16_t y = round(noteIndex * view.noteHeight);
//         const bool isPressed = keysPressed.find(note) != keysPressed.end();
//         const uint16_t bgColor = isPressed ? GREY2 : (note & 0x01 ? GREY4 : COLOR_BACKGROUND);
        
//         const uint16_t width = u.toX - u.fromX;
//         const uint16_t xFrom = std::max(ROLL_WIDTH, u.fromX);
        
//         if (width > 1) {
//           display.fillRect(xFrom, y, width, view.noteHeight, bgColor);
//         } else {
//           display.drawLine(xFrom, y, xFrom, y + view.noteHeight - 1, bgColor);
//         }
//       }
//     } else {
//       // Clear only specific note rows
//       for (uint8_t note : u.notes) {
//         if (note >= view.lowestNote && note < view.highestNote) {
//           const uint8_t noteIndex = view.highestNote - note - 1;
//           const uint16_t y = round(noteIndex * view.noteHeight);
//           const bool isPressed = keysPressed.find(note) != keysPressed.end();
//           const uint16_t bgColor = isPressed ? GREY2 : (note & 0x01 ? GREY4 : COLOR_BACKGROUND);
          
//           const uint16_t width = u.toX - u.fromX;
//           const uint16_t xFrom = std::max(ROLL_WIDTH, u.fromX);
          
//           if (width > 1) {
//             display.fillRect(xFrom, y, width, view.noteHeight, bgColor);
//           } else {
//             display.drawLine(xFrom, y, xFrom, y + view.noteHeight - 1, bgColor);
//           }
//         }
//       }
//     }
//   }
// }

FLASHMEM void UI_LiveSeq_PianoRoll::drawBackground(void) {
  for (auto &u : drawRegionsX) {
    for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
      
      if (u.notes.count(note) || u.notes.empty()) {
        const uint8_t noteIndex = view.highestNote - note - 1;
        const uint16_t y = round(noteIndex * view.noteHeight);
        const bool isPressed = keysPressed.find(note) != keysPressed.end();
        const uint16_t bgColor = isPressed ? GREY2 : (note & 0x01 ? GREY4 : COLOR_BACKGROUND);
        display.console = true;

        //DBG_LOG(printf("draw X %i to %i\n", u.fromX, u.toX));
        const uint16_t width = u.toX - u.fromX;
        const uint16_t xFrom = std::max(ROLL_WIDTH, u.fromX);
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



FLASHMEM void UI_LiveSeq_PianoRoll::drawVerticalBars(void) {

  // Calculate white key height to match the keyboard
  const uint16_t numWhiteKeys = view.numOctaves * 7;
  const float whiteKeyHeight = CONTENT_HEIGHT / float(numWhiteKeys);
  
  // Draw horizontal lines at each white key position
  for (uint8_t whiteKey = 0; whiteKey <= numWhiteKeys; whiteKey++) {
    const uint16_t y = round(whiteKey * whiteKeyHeight);
    const uint16_t lineColor = (whiteKey % 7 == 0) ? GREY3 : GREY4; // Octave boundaries are darker
    
    for (auto &u : drawRegionsX) {
      const bool drawLine = (u.notes.empty());
      if (drawLine) {
        display.console = true;
        display.drawLine(ROLL_WIDTH, y, DISPLAY_WIDTH, y, lineColor);
      }
    }
  }

  // Draw vertical pattern lines
  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numBars * substeps);
  
  // Draw vertical lines
  for (uint8_t step = 0; step < view.numBars * substeps; step++) {
    const uint16_t x = ROLL_WIDTH + round(step * stepWidth);
    const uint16_t lineColor = (step % substeps == 0) ? GREY3 : GREY4;
    
    for (auto &u : drawRegionsX) {
      const bool drawLine = ((x >= u.fromX) && (x <= u.toX));
      if (drawLine && u.notes.empty()) {
        display.console = true;
        display.drawLine(x, 0, x, CONTENT_HEIGHT - 1, lineColor);
      }
    }
  }
}
  
FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getNoteCoord(uint32_t time) const {
  const uint32_t timeAbs = getRelativeNoteTime(time);
  const float px = ROLL_WIDTH + (timeAbs * view.msToPix);
  int32_t coord = int32_t(roundf(px));
  if (coord < int32_t(ROLL_WIDTH)) coord = ROLL_WIDTH;
  if (coord > int32_t(DISPLAY_WIDTH - 1)) coord = DISPLAY_WIDTH - 1;
  return uint16_t(coord);
}

FLASHMEM uint32_t UI_LiveSeq_PianoRoll::getRelativeNoteTime(uint32_t time) const {
  const int32_t startMs = int32_t(view.startBar) * int32_t(data.patternLengthMs);
  const int32_t endMs   = (int32_t(view.startBar) + int32_t(view.numBars)) * int32_t(data.patternLengthMs);

  int32_t timeRel = int32_t(time) - startMs;
  if (timeRel < 0) timeRel = 0;
  if (timeRel > endMs) timeRel = endMs;

  return uint32_t(timeRel);
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNotes(void) {
  if (drawFlags & DRAW_CONTENT_FULL) {
    for (size_t i = 0; i < notePairs.size(); ++i) {
      drawNote(notePairs[i], (selectedNote == int16_t(i)));
    }
  } else if (drawFlags & DRAW_NOTES) {
    for (auto &updateRegion : drawRegionsX) {
      for (size_t i = 0; i < notePairs.size(); ++i) {
        const LiveSequencer::NotePair &note = notePairs[i];
        const uint32_t timeOn  = getEventTime(note.noteOn);
        const uint32_t timeOff = getEventTime(note.noteOff);
        const uint16_t x = getNoteCoord(timeOn);
        const uint32_t lenMs = getRelativeNoteTime(timeOff) - getRelativeNoteTime(timeOn);
        const int w = std::max(1, int(roundf(lenMs * view.msToPix)));

        const bool isInsideX = (int32_t(x) <= int32_t(updateRegion.toX)) &&
                               (int32_t(x + w) >= int32_t(updateRegion.fromX));
        const bool isInsideY = updateRegion.notes.count(note.noteOn.note_in) || updateRegion.notes.empty();

        if (isInsideX && isInsideY) {
          drawNote(note, (selectedNote == int16_t(i)));
        }
      }
    }
  }

  if (notePairs.empty() && (drawFlags & DRAW_CONTENT_FULL)) {
    display.console = true;
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(1);
    display.setCursor(100, CONTENT_HEIGHT / 2 );
    if (data.isSongMode && !data.useGridSongMode && data.activeTrack != SONG_MODE_MUTE_TRACK) {
      display.print("SONG MODE - NO NOTES");
    } else if (data.useGridSongMode) {
      display.print("GRID MODE - NO NOTES");
    } else if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
      display.print("SONG - MUTE AUTOMATION");
    }
    else {
      display.print("PATTERN MODE - NO NOTES");
    }
  }
}



FLASHMEM void UI_LiveSeq_PianoRoll::drawNoteAtPosition(LiveSequencer::NotePair note, bool isSelected, uint16_t x, uint16_t w) const {
  const uint16_t y = CONTENT_HEIGHT - (note.noteOn.note_in - view.lowestNote + 1) * view.noteHeight;
  const uint16_t h = view.noteHeight;
  const uint16_t color = isSelected ? COLOR_SYSTEXT : layerColors.at(note.noteOn.layer);
  
  display.console = true;
  
  if (w > 1) {
    if (note.isMuted) {
      display.drawRect(x, y, w, h, color);
    } else {
      display.fillRect(x, y, w, h, color);
    }
  } else {
    display.drawLine(x, y, x, y + h - 1, color);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note, bool isSelected) const {
  const uint32_t timeOn  = getEventTime(note.noteOn);
  const uint32_t timeOff = getEventTime(note.noteOff);

  const uint16_t x = getNoteCoord(timeOn);
  const uint32_t lenMs = getRelativeNoteTime(timeOff) - getRelativeNoteTime(timeOn);
  const int w = std::max(1, int(roundf(lenMs * view.msToPix)));

  if (!drawRegionsX.empty()) {
    for (auto &u : drawRegionsX) {
      const bool isInsideX = (int32_t(x) <= int32_t(u.toX)) && (int32_t(x + w) >= int32_t(u.fromX));
      const bool isInsideY = u.notes.count(note.noteOn.note_in) || u.notes.empty();
      if (isInsideX && isInsideY) {
        drawNoteAtPosition(note, isSelected, x, uint16_t(w));
        break;
      }
    }
  } else {
    drawNoteAtPosition(note, isSelected, x, uint16_t(w));
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
      editMode = EDIT_SELECT_NOTE;
      buttonEdit->drawNow();
      if (selectedNote > -1) {
        // Only redraw the selected note to remove highlight
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
      drawFlags |= DRAW_CONTENT_AREA; // Only redraw content area, not full screen
      break;
    }

    // entering new mode actions
    switch (newMode) {
    case MODE_VIEW:
      break;

    case MODE_EDIT:
      if ((selectedNote == -1) && notePairs.size()) {
        selectedNote = 0;
        // Only redraw the first note to add highlight
        queueDrawNote(notePairs.at(0));
        drawFlags |= DRAW_NOTES;
      }
      break;

    case MODE_STEP:
      stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);
      drawFlags |= DRAW_STEP_CURSOR;
      break;
    }
    
    // Only redraw header, not full content
    drawHeader(HEADER_DRAW_ALL);
  }
  return modeChanged;
}

FLASHMEM void UI_LiveSeq_PianoRoll::deleteSelectedNote(void) {
  // For mute automation track, delete works for any mute automation parameter
  bool isMuteAutomationMode = (data.activeTrack == SONG_MODE_MUTE_TRACK) && 
                             (editMode == EDIT_SELECT_NOTE || editMode == EDIT_LAYER || 
                              editMode == EDIT_NOTEON || editMode == EDIT_MUTE_AUTOMATION);
  
  if (isMuteAutomationMode && selectedMuteEvent > -1 && muteAutomationEvents.size() > 0) {
    LiveSequencer::MidiEvent eventToRemove = muteAutomationEvents.at(selectedMuteEvent);
    
    // Remove from songEvents
    for (auto &patternEntry : data.songEvents) {
      patternEntry.second.erase(
        std::remove_if(patternEntry.second.begin(), patternEntry.second.end(),
          [&eventToRemove](const LiveSequencer::MidiEvent &e) {
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
    } else {
      selectedMuteEvent = std::min(selectedMuteEvent, (int16_t)(muteAutomationEvents.size() - 1));
    }
    
    // Use the same draw flags that were working before
    drawFlags |= DRAW_CONTENT_FULL | DRAW_MUTE_AUTOMATION;
    
    
  } else if (selectedNote > -1) {
    // Get the events to delete
    LiveSequencer::MidiEvent noteOnToDelete = notePairs.at(selectedNote).noteOn;
    LiveSequencer::MidiEvent noteOffToDelete = notePairs.at(selectedNote).noteOff;
    
    // Delete from songEvents for song mode
    if (data.isSongMode && !data.useGridSongMode) {
      for (auto &patternEntry : data.songEvents) {
        patternEntry.second.erase(
          std::remove_if(patternEntry.second.begin(), patternEntry.second.end(),
            [&noteOnToDelete, &noteOffToDelete](const LiveSequencer::MidiEvent &e) {
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
    } else {
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
  [ this ] (auto *b) { // drawHandler
    const TouchButton::Color playColor = data.isRunning ? TouchButton::BUTTON_RED : TouchButton::BUTTON_ACTIVE;
    b->draw((data.isRunning ? "STOP" : "START"), "", playColor);
  }, [ this ] (auto *b) { // clickedHandler
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
  [ this ] (auto *b) { // drawHandler
    char temp_char[4];
    if (data.activeTrack >= SONG_MODE_NOTES_TRACK) {
        // Song mode tracks
        if (data.activeTrack == SONG_MODE_NOTES_TRACK) {
            b->draw("SONG", "NOTES", TouchButton::BUTTON_ACTIVE);
        } else {
            b->draw("SONG", "MUTE", TouchButton::BUTTON_ACTIVE);
        }
    } else {
        // Pattern tracks
        itoa(data.activeTrack + 1, temp_char, 10);
        b->draw("PATTERN", temp_char, TouchButton::BUTTON_ACTIVE);
    }
  }, [ this ] (auto *b) { // clickedHandler
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
    } else {
        data.isSongMode = false;
    }
    
    // Reset edit mode state
    if (mode == MODE_EDIT) {
        isSettingActivated = false;
        if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
            editMode = EDIT_MUTE_AUTOMATION;
            selectedNote = -1;
        } else {
            editMode = EDIT_SELECT_NOTE;
            selectedMuteEvent = -1;
        }
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
    } else if (notePairs.size() > 0) {
        selectedNote = 0;
    }
    
    // Update UI
    drawFlags |= DRAW_CONTENT_FULL;
    if (data.activeTrack == SONG_MODE_MUTE_TRACK) {
        drawFlags |= DRAW_MUTE_AUTOMATION;
    } else {
        drawFlags |= DRAW_PIANOROLL;
    }
    
    buttonEdit->drawNow();
    ui_pianoroll->drawHeader(HEADER_DRAW_ALL);
    ui_pianoroll->drawLayersIndicator();
    liveSeq.setActiveTrack(data.activeTrack);
    b->drawNow();
  }));

  buttonInst = new TouchButton(GRID.X[2], y, 
  [ this ] (auto *b) { // drawHandler
    const uint8_t device = data.trackSettings[data.activeTrack].device;
    const uint8_t instrument = data.trackSettings[data.activeTrack].instrument;
    char name[10];
    char sub[10];
    liveSeq.getInstrumentName(device, instrument, name, sub);
    b->draw(name, sub, TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    // open instrument settings
    if (data.tracks[data.activeTrack].screenSetupFn != nullptr) {
      SetupFn f = (SetupFn)data.tracks[data.activeTrack].screenSetupFn;
      f(0);
    }
    UI_LiveSequencer::openScreen(data.tracks[data.activeTrack].screen);
  });
  buttons.push_back(buttonInst);

  buttonView = new TouchButton(GRID.X[3], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("VIEW", "MODE", (mode == MODE_VIEW) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
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
  [ this ] (auto *b) { // drawHandler
    TouchButton::Color color = (mode == MODE_STEP) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;
    if (data.isRunning) {
      color = TouchButton::BUTTON_NORMAL;
    }
    else {
      if ((mode == MODE_STEP) && (stepMode == STEP_RECORD)) {
        color = TouchButton::BUTTON_RED;
      }
    }
    b->draw("STEP", "RECORD", color);
  }, [ this ] (auto *b) { // clickedHandler
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
  const bool stepCursorChanged = hasConstrainedChanged(stepCursorPositionIndex, diff, uint16_t(0), uint16_t(view.numBars * stepRecordSteps));
  if (stepCursorChanged) {
    if (stepCursorPositionIndex == view.numBars * stepRecordSteps) {
      stepCursorPositionIndex = 0;
    }
    queueDrawNote({ stepCursorPosition, stepCursorPosition, {} });
    stepCursorPosition = ROLL_WIDTH + round(stepCursorPositionIndex * stepRecordStepSizeMs * view.msToPix);
    drawFlags |= DRAW_STEP_CURSOR | DRAW_CONTENT_AREA;
    drawHeader(1 << STEP_RECORD);
  }
  return stepCursorChanged;
}