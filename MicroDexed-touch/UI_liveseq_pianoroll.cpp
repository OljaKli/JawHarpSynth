#include "UI_liveseq_pianoroll.h"
#include "UI.h"
#include "touch.h"
#include "ILI9341_t3n.h"
#include "LCDMenuLib2.h"
#include "ui_livesequencer.h"

extern LCDMenuLib2 LCDML;
extern ILI9341_t3n display;

UI_LiveSeq_PianoRoll* ui_pianoroll;

UI_LiveSeq_PianoRoll::UI_LiveSeq_PianoRoll(LiveSequencer &sequencer, LiveSequencer::LiveSeqData &d) :
  liveSeq(sequencer),
  data(d),
  isRunningHere(d.isRunning) {
  ui_pianoroll = this;
  layerColors.push_back(COLOR_ARP); // layer 0
  layerColors.push_back(COLOR_CHORDS);
  layerColors.push_back(COLOR_DRUMS);
  layerColors.push_back(COLOR_PITCHSMP);
  initGUI();
  view.cursorPos = ROLL_WIDTH;
  view.cursorPosPrev = ROLL_WIDTH;
}

FLASHMEM void UI_LiveSeq_Graphic(uint8_t param) {  // for Livesequencer
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
    if(hasNotes) {
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
  const uint16_t h = hTotal * view.numOctaves / 8.0f; // we have 8 octaves here
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
    stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);

    isVisible = true;
    registerTouchHandler(handle_touchscreen_pianoroll);

    setNumOctaves(5);
    setStartOctave(3);
    setNumBars(data.numberOfBars);
    setBarStart(0);
    reloadNotes(view.startBar, view.numBars);
   
    // setup function
    display.console = true;
    display.fillScreen(COLOR_BACKGROUND);
    LCDML.FUNC_setLoopInterval(25); // 40Hz gui refresh

    display.drawLine(0, CONTENT_HEIGHT, DISPLAY_WIDTH, CONTENT_HEIGHT, GREY2);
    
    drawLayersIndicator();
    drawAreaIndicator();

    printActionName();
    printActionValue();

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
      switch (mode) {
      case MODE_VIEW:
        if (++viewMode == VIEW_NUM) {
          viewMode = VIEW_ZOOM_Y;
        }
        buttonView->drawNow();
        printActionName();
        printActionValue();
        break;

      case MODE_EDIT:
        if (++editMode == EDIT_NUM) {
          editMode = EDIT_SELECT_NOTE;
        }
        buttonEdit->drawNow();
        printActionName();
        printActionValue();
        break;

      case MODE_STEP:
        if (++stepMode == STEP_NUM) {
          stepMode = STEP_RECORD;
        }
        if(stepMode == STEP_RECORD) {
          drawFlags |= DRAW_STEP_CURSOR;
        }
        else {
          // clear step cursor
          drawFlags |= DRAW_CONTENT_AREA;
        }
        drawRegionsX.push_back({ stepCursorPosition, stepCursorPosition, true }); // refresh step cursor

        buttonStep->drawNow();
        printActionName();
        printActionValue();
        break;
      }
    }
    const bool runningChanged = isRunningHere != data.isRunning;
    const bool justStopped = isRunningHere && !data.isRunning;
    if (runningChanged) {
      isRunningHere = data.isRunning;
      buttonPlay->drawNow();
      buttonStep->drawNow(); // handle active state
    }

    if(isRunningHere || justStopped) {
      const uint16_t patternTime = isRunningHere ? ((data.currentPattern - view.startBar) * data.patternLengthMs + data.patternTimer) : 0;
      view.cursorPos = ROLL_WIDTH + round(patternTime * view.msToPix);
      const bool offLeft = (data.currentPattern < view.startBar);
      const bool offRight = (data.currentPattern >= (view.startBar + view.numBars));
      view.cursorPos = offLeft ? ROLL_WIDTH : view.cursorPos;
      view.cursorPos = offRight ? DISPLAY_WIDTH - 1 : view.cursorPos;
      const uint16_t color = (offLeft || offRight) ? RED : MIDDLEGREEN;
      const bool colorChanged = color != view.cursorColor;
      const bool cursorPosChanged = (view.cursorPos != view.cursorPosPrev);

      if(justStopped || colorChanged || cursorPosChanged) {
        drawRegionsX.push_back({ view.cursorPosPrev, view.cursorPosPrev, true });
        drawFlags |= DRAW_CONTENT_AREA;

        if(!justStopped) {
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

FLASHMEM void UI_LiveSeq_PianoRoll::reloadNotes(uint8_t patternFrom, uint8_t numBars) {
  notePairs = liveSeq.getNotePairsFromTrack(data.activeTrack, patternFrom, patternFrom + numBars - 1);
  DBG_LOG(printf("%i/%i: got %i note pairs\n", patternFrom, patternFrom + numBars - 1, notePairs.size()));
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawGUI(void) {
  // NOTE: order of drawing is important
  if (drawFlags & DRAW_CONTENT_FULL) {
    drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
    drawRegionsX.push_back({ ROLL_WIDTH, DISPLAY_WIDTH, false }); // force redraw all
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
  singleNoteDraw.clear();
  drawRegionsX.clear();
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleKeyChanged(uint8_t key, midi::MidiType event, uint8_t velocity) {
  if (isVisible) {
    switch (event) {
    case midi::NoteOn:
      keysPressed.insert(key);
      break;

    case midi::NoteOff:
      keysPressed.erase(key);
      break;

    default:
      break;
    }

    const bool isStepRecording = (mode == MODE_STEP) && (stepMode == STEP_RECORD);
    if(isStepRecording == false) { // step record will draw itself
      singleNoteDraw.insert(key);
      drawFlags |= DRAW_CONTENT_FULL;
    }
    else {
      if((data.isRunning == false) && (event == midi::NoteOn)) {
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
        if(stepRecordLayer + 1 > data.trackSettings[data.activeTrack].layerCount) {
          data.trackSettings[data.activeTrack].layerCount++;
          drawLayersIndicator();
        }
        liveSeq.addNotePair(on, off);
        reloadNotes();
        drawFlags |= DRAW_NOTES;
      }
      else if (keysPressed.empty()) {
        // advance on last key released
        step(+1);
      }
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
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setStartOctave(int8_t num) {
  view.startOctave = num;
  view.lowestNote = 24 + view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
  drawAreaIndicator();
}
FLASHMEM void UI_LiveSeq_PianoRoll::setNumBars(int8_t num) {
  view.numBars = num;
  if(view.startBar > data.numberOfBars - view.numBars) {
    setBarStart(data.numberOfBars - view.numBars);
  }
  view.msToPix = CONTENT_WIDTH / float(view.numBars * data.patternLengthMs);
  reloadNotes(view.startBar, view.numBars);
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::setBarStart(int8_t num) {
  view.startBar = num;
  DBG_LOG(printf("pattern from %i to %i\n", view.startBar, view.startBar + view.numBars));
  reloadNotes(view.startBar, view.numBars);
  drawAreaIndicator();
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleEncoderRight(int8_t diff) {
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
    if(drawFlags && data.isRunning) {
      drawFlags |= DRAW_CURSOR; // handle cursor off on viewport changed redraw
    }
    break;

  case MODE_EDIT:
    switch (editMode) {
    case EDIT_SELECT_NOTE:
      if(selectedNote > -1) {
        if (hasConstrainedChanged(selectedNote, diff, 0, notePairs.size() - 1)) {
          int8_t prev = selectedNote - diff;
          if (prev < 0) {
            prev = 0; // make nicer
          }
          queueDrawNote(notePairs.at(prev));
          queueDrawNote(notePairs.at(selectedNote));
        }
      }
      break;

    case EDIT_NOTEON:
    case EDIT_DURATION:
      if (selectedNote > -1) {
        LiveSequencer::MidiEvent &on = notePairs.at(selectedNote).noteOn;
        LiveSequencer::MidiEvent &off = notePairs.at(selectedNote).noteOff;
        const bool isNoteOn = editMode == EDIT_NOTEON;
        uint16_t time = isNoteOn ? on.patternMs : off.patternMs;
        const uint16_t min = isNoteOn ? 0 : on.patternMs;
        const uint16_t max = isNoteOn ? data.patternLengthMs - (off.patternMs - on.patternMs) : data.patternLengthMs;
        if (hasConstrainedChanged(time, diff, min, max)) {
          queueDrawNote(notePairs.at(selectedNote));
 
          if(isNoteOn) {
            off.patternMs += (time - on.patternMs);
            on.patternMs = time;
            liveSeq.requestSortEvents(); // re-sort events as noteOn changed
          }
          else {
            off.patternMs = time;
          }
        }
      }
      break;

    case EDIT_VELOCITY:
      if (selectedNote > -1) {
        LiveSequencer::MidiEvent &e = notePairs.at(selectedNote).noteOn;
        int8_t velo = notePairs.at(selectedNote).noteOn.note_in_velocity;
        if (hasConstrainedChanged(velo, diff, 0, 127)) {
          e.note_in_velocity = velo;
          printActionValue();
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
          queueDrawNote(thisNote);
          singleNoteDraw.insert(prevNote);
        }
      }
      break;

    case EDIT_DELETE:
      // nothing to do on encoder turn
      break;
    }
    break;

  case MODE_STEP:
    switch(stepMode) {
    case STEP_RECORD:
      if(step(diff)) {
        printActionValue();
      }
      break;

    case STEP_SIZE:
      if (hasConstrainedChanged(stepRecordSteps, diff, 1, 32)) {
        stepRecordStepSizeMs = data.patternLengthMs / float(stepRecordSteps);
        printActionValue();
        DBG_LOG(printf("step size ms: %i\n", stepRecordStepSizeMs));
      }
      break;

    case STEP_LAYER: {
        const uint8_t maxLayer = std::min(data.trackSettings[data.activeTrack].layerCount, uint8_t(LiveSequencer::LIVESEQUENCER_NUM_LAYERS - 1));
        if (hasConstrainedChanged(stepRecordLayer, diff, 0, maxLayer)) {
          DBG_LOG(printf("step record layer: %i\n", stepRecordLayer));
          printActionValue();
        }
        break;
      }
    }
  }
  if(drawFlags) {
    printActionValue();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::printActionName(void) {
  display.setTextSize(1);
  display.setCursor(GRID_X[2], CONTENT_HEIGHT + 4);
  display.setTextColor(COLOR_SYSTEXT, HEADER_BG_COLOR);
  
  switch (mode) {
  case MODE_EDIT:
    switch (editMode) {
    case EDIT_SELECT_NOTE:
      display.print("SELECT  ");
      break;
      
    case EDIT_NOTEON:
      display.print("ON TIME ");
      break;

    case EDIT_DURATION:
      display.print("OFF TIME");
      break;

    case EDIT_VELOCITY:
      display.print("VELOCITY");
      break;

    case EDIT_NOTE:
      display.print("NOTE    ");
      break;

    case EDIT_DELETE:
      display.print("DELETE  ");
      break;
    }
    break;

  case MODE_VIEW:
    display.print("        ");
    break;

  case MODE_STEP:
    switch(stepMode) {
    case STEP_RECORD:
      display.print("REC/ADV ");
      break;

    case STEP_SIZE:
      display.print("STEPSIZE");
      break;

    case STEP_LAYER:
      display.print("TO LAYER");
      break;
    }
    break;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::printActionValue(void) {
  char s[10] = { "\0" };
  switch (mode) {
  case MODE_EDIT:
    if(selectedNote > -1) {
      LiveSequencer::NotePair &p = notePairs.at(selectedNote);
      const char *noteString = getNoteString(p.noteOn.note_in).c_str();
      switch (editMode) {
      case EDIT_SELECT_NOTE:
        sprintf(s, "%i: %s", selectedNote + 1, noteString);
        break;

      case EDIT_NOTEON:
        sprintf(s, "%ims", p.noteOn.patternMs);
        break;

      case EDIT_DURATION:
        sprintf(s, "%ims", p.noteOff.patternMs);
        break;

      case EDIT_VELOCITY:
        sprintf(s, "%i", p.noteOn.note_in_velocity);
        break;

      case EDIT_NOTE:
        sprintf(s, "%s", noteString);
        break;

      case EDIT_DELETE:
        sprintf(s, "CONFIRM");
        break;
      }      
      break;
    }

  case MODE_VIEW:
    break;

  case MODE_STEP:
    switch (stepMode) {
    case STEP_RECORD:
      sprintf(s, "1/%i", stepRecordSteps);
      break;

    case STEP_SIZE:
      sprintf(s, "1/%i", stepRecordSteps);
      break;

    case STEP_LAYER:
      sprintf(s, "%i", stepRecordLayer + 1);
      break;
    }
    break;
  }
  display.setTextSize(1);
  display.setCursor(GRID_X[2], CONTENT_HEIGHT + 6 + CHAR_height_small);
  display.setTextColor(COLOR_SYSTEXT, HEADER_BG_COLOR);
  const uint8_t whitespace = 8 - (sizeof(s) / sizeof(char*));
  display.print(s);
  display.printf("%*s", whitespace, " "); // fill spaces
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
  for(auto &u : drawRegionsX) {
    for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
      const bool drawSingleNote = singleNoteDraw.find(note) != singleNoteDraw.end();
      
      if (u.fullHeight || singleNoteDraw.empty() || drawSingleNote) {
        const uint8_t noteIndex = view.highestNote - note - 1;
        const uint16_t y = round(noteIndex * view.noteHeight);
        const bool isPressed = keysPressed.find(note) != keysPressed.end();
        const uint16_t bgColor = isPressed ? GREY2 : (note & 0x01 ? GREY4 : COLOR_BACKGROUND);
        display.console = true;

        //DBG_LOG(printf("draw X %i to %i\n", u.fromX, u.toX));
        const uint16_t width = u.toX - u.fromX;
        const uint16_t xFrom = std::max(ROLL_WIDTH, u.fromX);
        if(width > 1) {
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
    bool drawLine = false;
    for(auto &u : drawRegionsX) {
      drawLine |= (x >= u.fromX) && (x <= u.toX);
    }
    if(drawLine) {
      const uint16_t lineColor = (step % substeps == 0) ? GREY2 : GREY3;
      display.console = true;
      if (singleNoteDraw.empty()) {// && keysPressed.empty()) {
        display.drawLine(x, 0, x, CONTENT_HEIGHT - 1, lineColor);
      }
      else {
        for (uint8_t note : singleNoteDraw) {
          if (note >= view.lowestNote && note < view.highestNote) {
            // only draw lines where single note update is needed and key *not* pressed
            if (keysPressed.find(note) == keysPressed.end()) {
              const uint16_t y = CONTENT_HEIGHT - (note - view.lowestNote + 1) * view.noteHeight;
              display.drawLine(x, y, x, y + view.noteHeight - 1, lineColor);
            }
          }
        }
      }
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNotes(void) {
  if (notePairs.size()) {
    uint16_t index = 0;
    for (LiveSequencer::NotePair note : notePairs) {
      const bool isVisibleY = note.noteOn.note_in >= view.lowestNote &&
                              note.noteOn.note_in < view.highestNote;
      bool isSelected = (index++ == selectedNote);
      if (isVisibleY) {
        drawNote(note, isSelected);
      }
    }
  }
  else {
    const std::string text = "TRACK HAS NO NOTES";
    const uint16_t stringWidth = text.length() * CHAR_width_small;
    display.console = true;
    display.setCursor((DISPLAY_WIDTH - stringWidth) / 2, CONTENT_HEIGHT / 2);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    display.setTextSize(0);
    display.print(text.c_str());
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::queueDrawNote(LiveSequencer::NotePair note) {
  singleNoteDraw.insert(note.noteOn.note_in);
  DBG_LOG(printf("queue note draw: %i\n", note.noteOn.note_in));
  SubUpdateX coords;
  coords.fromX = ROLL_WIDTH + round(midiEventTime(note.noteOn) - view.startBar * data.patternLengthMs) * view.msToPix;
  coords.toX = coords.fromX + round((midiEventTime(note.noteOff) - midiEventTime(note.noteOn)) * view.msToPix);
  coords.fullHeight = false;
  drawRegionsX.push_back(coords);
  drawFlags |= DRAW_CONTENT_AREA;
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note, bool isSelected) {
  const bool drawSingleNote = singleNoteDraw.find(note.noteOn.note_in) != singleNoteDraw.end();
  uint16_t x = ROLL_WIDTH + (round(midiEventTime(note.noteOn) - view.startBar * data.patternLengthMs) * view.msToPix);
  const uint16_t y = CONTENT_HEIGHT - (note.noteOn.note_in - view.lowestNote + 1) * view.noteHeight;
  const uint16_t h = view.noteHeight;
  uint16_t w = round((midiEventTime(note.noteOff) - midiEventTime(note.noteOn)) * view.msToPix);

  //DBG_LOG(printf("draw note %i\n", note.noteOn.note_in));
  //DBG_LOG(printf("x: %i, y: %i, w: %i, h: %i\n", x, y, w, h));

  for(auto &u : drawRegionsX) {
    if (u.fullHeight || singleNoteDraw.empty() || drawSingleNote) {

      const bool isOutside = (x > u.toX) || ((x + w) < u.fromX);

      if(isOutside == false) { // 
        const uint16_t color = isSelected ? COLOR_SYSTEXT : layerColors.at(note.noteOn.layer);

        display.console = true;
        DBG_LOG(printf("draw note %i: %i-%i\n", note.noteOn.note_in, x, x + w));
        if(w > 1) {
          if (note.isMuted) {
            display.drawRect(x, y, w, h, color);
          } else {
            display.fillRect(x, y, w, h, color);
          }
        }
        else {
          display.drawLine(x, y, x, y + h, color);
        }
      }
    }
  }
}

FLASHMEM uint32_t UI_LiveSeq_PianoRoll::midiEventTime(LiveSequencer::MidiEvent e) const {
  return e.patternNumber * data.patternLengthMs + e.patternMs;
}

FLASHMEM bool UI_LiveSeq_PianoRoll::setMode(uint8_t newMode) {
  const uint8_t oldMode = mode;
  const bool modeChanged = oldMode != newMode;
  if(modeChanged) {
    mode = newMode;
    // leaving old mode actions
    switch (oldMode) {
    case MODE_VIEW:
      buttonView->drawNow();
      break;

    case MODE_EDIT:
      buttonEdit->drawNow();
      if(selectedNote > -1) { // de-select note from edit
        singleNoteDraw.insert(ui_pianoroll->notePairs.at(selectedNote).noteOn.note_in);
        selectedNote = -1;
      }
      break;

    case MODE_STEP:
      // reset step cursor
      drawRegionsX.push_back({ stepCursorPosition, stepCursorPosition, true }); // clear step cursor
      stepCursorPositionIndex = 0;
      stepCursorPosition = ROLL_WIDTH;
      buttonStep->drawNow();
      break;
    }

    // entering new mode actions
    switch (newMode) {
    case MODE_VIEW:
      break;

    case MODE_EDIT:
      if((selectedNote == -1) && notePairs.size()) {
        selectedNote = 0; // auto select first note
        if(oldMode != MODE_STEP) {
          singleNoteDraw.insert(notePairs.at(0).noteOn.note_in);
        }
      }
      break;

    case MODE_STEP:
      drawFlags |= DRAW_STEP_CURSOR;
      break;
    }
    drawFlags |= DRAW_CONTENT_AREA;
    ui_pianoroll->printActionName();
    ui_pianoroll->printActionValue();
  }
  return modeChanged;
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
      drawRegionsX.push_back({ stepCursorPosition, stepCursorPosition, true });
    }
  });
  buttons.push_back(buttonPlay);

  buttons.push_back(new ValueButtonRange<uint8_t>(&currentValue, GRID_X[1], y, data.activeTrack, 0, LiveSequencer::LIVESEQUENCER_NUM_TRACKS - 1, 1, data.activeTrack, 
  [ this ] (auto *b, auto *v) { // drawHandler
    char temp_char[2];
    itoa(data.activeTrack + 1, temp_char, 10);
    b->draw("TRACK", temp_char, TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    ui_pianoroll->reloadNotes(view.startBar, view.numBars);
    if(mode == MODE_EDIT) {
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
    b->draw("VIEW", getViewModeString(viewMode), (mode == MODE_VIEW) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    ui_pianoroll->setMode(MODE_VIEW);
    b->drawNow();
  });
  buttons.push_back(buttonView);

  buttonEdit = new TouchButton(GRID_X[4], y,
  [ this ] (auto *b) { // drawHandler
    TouchButton::Color color = (mode == MODE_EDIT) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;
    if((mode == MODE_EDIT) && (editMode == EDIT_DELETE)) {
      color = TouchButton::BUTTON_RED;
    }
    b->draw("EDIT", getEditModeString(editMode), color);
  }, [ this ] (auto *b) { // clickedHandler
    ui_pianoroll->setMode(MODE_EDIT);
    if(editMode == EDIT_DELETE) {
      // delete (invalidate) this note pair
      if(selectedNote > -1) {
        notePairs.at(selectedNote).noteOn.event = midi::InvalidType;
        notePairs.at(selectedNote).noteOff.event = midi::InvalidType;
        ui_pianoroll->reloadNotes(view.startBar, view.numBars);
        drawFlags |= DRAW_CONTENT_AREA;
        editMode = EDIT_SELECT_NOTE; // after delete, switch back to select mode
      }
    }
    b->drawNow();
  });
  buttons.push_back(buttonEdit);

  buttonStep = new TouchButton(GRID_X[5], y,
  [ this ] (auto *b) { // drawHandler
    TouchButton::Color color = (mode == MODE_STEP) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;
    if(data.isRunning) {
      color = TouchButton::BUTTON_NORMAL;
    }
    else {
      if((mode == MODE_STEP) && (stepMode == STEP_RECORD)) {
        color = TouchButton::BUTTON_RED;
      }
    }
    b->draw("STEP", getStepModeString(stepMode), color);
  }, [ this ] (auto *b) { // clickedHandler
    if(data.isRunning == false) {
      const bool modeChanged = ui_pianoroll->setMode(MODE_STEP);
      if((modeChanged == false) && (stepMode == STEP_RECORD)) {
        ui_pianoroll->step(+1);
      }
      b->drawNow();
    }
  });
  buttons.push_back(buttonStep);
}

FLASHMEM bool UI_LiveSeq_PianoRoll::step(int8_t diff) {
  bool stepCursorChanged = false;
  if (hasConstrainedChanged(stepCursorPositionIndex, diff, 0, view.numBars * stepRecordSteps)) {
    if(stepCursorPositionIndex == view.numBars * stepRecordSteps) {
      stepCursorPositionIndex = 0;
    }
    drawRegionsX.push_back({ stepCursorPosition, stepCursorPosition, true }); // clear step cursor
    stepCursorPosition = ROLL_WIDTH + round(stepCursorPositionIndex * stepRecordStepSizeMs * view.msToPix);
    stepCursorChanged = true;
    drawFlags |= DRAW_STEP_CURSOR | DRAW_CONTENT_AREA;
  }
  return stepCursorChanged;
}

FLASHMEM std::string UI_LiveSeq_PianoRoll::getViewModeString(uint8_t subMode) {
  switch (subMode) {
  case VIEW_SCROLL_X:
    return "SCRL X";

  case VIEW_SCROLL_Y:
    return "SCRL Y";

  case VIEW_ZOOM_X:
    return "ZOOM X";

  case VIEW_ZOOM_Y:
    return "ZOOM Y";
  }
  return "NONE";
}

FLASHMEM std::string UI_LiveSeq_PianoRoll::getEditModeString(uint8_t subMode) {
  switch (subMode) {
  case EDIT_SELECT_NOTE:
    return "SELECT";

  case EDIT_NOTEON:
    return "ONTIME";

  case EDIT_VELOCITY:
    return "VELOCTY";

  case EDIT_DURATION:
    return "OFFTIME";

  case EDIT_NOTE:
    return "NOTE";

  case EDIT_DELETE:
    return "DELETE";
  }
  return "NONE";
}

FLASHMEM std::string UI_LiveSeq_PianoRoll::getStepModeString(uint8_t subMode) {
  switch (subMode) {
  case STEP_RECORD:
    return "RECORD";

  case STEP_SIZE:
    return "SIZE";

  case STEP_LAYER:
    return "LAYER";
  }
  return "NONE";
}