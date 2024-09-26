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
  for(TouchButton *b : buttons) {
    b->processPressed();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::processLCDM(uint8_t param) {
  std::vector<LiveSequencer::NotePair> notePairs = liveSeq.getNotePairsFromTrack(param);
  if (LCDML.FUNC_setup()) // ****** SETUP *********
  {
    currentTrack = param;
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

    drawFlags |= DRAW_ALL;

    for (auto *b : buttons) {
      b->drawNow();
    }
    drawCursor();
  }
  if (LCDML.FUNC_loop()) {
    if (LCDML.BT_checkDown()) {
      handleEncoderRight(-1);
    }
    if (LCDML.BT_checkUp()) {
      handleEncoderRight(+1);
    }
    if (LCDML.BT_checkEnter()) {
      if(++editMode == VIEW_NUM) {
        editMode = VIEW_ZOOM_X;
      }
    }

    if(isRunningHere != data.isRunning) {
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
  if(drawFlags & DRAW_CURSOR) {
    drawCursor();
  }
  if(drawFlags & DRAW_HORIZONTAL_BARS) {
    drawHorizontalBars();
  }
  if(drawFlags & DRAW_VERTICAL_BARS) {
    drawVerticalBars();
  }
  if(drawFlags & DRAW_NOTES) {
    drawNotes();
  }
  if(drawFlags & DRAW_CURSOR) {
    drawCursor();
  }
  if(drawFlags & DRAW_PIANOROLL) {
    drawPianoRoll();
  }
  drawFlags = 0;
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleKeyChanged(uint8_t key, midi::MidiType event) {
  if(isVisible) {
    switch(event) {
    case midi::NoteOn:
      keysPressed.insert(key);
      break;

    case midi::NoteOff:
      keysPressed.erase(key);
      break;

    default:
      break;
    }
    drawFlags |= DRAW_HORIZONTAL_BARS | DRAW_VERTICAL_BARS | DRAW_NOTES;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawCursor(void) {
  const uint16_t cursorX = ROLL_WIDTH + (data.currentPattern * data.patternLengthMs + data.patternTimer) * view.msToPix;
  display.console = true;
  display.drawLine(view.lastCursorPos, HEADER_HEIGHT - 2, view.lastCursorPos, HEADER_HEIGHT, HEADER_BG_COLOR);
  display.console = true;
  display.drawLine(cursorX, HEADER_HEIGHT - 2, cursorX, HEADER_HEIGHT, RED);
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
}
FLASHMEM void UI_LiveSeq_PianoRoll::setNumPatterns(int8_t num) {
  view.numPatterns = num;
  view.msToPix = CONTENT_WIDTH / float(view.numPatterns * data.patternLengthMs);
}
FLASHMEM void UI_LiveSeq_PianoRoll::setPatternStart(int8_t num) {
  view.startPattern = num;
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleEncoderRight(int8_t diff) {
  bool drawY = false;
  bool drawX = false;
  switch(editMode) {
  case VIEW_ZOOM_Y:
    drawY = hasConstrainedChanged(view.numOctaves, 2 * diff, 1, 5); // only change between 1, 3, 5 octaves
    setNumOctaves(view.numOctaves);
    drawX = drawY;
    break;

  case VIEW_SCROLL_Y:
    drawY = hasConstrainedChanged(view.startOctave, diff, 0, 6);
    setStartOctave(view.startOctave);
    drawX = drawY;
    break;

  case VIEW_ZOOM_X:
    drawX = hasConstrainedChanged(view.numPatterns, diff, 1, data.numberOfBars);
    setNumPatterns(view.numPatterns);
    break;
  }

  if(drawX || drawY) {
    drawFlags |= DRAW_HORIZONTAL_BARS | DRAW_NOTES;
  }
  if(drawY) {
    drawFlags |= DRAW_VERTICAL_BARS | DRAW_PIANOROLL;
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawPianoRoll() {
  const uint16_t numWhiteKeys = view.numOctaves * 7;
  const float keyHeight = CONTENT_HEIGHT / float(numWhiteKeys);
  
  display.console = true;
  display.fillRect(0, HEADER_HEIGHT, ROLL_WIDTH, CONTENT_HEIGHT, COLOR_SYSTEXT);
  display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
  display.setTextSize(1);

  for(uint8_t i = 1; i <= numWhiteKeys; i++) {
    const uint16_t y = round(HEADER_HEIGHT + i * keyHeight);
    display.console = true;
    display.drawLine(0, y, ROLL_WIDTH, y, COLOR_BACKGROUND);
  }

  for(uint8_t note = view.lowestNote; note < view.highestNote; note++) {
    const uint8_t noteIndex = view.highestNote - note - 1;
    const uint16_t y = HEADER_HEIGHT + round(noteIndex * view.noteHeight);
    switch(note % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
      display.console = true;
      display.fillRect(0, y, 10, 0.8 * keyHeight, COLOR_BACKGROUND);
      break;
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawHorizontalBars(void) {
  for(uint8_t note = view.lowestNote; note < view.highestNote; note++) {
    const uint8_t noteIndex = view.highestNote - note - 1;
    const uint16_t y = HEADER_HEIGHT + round(noteIndex * view.noteHeight);
    const bool isPressed = keysPressed.find(note) != keysPressed.end();
    const uint16_t bgColor = isPressed ? GREY2 : (note & 0x01 ? GREY4 : COLOR_BACKGROUND);
    display.console = true;
    display.fillRect(ROLL_WIDTH, y, DISPLAY_WIDTH - ROLL_WIDTH, view.noteHeight, bgColor);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawVerticalBars(void) {
  // draw verticl pattern lines
  static constexpr uint8_t substeps = 4;
  const float stepWidth = CONTENT_WIDTH / float(view.numPatterns * substeps);
  for(uint8_t step = 0; step < view.numPatterns * substeps; step++) {
    const uint16_t x = ROLL_WIDTH + step * stepWidth;
    const uint16_t lineColor = (step % substeps == 0) ? GREY2 : GREY3;
    display.console = true;
    display.drawLine(x, HEADER_HEIGHT, x, HEADER_HEIGHT + CONTENT_HEIGHT - 1, lineColor);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNotes(void) {
  std::vector<LiveSequencer::NotePair> notePairs = liveSeq.getNotePairsFromTrack(currentTrack);
  int drawNotes = 0;
  int hideNotes = 0;

  if(notePairs.size()) {
    for (LiveSequencer::NotePair note : notePairs) {
      const bool isVisibleY = note.noteOn.note_in >= view.lowestNote &&
                              note.noteOn.note_in < view.highestNote;
      const bool isVisibleX = note.noteOn.patternNumber >= view.startPattern &&
                              note.noteOn.patternNumber <= (view.startPattern + view.numPatterns);
      if(isVisibleX && isVisibleY) {
        drawNote(note);
        drawNotes++;
      }
      else {
        hideNotes++;
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
  DBG_LOG(printf("draw notes: %i, hide nodes: %i\n", drawNotes, hideNotes));
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note) {
  const uint16_t x = ROLL_WIDTH + round(midiEventTime(note.noteOn) * view.msToPix);
  const uint16_t y = HEADER_HEIGHT + CONTENT_HEIGHT - (note.noteOn.note_in + 1 - view.startOctave * 12) * view.noteHeight;
  const uint16_t h = view.noteHeight;
  const uint16_t w = round((midiEventTime(note.noteOff) - midiEventTime(note.noteOn)) * view.msToPix);
  //DBG_LOG(printf("x: %i, y: %i, w: %i, h: %i\n", x, y, w, h));
  display.console = true;
  if(note.isMuted) {
    display.drawRect(x, y, w, h, layerColors.at(note.noteOn.layer));
  } else {
    display.fillRect(x, y, w, h, layerColors.at(note.noteOn.layer));
  }
}

FLASHMEM uint32_t UI_LiveSeq_PianoRoll::midiEventTime(LiveSequencer::MidiEvent e) const {
  return e.patternNumber * data.patternLengthMs + e.patternMs;
}

FLASHMEM void UI_LiveSeq_PianoRoll::initGUI(void) {
  const uint8_t BUTTON_SPACING = 4;  // center in screen
  uint16_t GRID_X[LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN];
  for (uint8_t i = 0; i < LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN; i++) {
    GRID_X[i] = i * (TouchButton::BUTTON_SIZE_X + BUTTON_SPACING);
  }

  buttons.push_back(new ValueButtonRange<uint8_t>(&currentValue, GRID_X[0], HEADER_HEIGHT + CONTENT_HEIGHT, currentTrack, 0, LiveSequencer::LIVESEQUENCER_NUM_TRACKS - 1, 1, currentTrack, 
  [ this ] (auto *b, auto *v) { // drawHandler
    char temp_char[2];
    itoa(currentTrack + 1, temp_char, 10);
    b->draw("TRACK", temp_char, TouchButton::BUTTON_ACTIVE);
  }, [ this ] (auto *b) { // clickedHandler
    drawFlags |= DRAW_NOTES;
  }));

  buttons.push_back(new TouchButton(GRID_X[1], HEADER_HEIGHT + CONTENT_HEIGHT,
  [ this ] (auto *b) { // drawHandler
    b->draw("BAR", (data.arpSettings.enabled == 0) ? "OFF" : "ON", (data.arpSettings.enabled == 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_HIGHLIGHTED);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));  
  buttons.push_back(new TouchButton(GRID_X[2], HEADER_HEIGHT + CONTENT_HEIGHT,
  [ this ] (auto *b) { // drawHandler
    b->draw("BAZ", (data.arpSettings.enabled == 0) ? "OFF" : "ON", (data.arpSettings.enabled == 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_HIGHLIGHTED);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));
  buttons.push_back(new TouchButton(GRID_X[3], HEADER_HEIGHT + CONTENT_HEIGHT,
  [ this ] (auto *b) { // drawHandler
    b->draw("YOU", (data.arpSettings.enabled == 0) ? "OFF" : "ON", (data.arpSettings.enabled == 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_HIGHLIGHTED);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));
  buttons.push_back(new TouchButton(GRID_X[4], HEADER_HEIGHT + CONTENT_HEIGHT,
  [ this ] (auto *b) { // drawHandler
    b->draw("DOO", (data.arpSettings.enabled == 0) ? "OFF" : "ON", (data.arpSettings.enabled == 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_HIGHLIGHTED);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));
  buttons.push_back(new TouchButton(GRID_X[5], HEADER_HEIGHT + CONTENT_HEIGHT,
  [ this ] (auto *b) { // drawHandler
    b->draw("SOO", (data.arpSettings.enabled == 0) ? "OFF" : "ON", (data.arpSettings.enabled == 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_HIGHLIGHTED);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
  }));
}