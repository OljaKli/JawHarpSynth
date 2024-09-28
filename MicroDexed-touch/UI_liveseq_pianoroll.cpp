#include "UI_liveseq_pianoroll.h"
#include "UI.h"
#include "touch.h"
#include "ILI9341_t3n.h"
#include "LCDMenuLib2.h"

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

FLASHMEM void UI_LiveSeq_PianoRoll::processLCDM(uint8_t param) {
  if (LCDML.FUNC_setup()) // ****** SETUP *********
  {
    data.activeTrack = param;
    isVisible = true;
    registerTouchHandler(handle_touchscreen_pianoroll);

    setNumOctaves(5);
    setStartOctave(3);
    setNumPatterns(data.numberOfBars);
    setPatternStart(0);
    reloadNotes(view.startPattern, view.numPatterns);
   
    // setup function
    display.console = true;
    display.fillScreen(COLOR_BACKGROUND);
    display.fillRect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, HEADER_BG_COLOR);
    display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
    display.setTextSize(1);
    display.setCursor(0, 1);
    display.setTextColor(RED, HEADER_BG_COLOR);
    display.print("LIVESEQ PIANOROLL");
    LCDML.FUNC_setLoopInterval(40); // 25Hz gui refresh

    static constexpr uint16_t layerViewSize = (HEADER_HEIGHT - CURSOR_HEIGHT) / 2;
    static constexpr uint16_t layerViewX = DISPLAY_WIDTH - 2 * layerViewSize;
    static constexpr uint16_t layerViewY = 0;
    for (uint8_t l = 0; l < LiveSequencer::LIVESEQUENCER_NUM_LAYERS; l++) {
      const uint16_t x = layerViewX + (l % 2) * (layerViewSize);
      const uint16_t y = layerViewY + (l / 2) * (layerViewSize);
      display.console = true;
      display.fillRect(x, y, layerViewSize, layerViewSize, layerColors[l]);
      display.setCursor(x + 2, y + 2);
      display.setTextSize(0);
      display.setTextColor(COLOR_BACKGROUND);
      display.print(l + 1);
    }

    drawFlags |= DRAW_ALL;

    for (auto *b : buttons) {
      b->drawNow();
    }

  }
  if (LCDML.FUNC_loop()) {
    if (LCDML.BT_checkDown()) {
      handleEncoderRight(-1);
    }
    if (LCDML.BT_checkUp()) {
      handleEncoderRight(+1);
    }
    if (LCDML.BT_checkEnter()) {
      switch (mode) {
      case MODE_VIEW:
        if (++viewMode == VIEW_NUM) {
          viewMode = VIEW_ZOOM_X;
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
      }
    }

    if (isRunningHere != data.isRunning) {
      isRunningHere = data.isRunning;
    }
    drawFlags |= isRunningHere ? DRAW_CURSOR : 0;
    
    drawGUI();
  }
  if (LCDML.FUNC_close()) {
    isVisible = false;
    unregisterTouchHandler();
    display.fillScreen(COLOR_BACKGROUND);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::reloadNotes(uint8_t patternFrom, uint8_t numPatterns) {
  notePairs = liveSeq.getNotePairsFromTrack(data.activeTrack, patternFrom, patternFrom + numPatterns - 1);
  DBG_LOG(printf("%i/%i: got %i note pairs\n", patternFrom, patternFrom + numPatterns - 1, notePairs.size()));
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawGUI(void) {
  // NOTE: order of drawing is important
  if (drawFlags & DRAW_CURSOR) {
    drawCursor();
  }
  if (drawFlags & DRAW_BACKGROUND) {
    drawBackground();
  }
  if (drawFlags & DRAW_VERTICAL_BARS) {
    drawVerticalBars();
  }
  if (drawFlags & DRAW_NOTES) {
    drawNotes();
  }
  if (drawFlags & DRAW_PIANOROLL) {
    drawPianoRoll();
  }
  drawFlags = 0;
  singleNoteDraw.clear();
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleKeyChanged(uint8_t key, midi::MidiType event) {
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
    singleNoteDraw.insert(key);
    drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawCursor(void) {
  static const uint16_t yTo = HEADER_HEIGHT - 1;
  static const uint16_t yFrom = yTo - CURSOR_HEIGHT;

  uint16_t cursorX = ROLL_WIDTH + ((data.currentPattern - view.startPattern) * data.patternLengthMs + data.patternTimer) * view.msToPix;
  const bool offLeft = (data.currentPattern < view.startPattern);
  const bool offRight = (data.currentPattern >= (view.startPattern + view.numPatterns));
  cursorX = offLeft ? ROLL_WIDTH : cursorX;
  cursorX = offRight ? DISPLAY_WIDTH - 1 : cursorX;
  const uint16_t cursorColor = (offLeft || offRight) ? RED : COLOR_SYSTEXT;

  display.console = true;
  display.drawLine(view.lastCursorPos, yFrom, view.lastCursorPos, yTo, CURSOR_BG_COLOR);
  display.console = true;
  display.drawLine(cursorX, yFrom, cursorX, yTo, cursorColor);
  view.lastCursorPos = cursorX;
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
  view.lowestNote = view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
}

FLASHMEM void UI_LiveSeq_PianoRoll::setStartOctave(int8_t num) {
  view.startOctave = num;
  view.lowestNote = view.startOctave * 12;
  view.highestNote = (view.lowestNote + view.numNotes);
}
FLASHMEM void UI_LiveSeq_PianoRoll::setNumPatterns(int8_t num) {
  view.numPatterns = num;
  view.msToPix = CONTENT_WIDTH / float(view.numPatterns * data.patternLengthMs);
  reloadNotes(view.startPattern, view.numPatterns);
}

FLASHMEM void UI_LiveSeq_PianoRoll::setPatternStart(int8_t num) {
  view.startPattern = num;
  DBG_LOG(printf("pattern from %i to %i\n", view.startPattern, view.startPattern + view.numPatterns));
  reloadNotes(view.startPattern, view.numPatterns);
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleEncoderRight(int8_t diff) {
  switch (mode) {
  case MODE_VIEW:
    switch (viewMode) {
    case VIEW_ZOOM_Y:
      if (hasConstrainedChanged(view.numOctaves, 2 * diff, 1, 5)) { // only change between 1, 3, 5 octaves
        setStartOctave(view.startOctave - diff); // zoom octave-centered
        setNumOctaves(view.numOctaves);
        drawFlags |= DRAW_ALL;
      }
      break;

    case VIEW_SCROLL_Y:
      if (hasConstrainedChanged(view.startOctave, diff, 0, 7)) { // octaves 0 (note 24) upto 7 (note 119)
        setStartOctave(view.startOctave);
        drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
      }
      break;

    case VIEW_ZOOM_X:
      if (hasConstrainedChanged(view.numPatterns, diff, 1, data.numberOfBars)) {
        setNumPatterns(view.numPatterns);
        drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
      }
      break;

    case VIEW_SCROLL_X:
      if (hasConstrainedChanged(view.startPattern, -diff, 0, data.numberOfBars - view.numPatterns)) {
        setPatternStart(view.startPattern);
        drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
      }
      break;
    }
    break;

  case MODE_EDIT:
    switch (editMode) {
    case EDIT_SELECT_NOTE:
      if (hasConstrainedChanged(selectedNote, -diff, 0, notePairs.size() - 1)) {
        int8_t prev = selectedNote + diff;
        if (prev < 0) {
          prev = 0; // make nicer
        }
        singleNoteDraw.insert(notePairs.at(prev).noteOn.note_in);
        singleNoteDraw.insert(notePairs.at(selectedNote).noteOn.note_in);
        drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
      }
      break;

    case EDIT_NOTEON:
    case EDIT_NOTEOFF:
      if (selectedNote > -1) {
        LiveSequencer::MidiEvent &e = (editMode == EDIT_NOTEON) ? notePairs.at(selectedNote).noteOn : notePairs.at(selectedNote).noteOff;
        if (hasConstrainedChanged(e.patternMs, -diff, 0, data.patternLengthMs)) {
          singleNoteDraw.insert(e.note_in);
          drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
          if(editMode == EDIT_NOTEON) {
            liveSeq.requestSortEvents(); // re-sort events as times changed
          }
        }
      }
      break;

    case EDIT_VELOCITY:
      if (selectedNote > -1) {
        LiveSequencer::MidiEvent &e = notePairs.at(selectedNote).noteOn;
        int8_t velo = notePairs.at(selectedNote).noteOn.note_in_velocity;
        if (hasConstrainedChanged(velo, -diff, 0, 127)) {
          e.note_in_velocity = velo;
          singleNoteDraw.insert(e.note_in);
          drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
        }
      }
      break;

    case EDIT_NOTE:
      if (selectedNote > -1) {
        LiveSequencer::NotePair &p = notePairs.at(selectedNote);
        int8_t note = p.noteOn.note_in;
        int8_t notePrev = note;
        if (hasConstrainedChanged(note, -diff, view.lowestNote, view.highestNote)) {
          singleNoteDraw.insert(note);
          p.noteOn.note_in = note;
          p.noteOff.note_in = note;
          singleNoteDraw.insert(notePrev);
          singleNoteDraw.insert(note);
          drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
        }
      }
      break;

    case EDIT_DELETE:
      // nothing to do on encoder turn
      break;
    }
    break;
  }
  if(drawFlags) {
    printActionValue();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::printActionName(void) {
  display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
  display.setTextSize(1);
  display.setCursor(100, 1);
  display.setTextColor(COLOR_SYSTEXT, HEADER_BG_COLOR);
  
  switch (mode) {
  case MODE_EDIT:
    switch (editMode) {
    case EDIT_SELECT_NOTE:
      display.print("SELECT NOTE: ");
      break;
    case EDIT_NOTEON:
      display.print("NOTE ON:     ");
      break;

    case EDIT_NOTEOFF:
      display.print("NOTE OFF:    ");
      break;

    case EDIT_VELOCITY:
      display.print("VELOCITY:    ");
      break;

    case EDIT_NOTE:
      display.print("NOTE:        ");
      break;

    case EDIT_DELETE:
      display.print("DELETE NOTE: ");
      break;
    }
    break;

  case MODE_VIEW:
    break;

  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::printActionValue(void) {
  display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
  display.setTextSize(1);
  display.setCursor(180, 1);
  display.setTextColor(COLOR_SYSTEXT, HEADER_BG_COLOR);
  
  switch (mode) {
  case MODE_EDIT:
    if(selectedNote > -1) {
      LiveSequencer::NotePair &p = notePairs.at(selectedNote);
      switch (editMode) {
      case EDIT_SELECT_NOTE:
        display.printf("[%i]     ", selectedNote);
        break;

      case EDIT_NOTEON:
        display.printf("%ims     ", p.noteOn.patternMs);
        break;

      case EDIT_NOTEOFF:
        display.printf("%ims     ", p.noteOff.patternMs);
        break;

      case EDIT_VELOCITY:
        display.printf("%i       ", p.noteOn.note_in_velocity);
        break;

      case EDIT_NOTE:
        display.printf("%i       ", p.noteOn.note_in);
        break;

      case EDIT_DELETE:
        display.print("CONFIRM");
        break;
      }
      break;
    }

  case MODE_VIEW:
    break;

  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawPianoRoll() {
  const uint16_t numWhiteKeys = view.numOctaves * 7;
  const float keyHeight = CONTENT_HEIGHT / float(numWhiteKeys);
  
  display.console = true;
  display.fillRect(0, HEADER_HEIGHT, ROLL_WIDTH, CONTENT_HEIGHT, COLOR_SYSTEXT);
  display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
  display.setTextSize(1);

  for (uint8_t i = 1; i < numWhiteKeys; i++) {
    const uint16_t y = round(HEADER_HEIGHT + i * keyHeight);
    display.console = true;
    display.drawLine(0, y, ROLL_WIDTH, y, COLOR_BACKGROUND);
  }

  for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
    const uint8_t noteIndex = view.highestNote - note - 1;
    const uint16_t y = HEADER_HEIGHT + ceil(noteIndex * view.noteHeight); // ceil looks best rounded
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

FLASHMEM void UI_LiveSeq_PianoRoll::drawBackground(void) {
  for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
    if (singleNoteDraw.empty() || (singleNoteDraw.find(note) != singleNoteDraw.end())) {
      const uint8_t noteIndex = view.highestNote - note - 1;
      const uint16_t y = HEADER_HEIGHT + round(noteIndex * view.noteHeight);
      const bool isPressed = keysPressed.find(note) != keysPressed.end();
      const uint16_t bgColor = isPressed ? GREY2 : (note & 0x01 ? GREY4 : COLOR_BACKGROUND);
      display.console = true;
      display.fillRect(ROLL_WIDTH, y, CONTENT_WIDTH, view.noteHeight, bgColor);
    }
  }
}  

FLASHMEM void UI_LiveSeq_PianoRoll::drawVerticalBars(void) {
  // draw vertical pattern lines
  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numPatterns * substeps);
  for (uint8_t step = 0; step < view.numPatterns * substeps; step++) {
    const uint16_t x = ROLL_WIDTH + step * stepWidth;
    const uint16_t lineColor = (step % substeps == 0) ? GREY2 : GREY3;
    display.console = true;
    if (singleNoteDraw.empty()) {
      display.drawLine(x, HEADER_HEIGHT, x, HEADER_HEIGHT + CONTENT_HEIGHT - 1, lineColor);
    }
    else {
      for(uint8_t note : singleNoteDraw) {
        if (note >= view.lowestNote && note < view.highestNote) {
          // only draw lines where single note update is needed
          const uint16_t y = HEADER_HEIGHT + CONTENT_HEIGHT - (note + 1 - view.startOctave * 12) * view.noteHeight;
          display.drawLine(x, y, x, y + view.noteHeight - 1, lineColor);
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
    display.setCursor((DISPLAY_WIDTH - stringWidth) / 2, DISPLAY_HEIGHT / 2);
    display.setTextColor(COLOR_SYSTEXT);
    display.setTextSize(0);
    display.print(text.c_str());
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note, bool isSelected) {
  if (singleNoteDraw.empty() || singleNoteDraw.find(note.noteOn.note_in) != singleNoteDraw.end()) {
    const uint16_t x = ROLL_WIDTH + (round(midiEventTime(note.noteOn) - view.startPattern * data.patternLengthMs) * view.msToPix);
    const uint16_t y = HEADER_HEIGHT + CONTENT_HEIGHT - (note.noteOn.note_in + 1 - view.startOctave * 12) * view.noteHeight;
    const uint16_t h = view.noteHeight;
    const uint16_t w = round((midiEventTime(note.noteOff) - midiEventTime(note.noteOn)) * view.msToPix);
    //DBG_LOG(printf("draw note %i\n", note.noteOn.note_in));
    //DBG_LOG(printf("x: %i, y: %i, w: %i, h: %i\n", x, y, w, h));
    const uint16_t color = isSelected ? COLOR_SYSTEXT : layerColors.at(note.noteOn.layer);
    display.console = true;
    if (note.isMuted) {
      display.drawRect(x, y, w, h, color);
    } else {
      display.fillRect(x, y, w, h, color);
    }
  }
}

FLASHMEM uint32_t UI_LiveSeq_PianoRoll::midiEventTime(LiveSequencer::MidiEvent e) const {
  return e.patternNumber * data.patternLengthMs + e.patternMs;
}

FLASHMEM std::string UI_LiveSeq_PianoRoll::getViewModeString(void) {
  switch (viewMode) {
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

FLASHMEM std::string UI_LiveSeq_PianoRoll::getEditModeString(void) {
  switch (editMode) {
  case EDIT_SELECT_NOTE:
    return "SELECT";

  case EDIT_NOTEON:
    return "NOTEON";

  case EDIT_VELOCITY:
    return "VELOCTY";

  case EDIT_NOTEOFF:
    return "NOTEOFF";

  case EDIT_NOTE:
    return "NOTE";

  case EDIT_DELETE:
    return "DELETE";
  }
  return "NONE";
}

FLASHMEM void UI_LiveSeq_PianoRoll::initGUI(void) {
  const uint8_t BUTTON_SPACING = 4;  // center in screen
  const uint16_t y = HEADER_HEIGHT + CONTENT_HEIGHT;

  uint16_t GRID_X[LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN];
  for (uint8_t i = 0; i < LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN; i++) {
    GRID_X[i] = i * (TouchButton::BUTTON_SIZE_X + BUTTON_SPACING);
  }

  buttons.push_back(new ValueButtonRange<uint8_t>(&currentValue, GRID_X[0], y, data.activeTrack, 0, LiveSequencer::LIVESEQUENCER_NUM_TRACKS - 1, 1, data.activeTrack, 
  [ this ] (auto *b, auto *v) { // drawHandler
    char temp_char[2];
    itoa(data.activeTrack + 1, temp_char, 10);
    b->draw("TRACK", temp_char, TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    ui_pianoroll->reloadNotes(view.startPattern, view.numPatterns);
    drawFlags |= DRAW_BACKGROUND | DRAW_NOTES | DRAW_VERTICAL_BARS;
  }));

  buttonView = new TouchButton(GRID_X[1], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("VIEW", ui_pianoroll->getViewModeString(), (mode == MODE_VIEW) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    const bool modeChanged = mode != MODE_VIEW;
    mode = MODE_VIEW;
    if (modeChanged) {
      if(selectedNote > -1) { // de-select note from edit
        //singleNoteDraw.insert(selectedNote); // fixme!
        //DBG_LOG(printf("should update %i\n", selectedNote));
        selectedNote = -1;
        drawFlags |= DRAW_BACKGROUND | DRAW_NOTES | DRAW_VERTICAL_BARS;
      }
      editMode = EDIT_SELECT_NOTE;
      buttonEdit->drawNow();
      ui_pianoroll->printActionName();
      ui_pianoroll->printActionValue();
    }
    b->drawNow();
  });
  buttons.push_back(buttonView);

  buttonEdit = new TouchButton(GRID_X[2], y,
  [ this ] (auto *b) { // drawHandler
    TouchButton::Color color = (mode == MODE_EDIT) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE;
    if((mode == MODE_EDIT) && (editMode == EDIT_DELETE)) {
      color = TouchButton::BUTTON_RED;
    }
    b->draw("EDIT", ui_pianoroll->getEditModeString(), color);
  }, [ this ] (auto *b) { // clickedHandler
    const bool modeChanged = mode != MODE_EDIT;
    mode = MODE_EDIT;
    if (modeChanged) {
      buttonView->drawNow();
      ui_pianoroll->printActionName();
      ui_pianoroll->printActionValue();
    }
    if((mode == MODE_EDIT) && (editMode == EDIT_DELETE)) {
      // delete (invalidate) this note pair
      if(selectedNote > -1) {
        notePairs.at(selectedNote).noteOn.event = midi::InvalidType;
        notePairs.at(selectedNote).noteOff.event = midi::InvalidType;
        ui_pianoroll->reloadNotes(view.startPattern, view.numPatterns);
        drawFlags |= DRAW_BACKGROUND | DRAW_NOTES | DRAW_VERTICAL_BARS;
        editMode = EDIT_SELECT_NOTE;
      }
    }
    b->drawNow();
  });
  buttons.push_back(buttonEdit);

  buttons.push_back(new TouchButton(GRID_X[3], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("YOU", (data.arpSettings.enabled == 0) ? "OFF" : "ON", TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));
  buttons.push_back(new TouchButton(GRID_X[4], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("DOO", (data.arpSettings.enabled == 0) ? "OFF" : "ON", TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));
  buttons.push_back(new TouchButton(GRID_X[5], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("SOO", (data.arpSettings.enabled == 0) ? "OFF" : "ON", TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));
}