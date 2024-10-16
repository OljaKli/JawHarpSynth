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
  static constexpr uint16_t layerViewSizeX = ROLL_WIDTH / 2;
  static constexpr uint16_t layerViewY = CONTENT_HEIGHT + LINE_HEIGHT;
  for (uint8_t l = 0; l < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; l++) {
    const uint16_t x = GRID_X[0] + (l % 2) * (layerViewSizeX);
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
    display.setTextSize(0);
    display.print(l + 1);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawAreaIndicator(void) {
  display.console = true;
  static constexpr uint16_t layerViewY = CONTENT_HEIGHT + LINE_HEIGHT;
  display.drawRect(ROLL_WIDTH, layerViewY, TouchButton::BUTTON_SIZE_X - ROLL_WIDTH, HEADER_HEIGHT, GREY1);

  static constexpr uint16_t wTotal = TouchButton::BUTTON_SIZE_X - ROLL_WIDTH - 2;
  static constexpr uint16_t hTotal = HEADER_HEIGHT - 2;

  // clear content
  display.console = true;
  display.fillRect(ROLL_WIDTH + 1, layerViewY + 1, wTotal, hTotal, COLOR_BACKGROUND);
  
  // draw content
  const uint16_t x = ROLL_WIDTH + 1 + round(wTotal * view.startBar / float(data.numberOfBars));
  const uint16_t h = round(hTotal * view.numOctaves / 8.0f); // we have 8 octaves here
  const uint16_t y = layerViewY + 1 + hTotal - h - round(hTotal * view.startOctave / 8.0f); // we have 8 octaves here
  const uint16_t w = wTotal * (view.numBars) / float(data.numberOfBars);
  display.console = true;
  display.fillRect(x, y, w, h, MIDDLEGREEN);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setCursor(GRID_X[1], layerViewY + 3);
  display.printf("BAR %i-%i", view.startBar + 1, view.startBar + view.numBars);
  display.setCursor(GRID_X[1], layerViewY + CHAR_height_small + 5);
  display.printf("OCT %i-%i", view.startOctave, view.startOctave + view.numOctaves - 1);
}

FLASHMEM void UI_LiveSeq_PianoRoll::processLCDM(uint8_t param) {
  if (LCDML.FUNC_setup()) // ****** SETUP *********
  {
    data.activeTrack = param;

    isVisible = true;
    registerTouchHandler(handle_touchscreen_pianoroll);

    view.startBar = 0;
    view.numBars = data.numberOfBars;

    view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
    reloadNotes(view.startBar, view.numBars, true);
   
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
    if (LCDML.BT_checkDown()) {
      handleEncoderRight(+1);
    }
    if (LCDML.BT_checkUp()) {
      handleEncoderRight(-1);
    }
    if (LCDML.BT_checkEnter()) {
      if ((mode == MODE_EDIT) && (editMode == EDIT_DELETE)) {
        ui_pianoroll->deleteSelectedNote();
      }
      else {
        toggleSettingActivated();
      }
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

FLASHMEM void UI_LiveSeq_PianoRoll::reloadNotes(uint8_t patternFrom, uint8_t numBars, bool autoAdaptView) {
  uint8_t lowestNote = view.lowestNote;
  uint8_t highestNote = view.highestNote;
  if (autoAdaptView) {
    lowestNote = 0x00;
    highestNote = 0xFF;
  }
  notePairs = liveSeq.getNotePairsFromTrack(data.activeTrack, lowestNote, highestNote, patternFrom, patternFrom + numBars - 1);
  if (autoAdaptView && notePairs.size()) {
    uint8_t octLow = (lowestNote - 24) / 12;
    uint8_t octHi = (highestNote - 24) / 12;
    setStartOctave(octLow);
    DBG_LOG(printf("low/high: %i/%i\n", octLow, octHi));
    drawFlags |= DRAW_CONTENT_FULL;
  }
  DBG_LOG(printf("%i/%i: got %i note pairs\n", patternFrom, patternFrom + numBars - 1, notePairs.size()));
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawGUI(void) {
  // NOTE: order of drawing is important
  if (drawFlags & DRAW_CONTENT_FULL) {
    drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    queueDrawNote({ ROLL_WIDTH, DISPLAY_WIDTH, {} }); // force redraw all
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
  if (drawFlags & DRAW_PIANOROLL) {
    drawPianoRoll();
  }
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

    if ((data.isRunning == false) && (mode == MODE_STEP) && (stepMode == STEP_RECORD)) { // step record will draw itself
      if (event == midi::NoteOn) {
        // record note and advance
        const uint16_t stepCursorPositionMs = view.startBar * data.patternLengthMs + round(stepCursorPositionIndex * stepRecordStepSizeMs);
        LiveSequencer::MidiEvent on;
        on.source = LiveSequencer::EventSource::EVENT_PATTERN;
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
        if (stepRecordLayer + 1 > data.trackSettings[data.activeTrack].layerCount) {
          data.trackSettings[data.activeTrack].layerCount++;
          drawLayersIndicator();
        }
        liveSeq.addNotePair(on, off);
        reloadNotes(view.startBar, view.numBars);
        drawFlags |= DRAW_NOTES;
      }
      else if (keysPressed.empty()) {
        // advance on last key released
        step(+1);
      }
    }
    else {
      queueDrawNote({ ROLL_WIDTH, DISPLAY_WIDTH - 1, { key } });
      drawFlags |= DRAW_CONTENT_AREA;
    }    
  }
}

FLASHMEM bool UI_LiveSeq_PianoRoll::hasConstrainedChanged(int8_t &value, int8_t diff, int8_t min, int8_t max) {
  const int16_t newValue = constrain(value + diff, min, max);
  const bool changed = newValue != value;
  value = newValue;
  return changed;
}

FLASHMEM bool UI_LiveSeq_PianoRoll::hasConstrainedChanged(uint16_t &value, int16_t diff, uint16_t min, uint16_t max) {
  const uint16_t newValue = constrain(value + diff, min, max);
  const bool changed = newValue != value;
  value = newValue;
  return changed;
}

FLASHMEM void UI_LiveSeq_PianoRoll::setNumOctaves(int8_t num) {
  view.numOctaves = num;
  view.numNotes = view.numOctaves * 12;
  view.noteHeight = CONTENT_HEIGHT / float(view.numNotes);
  view.lowestNote = 24 + view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
  reloadNotes(view.startBar, view.numBars);
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setStartOctave(int8_t num) {
  view.startOctave = num;
  view.lowestNote = 24 + view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
  reloadNotes(view.startBar, view.numBars);
  drawAreaIndicator();
}
FLASHMEM void UI_LiveSeq_PianoRoll::setNumBars(int8_t num) {
  view.numBars = num;
  if (view.startBar > data.numberOfBars - view.numBars) {
    setBarStart(data.numberOfBars - view.numBars);
  }
  view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
  reloadNotes(view.startBar, view.numBars);
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setBarStart(int8_t num) {
  view.startBar = num;
  DBG_LOG(printf("pattern from %i to %i\n", view.startBar, view.startBar + view.numBars - 1));
  reloadNotes(view.startBar, view.numBars);
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::toggleSettingActivated(void) {
  isSettingActivated = !isSettingActivated;
  drawHeader(HEADER_DRAW_ALL);
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleEncoderRight(int8_t diff) {
  if (isSettingActivated) {
    uint16_t headerFlags = 0;
    switch (mode) {
    case MODE_VIEW:
      switch (viewMode) {
      case VIEW_ZOOM_Y:
        if (hasConstrainedChanged(view.numOctaves, -2 * diff, 1, 5)) { // only change between 1, 3, 5 octaves
          setStartOctave(view.startOctave + diff); // zoom octave-centered
          setNumOctaves(view.numOctaves);
          drawFlags |= DRAW_CONTENT_FULL | DRAW_PIANOROLL;
        }
        break;

      case VIEW_SCROLL_Y:
        if (hasConstrainedChanged(view.startOctave, -diff, 0, 8 - view.numOctaves)) { // octaves 0 (note 24) upto 7 (note 119)
          setStartOctave(view.startOctave);
          drawFlags |= DRAW_CONTENT_FULL;
        }
        break;

      case VIEW_ZOOM_X:
        if (hasConstrainedChanged(view.numBars, -diff, 1, data.numberOfBars)) {
          setNumBars(view.numBars);
          drawFlags |= DRAW_CONTENT_FULL;
        }
        break;

      case VIEW_SCROLL_X:
        if (hasConstrainedChanged(view.startBar, diff, 0, data.numberOfBars - view.numBars)) {
          setBarStart(view.startBar);
          drawFlags |= DRAW_CONTENT_FULL;
        }
        break;
      }
      if (drawFlags && data.isRunning) {
        drawFlags |= DRAW_CURSOR; // handle cursor off on viewport changed redraw
      }
      break;

    case MODE_EDIT:
      headerFlags |= (1 << editMode); // default update only current edit mode setting
      switch (editMode) {
      case EDIT_SELECT_NOTE:
        if (selectedNote > -1) {
          int8_t prev = selectedNote;
          if (hasConstrainedChanged(selectedNote, diff, 0, notePairs.size() - 1)) {
            queueDrawNote(notePairs.at(prev));
            queueDrawNote(notePairs.at(selectedNote));
            headerFlags = HEADER_DRAW_ALL; // update all
          }
        }
        break;

      case EDIT_LAYER:
        if (selectedNote > -1) {
          int8_t layer = notePairs.at(selectedNote).noteOn.layer;
          if (hasConstrainedChanged(layer, diff, 0, data.trackSettings[data.activeTrack].layerCount -1)) {
            notePairs.at(selectedNote).noteOn.layer = layer;
            notePairs.at(selectedNote).noteOff.layer = layer;
            headerFlags |= (1 << EDIT_LAYER);
            queueDrawNote(notePairs.at(selectedNote));
          }
        }
        break;

      case EDIT_NOTEON:
      case EDIT_NOTEOFF:
        if (selectedNote > -1) {
          LiveSequencer::MidiEvent &on = notePairs.at(selectedNote).noteOn;
          LiveSequencer::MidiEvent &off = notePairs.at(selectedNote).noteOff;
          const bool isNoteOn = editMode == EDIT_NOTEON;
          const uint32_t noteOnTime = getEventTime(on);
          const uint32_t noteOffTime = getEventTime(off);
          uint16_t time = isNoteOn ? noteOnTime : noteOffTime;
          const uint16_t min = isNoteOn ? 0 : noteOnTime;
          const uint16_t max = isNoteOn ? noteOffTime : data.numberOfBars * data.patternLengthMs;
          if (hasConstrainedChanged(time, ENCODER[ENC_R].speed() * diff, min, max)) {
            if (isNoteOn) {
              setEventTime(on, time);
              headerFlags |= (1 << EDIT_NOTEON); // also update off time
            }
            else {
              setEventTime(off, time);
              headerFlags |= (1 << EDIT_NOTEOFF); // draw noteOff in both cases
            }
            
            liveSeq.requestSortEvents(); // re-sort events as note time changed

            const uint16_t minTime = std::min(noteOnTime, getEventTime(on));
            const uint16_t maxTime = std::max(noteOffTime, getEventTime(off));
            const UpdateRange range = {
              .fromX = getNoteCoord(minTime),
              .toX = getNoteCoord(maxTime),
              .notes = { on.note_in }
            };
            queueDrawNote(range);
          }
        }
        break;

      case EDIT_VELOCITY:
        if (selectedNote > -1) {
          LiveSequencer::MidiEvent &e = notePairs.at(selectedNote).noteOn;
          int8_t velo = notePairs.at(selectedNote).noteOn.note_in_velocity;
          if (hasConstrainedChanged(velo, ENCODER[ENC_R].speed() * diff, 0, 127)) {
            e.note_in_velocity = velo;
          }
        }
        break;

      case EDIT_NOTE:
        if (selectedNote > -1) {
          LiveSequencer::NotePair &thisNote = notePairs.at(selectedNote);
          int8_t prevNote = thisNote.noteOn.note_in;
          int8_t note = thisNote.noteOn.note_in;
          if (hasConstrainedChanged(note, diff, view.lowestNote, view.highestNote)) {
            thisNote.noteOn.note_in = note;
            thisNote.noteOff.note_in = note;

            const UpdateRange range = {
              .fromX = getNoteCoord(getEventTime(thisNote.noteOn)),
              .toX = getNoteCoord(getEventTime(thisNote.noteOff)),
              .notes = { uint8_t(note), uint8_t(prevNote) }
            };
            queueDrawNote(range);
          }
        }
        break;

      case EDIT_DELETE:
        // nothing to do on encoder turn
        break;
      }
      break;

    case MODE_STEP:
      switch (stepMode) {
      case STEP_RECORD:
        if (data.isRunning == false) {
          step(diff);
        }
        break;

      case STEP_SIZE: {
          bool changed = false;
          const uint8_t oldSteps = stepRecordSteps;
          if (diff > 0) {
            changed = recordSteps->next();
          }
          if (diff < 0) {
            changed = recordSteps->previous();
          }
          if (changed) {
            stepCursorPositionIndex = round(stepCursorPositionIndex * stepRecordSteps / float(oldSteps)); // estimation to keep absolute position
            stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);
            headerFlags |= (1 << STEP_SIZE);
            DBG_LOG(printf("steps: %i, size ms: %i\n", stepRecordSteps, int(stepRecordStepSizeMs)));
          }
        }
        break;

      case STEP_LAYER: {
          const uint8_t maxLayer = std::min(data.trackSettings[data.activeTrack].layerCount, uint8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1));
          if (hasConstrainedChanged(stepRecordLayer, diff, 0, maxLayer)) {
            DBG_LOG(printf("step record layer: %i\n", stepRecordLayer));
            headerFlags |= (1 << STEP_LAYER);
          }
          break;
        }
      }
    }
    drawHeader(headerFlags);
  }
  else {
    uint8_t oldMode = 0;
    uint8_t newMode = 0;
    switch (mode) {
    case MODE_VIEW:
      oldMode = viewMode;
      newMode = constrain(viewMode + diff, 0, VIEW_NUM - 1);
      viewMode = newMode;
      buttonView->drawNow();
      break;

    case MODE_EDIT:
      oldMode = editMode;
      newMode = constrain(editMode + diff, 0, EDIT_NUM - 1);
      editMode = newMode;
      buttonEdit->drawNow();
      break;

    case MODE_STEP:
      oldMode = stepMode;
      newMode = constrain(stepMode + diff, 0, STEP_NUM - 1);
      stepMode = newMode;
      if ((data.isRunning == false) && (newMode == STEP_RECORD)) {
        drawFlags |= DRAW_STEP_CURSOR;
      }
      queueDrawNote({ stepCursorPosition, stepCursorPosition, {} }); // refresh step cursor
      buttonStep->drawNow();
      break;
    }
    if (oldMode != newMode) {
      drawHeader((1 << oldMode) | (1 << newMode));
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::setEventTime(LiveSequencer::MidiEvent &e, uint32_t time) {
  e.patternNumber = time / data.patternLengthMs;
  e.patternMs = time % data.patternLengthMs;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawHeader(uint16_t modeFlags) {
  display.setTextSize(1);
  
  char s[10] = { "\0" };

  if (modeFlags == HEADER_DRAW_ALL) {
    display.fillRect(GRID_X[2], CONTENT_HEIGHT + LINE_HEIGHT, DISPLAY_WIDTH - GRID_X[2], HEADER_HEIGHT, HEADER_BG_COLOR);
  }

  switch (mode) {
  case MODE_VIEW:
    if (modeFlags & (1 << VIEW_ZOOM_X)) {
      drawHeaderSetting(GRID_X[2], "ZOOM X", "", (viewMode == VIEW_ZOOM_X), false);
    }
    if (modeFlags & (1 << VIEW_SCROLL_X)) {
      drawHeaderSetting(GRID_X[3], "SCROLL X", "", (viewMode == VIEW_SCROLL_X), false);
    }
    if (modeFlags & (1 << VIEW_ZOOM_Y)) {
      drawHeaderSetting(GRID_X[2], "", "ZOOM Y", false, (viewMode == VIEW_ZOOM_Y));
    }
    if (modeFlags & (1 << VIEW_SCROLL_Y)) {
      drawHeaderSetting(GRID_X[3], "", "SCROLL Y", false, (viewMode == VIEW_SCROLL_Y));
    }
    break;

  case MODE_EDIT:
    if (selectedNote > -1) {
      LiveSequencer::NotePair &p = notePairs.at(selectedNote);
      std::string noteString = getNoteString(p.noteOn.note_in);
      if (noteString.length() == 2) {
        noteString.append(" ");
      }

      if (modeFlags & (1 << EDIT_SELECT_NOTE)) {
        sprintf(s, "%03i", selectedNote + 1);
        drawHeaderSetting(GRID_X[2], "SEL", s, false, (editMode == EDIT_SELECT_NOTE));
      }
      if (modeFlags & (1 << EDIT_LAYER)) {
        sprintf(s, "%i", p.noteOn.layer + 1);
        drawHeaderSetting(GRID_X[2] + 25, "LAYR", s, false, (editMode == EDIT_LAYER));
      }
      if (modeFlags & (1 << EDIT_NOTEON)) {
        sprintf(s, "/ %i.%04i", p.noteOn.patternNumber, p.noteOn.patternMs);
        drawHeaderSetting(GRID_X[3], s, "", (editMode == EDIT_NOTEON), false);
      }
      if (modeFlags & (1 << EDIT_NOTEOFF)) {
        sprintf(s, "\\ %i.%04i", p.noteOff.patternNumber, p.noteOff.patternMs);
        drawHeaderSetting(GRID_X[3], "", s, false, (editMode == EDIT_NOTEOFF));
      }
      if (modeFlags & (1 << EDIT_VELOCITY)) {
        sprintf(s, "%03u", p.noteOn.note_in_velocity);
        drawHeaderSetting(GRID_X[4], "VEL", s, false, (editMode == EDIT_VELOCITY));
      }
      if (modeFlags & (1 << EDIT_NOTE)) {
        sprintf(s, "%s", noteString.c_str());
        drawHeaderSetting(GRID_X[4] + 25, "NOTE", s, false, (editMode == EDIT_NOTE));
      }
    }
    break;

  case MODE_STEP:
    if (modeFlags & (1 << STEP_RECORD)) {
      const uint8_t pattern = (stepCursorPositionIndex / stepRecordSteps);
      sprintf(s, "P%i %02i/%02i", pattern, stepCursorPositionIndex % stepRecordSteps, stepRecordSteps);
      drawHeaderSetting(GRID_X[2], "REC/ADV", s, false, (stepMode == STEP_RECORD));
    }
    if (modeFlags & (1 << STEP_SIZE)) {
      sprintf(s, "1/%02i", stepRecordSteps);
      drawHeaderSetting(GRID_X[3], "STEPSIZE", s, false, (stepMode == STEP_SIZE));

    }
    if (modeFlags & (1 << STEP_LAYER)) {
      sprintf(s, "%i", stepRecordLayer + 1);
      drawHeaderSetting(GRID_X[4], "TO LAYER", s, false, (stepMode == STEP_LAYER));
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
  // draw vertical pattern lines
  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numBars * substeps);
  for (uint8_t step = 0; step < view.numBars * substeps; step++) {
    const uint16_t x = ROLL_WIDTH + round(step * stepWidth);
    const uint16_t lineColor = (step % substeps == 0) ? GREY2 : GREY3;
    for (auto &u : drawRegionsX) {
      const bool drawLine = ((x >= u.fromX) && (x <= u.toX));
      if (drawLine) {
        if (u.notes.empty()) {
          display.console = true;
          display.drawLine(x, 0, x, CONTENT_HEIGHT - 1, lineColor);
        }
        else {
          // only draw lines where single note update is needed and key *not* pressed
          for (uint8_t note : u.notes) {
            if (keysPressed.find(note) == keysPressed.end()) {
              const uint16_t y = CONTENT_HEIGHT - (note - view.lowestNote + 1) * view.noteHeight;
              display.console = true;
              display.drawLine(x, y, x, y + view.noteHeight - 1, lineColor);
            }
          }
        }
      }
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(LiveSequencer::NotePair note) {
  UpdateRange coords;
  coords.fromX = getNoteCoord(getEventTime(note.noteOn));
  coords.toX = getNoteCoord(getEventTime(note.noteOff));
  coords.notes = { note.noteOn.note_in };
  queueDrawNote(coords);
}

FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(UpdateRange coords) {
  DBG_LOG(printf("queue note draw: from %i to %i\n", coords.fromX, coords.toX));
  drawRegionsX.push_back(coords);
  drawFlags |= DRAW_CONTENT_AREA;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getNoteCoord(uint32_t time) const {
  const uint16_t timeAbs = getRelativeNoteTime(time);
  const uint16_t coord = ROLL_WIDTH + round(timeAbs * view.msToPix);
  return coord;
}

FLASHMEM uint16_t UI_LiveSeq_PianoRoll::getRelativeNoteTime(uint32_t time) const {
  int32_t timeRel = time - (view.startBar * data.patternLengthMs);
  const int32_t maxTime = (view.startBar + view.numBars) * data.patternLengthMs;
  timeRel = std::max(int32_t(0), timeRel); // clamp noteOn offscreen left
  timeRel = std::min(timeRel, maxTime);    // clamp noteOff offscreen right
  return timeRel;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNotes(void) {
  if (notePairs.size()) {
    int16_t noteIndex = 0;
    for (LiveSequencer::NotePair &note : notePairs) {
      drawNote(note, (noteIndex == selectedNote));
      noteIndex++;
    }
  }
  else {
    const std::string text = "SELECTION HAS NO NOTES";
    const uint16_t stringWidth = text.length() * CHAR_width_small;
    display.console = true;
    display.setCursor((DISPLAY_WIDTH - stringWidth) / 2, CONTENT_HEIGHT / 2);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(0);
    display.print(text.c_str());
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note, bool isSelected) const {
  const uint16_t timeOn = getEventTime(note.noteOn);
  const uint16_t timeOff = getEventTime(note.noteOff);
  const uint16_t x = getNoteCoord(timeOn);
  const uint16_t w = (getRelativeNoteTime(timeOff) - getRelativeNoteTime(timeOn)) * view.msToPix;
  DBG_LOG(printf("note: %i, x: %i, ton: %i, toff: %i, w: %i\n", note.noteOn.note_in, x, timeOn, timeOff, w));

  for (auto &u : drawRegionsX) {
    const bool isInsideX = (x <= u.toX) && ((x + w) >= u.fromX); // FIXME: selected 0 with noteOn far left does not highlight
    const bool isInsideY = u.notes.count(note.noteOn.note_in) || u.notes.empty();

    if (isInsideX && isInsideY) {
      const uint16_t y = CONTENT_HEIGHT - (note.noteOn.note_in - view.lowestNote + 1) * view.noteHeight;
      const uint16_t h = view.noteHeight;
      const uint16_t color = isSelected ? COLOR_SYSTEXT : layerColors.at(note.noteOn.layer);
      
      display.console = true;
      //DBG_LOG(printf("draw note %i: %i-%i, selected: %i\n", note.noteOn.note_in, x, x + w, isSelected));
      if (w > 1) {
        if (note.isMuted) {
          display.drawRect(x, y, w, h, color);
        } else {
          display.fillRect(x, y, w, h, color);
        }
      }
      else {
        display.drawLine(x, y, x, y + h - 1, color);
      }
    }
    else {
      //DBG_LOG(printf("not drawing note note %i: %i-%i\n", note.noteOn.note_in, x, x + w));
    }
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
      if (selectedNote > -1) { // de-select note from edit
        queueDrawNote(notePairs.at(selectedNote));
        selectedNote = -1;
      }
      break;

    case MODE_STEP:
      // reset step cursor
      stepMode = STEP_RECORD;
      buttonStep->drawNow();
      queueDrawNote({ stepCursorPosition, stepCursorPosition, {} }); // clear step cursor
      stepCursorPositionIndex = 0;
      stepCursorPosition = ROLL_WIDTH;
      break;
    }

    // entering new mode actions
    switch (newMode) {
    case MODE_VIEW:
      break;

    case MODE_EDIT:
      if ((selectedNote == -1) && notePairs.size()) {
        selectedNote = 0; // auto select first note
        if (oldMode != MODE_STEP) {
          queueDrawNote(notePairs.at(0));
        }
      }
      break;

    case MODE_STEP:
      stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);
      drawFlags |= DRAW_STEP_CURSOR;
      break;
    }
    drawFlags |= DRAW_CONTENT_AREA;
    ui_pianoroll->drawHeader(HEADER_DRAW_ALL);
  }
  return modeChanged;
}

FLASHMEM void UI_LiveSeq_PianoRoll::deleteSelectedNote(void) {
  // delete (invalidate) this note pair
  if (selectedNote > -1) {
    notePairs.at(selectedNote).noteOn.event = midi::InvalidType;
    notePairs.at(selectedNote).noteOff.event = midi::InvalidType;
    ui_pianoroll->reloadNotes(view.startBar, view.numBars);
    drawFlags |= DRAW_CONTENT_FULL;
    editMode = EDIT_SELECT_NOTE; // after delete, switch back to select mode
    ui_pianoroll->drawHeader(1 << EDIT_SELECT_NOTE);
    selectedNote = notePairs.empty() ? -1 : 0;
    buttonEdit->drawNow();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::initGUI(void) {
  const uint8_t BUTTON_SPACING = 4;  // center in screen
  const uint16_t y = CONTENT_HEIGHT + HEADER_HEIGHT + LINE_HEIGHT;

  for (uint8_t i = 0; i < LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN; i++) {
    GRID_X[i] = i * (TouchButton::BUTTON_SIZE_X + BUTTON_SPACING);
  }

  buttonPlay = new TouchButton(GRID_X[0], y, 
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

  buttons.push_back(new ValueButtonRange<uint8_t>(&currentValue, GRID_X[1], y, data.activeTrack, 0, LiveSequencer::LIVESEQUENCER_NUM_TRACKS - 1, 1, data.activeTrack, 
  [ this ] (auto *b, auto *v) { // drawHandler
    char temp_char[2];
    itoa(data.activeTrack + 1, temp_char, 10);
    b->draw("TRACK", temp_char, TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    ui_pianoroll->reloadNotes(view.startBar, view.numBars, true);
    if (mode == MODE_EDIT) {
      selectedNote = notePairs.size() ? 0 : -1;
    }
    buttonInst->drawNow();
    drawFlags |= DRAW_CONTENT_FULL;
    ui_pianoroll->drawLayersIndicator();
  }));

  buttonInst = new TouchButton(GRID_X[2], y, 
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

  buttonView = new TouchButton(GRID_X[3], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("VIEW", "MODE", (mode == MODE_VIEW) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    ui_pianoroll->setMode(MODE_VIEW);
    b->drawNow();
  });
  buttons.push_back(buttonView);

  buttonEdit = new TouchButton(GRID_X[4], y,
  [ this ] (auto *b) { // drawHandler
    TouchButton::Color color = (mode == MODE_EDIT) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;
    if ((mode == MODE_EDIT) && (editMode == EDIT_DELETE)) {
      color = TouchButton::BUTTON_RED;
    }
    const std::string txt = (editMode == EDIT_DELETE) ? "NOTE" : "EDIT";
    const std::string sub = (editMode == EDIT_DELETE) ? "DELETE" : "MODE";
    b->draw(txt.c_str(), sub.c_str(), color);
  }, [ this ] (auto *b) { // clickedHandler
    ui_pianoroll->setMode(MODE_EDIT);
    if (editMode == EDIT_DELETE) {
      ui_pianoroll->deleteSelectedNote();
    }
    b->drawNow();
  });
  buttons.push_back(buttonEdit);

  buttonStep = new TouchButton(GRID_X[5], y,
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
  const bool stepCursorChanged = hasConstrainedChanged(stepCursorPositionIndex, diff, 0, view.numBars * stepRecordSteps);
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
