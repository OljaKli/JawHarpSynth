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
  std::vector<LiveSequencer::NotePair> notePairs = liveSeq.getNotePairsFromTrack(param);
  if (LCDML.FUNC_setup()) // ****** SETUP *********
  {
    data.activeTrack = param;
    isVisible = true;
    registerTouchHandler(handle_touchscreen_pianoroll);

    setNumOctaves(5);
    setStartOctave(3);
    setNumPatterns(4);
    setPatternStart(0);
   
    // setup function
    display.fillScreen(COLOR_BACKGROUND);
    display.fillRect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, HEADER_BG_COLOR);
    display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
    display.setTextSize(1);
    display.setCursor(0, 1);
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.print("LIVESEQ PIANOROLL");
    LCDML.FUNC_setLoopInterval(40); // 25Hz gui refresh

    static constexpr uint16_t layerViewSize = (HEADER_HEIGHT - CURSOR_HEIGHT) / 2;
    static constexpr uint16_t layerViewX = DISPLAY_WIDTH - 2 * layerViewSize;
    static constexpr uint16_t layerViewY = 0;
    for (uint8_t l = 0; l < 4; l++) {
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
        break;

      case MODE_EDIT:
        if (++editMode == EDIT_NUM) {
          editMode = EDIT_SELECT_NOTE;
        }
        buttonEdit->drawNow();
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
  if (drawFlags & DRAW_CURSOR) {
    drawCursor();
  }
  if (drawFlags & DRAW_PIANOROLL) {
    drawPianoRoll();
  }
  drawFlags = 0;
  singleNoteDraw = -1;
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
    singleNoteDraw = key;
    drawFlags |= DRAW_BACKGROUND | DRAW_VERTICAL_BARS | DRAW_NOTES;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawCursor(void) {
  const uint16_t cursorX = ROLL_WIDTH + ((data.currentPattern - view.startPattern) * data.patternLengthMs + data.patternTimer) * view.msToPix;
  static const uint16_t yTo = HEADER_HEIGHT - 1;
  static const uint16_t yFrom = yTo - CURSOR_HEIGHT;
  display.console = true;
  display.drawLine(view.lastCursorPos, yFrom, view.lastCursorPos, yTo, HEADER_BG_COLOR);
  display.console = true;
  display.drawLine(cursorX, yFrom, cursorX, yTo, RED);
  view.lastCursorPos = cursorX;
}

FLASHMEM bool UI_LiveSeq_PianoRoll::hasConstrainedChanged(int8_t &value, int8_t diff, int8_t min, int8_t max) {
  const int16_t newValue = constrain(value + diff, min, max);
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
}
FLASHMEM void UI_LiveSeq_PianoRoll::setPatternStart(int8_t num) {
  view.startPattern = num;
  DBG_LOG(printf("pattern from %i to %i\n", view.startPattern, view.startPattern + view.numPatterns));
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
    // TODO
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
    const uint16_t y = HEADER_HEIGHT + round(noteIndex * view.noteHeight);
    switch (note % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
      display.console = true;
      display.fillRect(0, y, 14, 0.8 * keyHeight, COLOR_BACKGROUND);
      break;
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawBackground(void) {
  for (uint8_t note = view.lowestNote; note < view.highestNote; note++) {
    if ((singleNoteDraw == -1) || (singleNoteDraw == note)) {
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
    if (singleNoteDraw == -1) {
      display.drawLine(x, HEADER_HEIGHT, x, HEADER_HEIGHT + CONTENT_HEIGHT - 1, lineColor);
    }
    else if (singleNoteDraw >= view.lowestNote && singleNoteDraw < view.highestNote) {
      // only draw lines where single note update is needed
      const uint16_t y = HEADER_HEIGHT + CONTENT_HEIGHT - (singleNoteDraw + 1 - view.startOctave * 12) * view.noteHeight;
      display.drawLine(x, y, x, y + view.noteHeight - 1, lineColor);
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNotes(void) {
  std::vector<LiveSequencer::NotePair> notePairs = liveSeq.getNotePairsFromTrack(data.activeTrack);

  if (notePairs.size()) {
    for (LiveSequencer::NotePair note : notePairs) {
      const bool isVisibleY = note.noteOn.note_in >= view.lowestNote &&
                              note.noteOn.note_in < view.highestNote;
      const bool isVisibleX = note.noteOn.patternNumber >= view.startPattern &&
                              note.noteOn.patternNumber <= (view.startPattern + view.numPatterns);
      if (isVisibleX && isVisibleY) {
        drawNote(note);
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

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note) {
  if ((singleNoteDraw == -1) || (singleNoteDraw == note.noteOn.note_in)) {
    const uint16_t x = ROLL_WIDTH + (round(midiEventTime(note.noteOn) - view.startPattern * data.patternLengthMs) * view.msToPix);
    const uint16_t y = HEADER_HEIGHT + CONTENT_HEIGHT - (note.noteOn.note_in + 1 - view.startOctave * 12) * view.noteHeight;
    const uint16_t h = view.noteHeight;
    const uint16_t w = round((midiEventTime(note.noteOff) - midiEventTime(note.noteOn)) * view.msToPix);
    //DBG_LOG(printf("x: %i, y: %i, w: %i, h: %i\n", x, y, w, h));
    display.console = true;
    if (note.isMuted) {
      display.drawRect(x, y, w, h, layerColors.at(note.noteOn.layer));
    } else {
      display.fillRect(x, y, w, h, layerColors.at(note.noteOn.layer));
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
    drawFlags |= DRAW_BACKGROUND | DRAW_NOTES | DRAW_VERTICAL_BARS;
  }));

  buttonView = new TouchButton(GRID_X[1], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("VIEW", ui_pianoroll->getViewModeString(), (mode == MODE_VIEW) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    const bool modeChanged = mode != MODE_VIEW;
    mode = MODE_VIEW;
    if (modeChanged) {
      buttonEdit->drawNow();
    }
    b->drawNow();
  });
  buttons.push_back(buttonView);

  buttonEdit = new TouchButton(GRID_X[2], y,
  [ this ] (auto *b) { // drawHandler
    b->draw("EDIT", ui_pianoroll->getEditModeString(), (mode == MODE_EDIT) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    const bool modeChanged = mode != MODE_EDIT;
    mode = MODE_EDIT;
    if (modeChanged) {
      buttonView->drawNow();
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