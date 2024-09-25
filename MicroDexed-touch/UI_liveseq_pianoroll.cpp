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
    
    registerTouchHandler(handle_touchscreen_pianoroll);
   
    // setup function
    display.fillScreen(COLOR_BACKGROUND);
    display.fillRect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, GREY3);
    display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
    display.setTextSize(1);
    display.setCursor(0, 1);
    display.setTextColor(RED, COLOR_BACKGROUND);
    display.print("LIVESEQ PIANOROLL");
    display.setTextColor(GREY1, COLOR_BACKGROUND);
    display.setCursor(CHAR_width_small * 29, 0);
    display.print(F("EVENT"));
    display.setCursor(CHAR_width_small * 39, 0);
    display.print(F("/"));
    display.setCursor(CHAR_width_small * 46, 0);
    display.print(F("TRACK"));
    LCDML.FUNC_setLoopInterval(40); // 25Hz gui refresh

    drawPianoRoll();
    drawContentArea();

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
      if(++editMode == VIEW_NUM) {
        editMode = VIEW_ZOOM_X;
      }
    }

    if(isRunningHere != data.isRunning) {
      isRunningHere = data.isRunning;
      if(isRunningHere) {
        drawCursor();
      }
    }
  }
  if (LCDML.FUNC_close()) {
    unregisterTouchHandler();
    display.fillScreen(COLOR_BACKGROUND);
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawCursor(void) {

}

FLASHMEM bool UI_LiveSeq_PianoRoll::hasConstrainedChanged(int8_t &value, int8_t diff, int8_t min, int8_t max) {
  const int16_t newValue = constrain(value + diff, min, max);
  const bool changed = newValue != value;
  value = newValue;
  return changed;
}

FLASHMEM void UI_LiveSeq_PianoRoll::handleEncoderRight(int8_t diff) {
  bool drawY = false;
  bool drawX = false;
  switch(editMode) {
  case VIEW_ZOOM_Y:
    drawY = hasConstrainedChanged(viewSettings.numOctaves, 2 * diff, 1, 5); // only change between octave numbers
    drawX = drawY;
    break;

  case VIEW_SCROLL_Y:
    drawY = hasConstrainedChanged(viewSettings.startOctave, diff, 0, 6);
    drawX = drawY;
    break;

  case VIEW_ZOOM_X:
    drawX = hasConstrainedChanged(viewSettings.numPatterns, diff, 1, data.numberOfBars);
    break;
  }

  if(drawX || drawY) {
    drawContentArea();
  }
  if(drawY) {
    drawPianoRoll();
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawPianoRoll() {
  const uint16_t numNotes = viewSettings.numOctaves * 12;
  const uint16_t numWhiteKeys = viewSettings.numOctaves * 7;
  const float keyHeight = CONTENT_HEIGHT / float(numWhiteKeys);
  
  display.fillRect(0, HEADER_HEIGHT, ROLL_WIDTH, CONTENT_HEIGHT, COLOR_SYSTEXT);
  display.setTextColor(COLOR_BACKGROUND, COLOR_SYSTEXT);
  display.setTextSize(1);

  for(uint8_t i = 1; i <= numWhiteKeys; i++) {
    const uint16_t y = round(HEADER_HEIGHT + i * keyHeight);
    display.drawLine(0, y, ROLL_WIDTH, y, COLOR_BACKGROUND);
    /*if(i % 7 == 0) {
      display.setCursor(10, y - keyHeight + 2);
      display.print("C");
      display.print(octaveStart + i / 12);
    }*/
  }

  const float noteHeight = CONTENT_HEIGHT / float(numNotes);
  for(uint8_t i = 0; i < numNotes; i++) {
    const uint16_t y = HEADER_HEIGHT + round(i * noteHeight);
    switch(i % 12) {
    case 1:
    case 3:
    case 5:
    case 8:
    case 10:
      display.fillRect(0, y, 10, 0.8 * keyHeight, COLOR_BACKGROUND);
      break;
    }
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawContentArea(void) {
  const uint16_t numNotes = viewSettings.numOctaves * 12;
  const float noteHeight = CONTENT_HEIGHT / float(numNotes);
  for(uint8_t i = 0; i < numNotes; i++) {
    const uint16_t y = HEADER_HEIGHT + round(i * noteHeight);
    display.fillRect(ROLL_WIDTH, y, DISPLAY_WIDTH - ROLL_WIDTH, noteHeight, i & 0x01 ? GREY4 : COLOR_BACKGROUND);
  }
  const float patternWidth = CONTENT_WIDTH / float(viewSettings.numPatterns);
  for(uint8_t i = 0; i < viewSettings.numPatterns; i++) {
    const uint16_t x = ROLL_WIDTH + i * patternWidth;
    display.drawLine(x, HEADER_HEIGHT, x, HEADER_HEIGHT + CONTENT_HEIGHT, GREY2);
  }

  // draw notes...
  std::vector<LiveSequencer::NotePair> notePairs = liveSeq.getNotePairsFromTrack(currentTrack);
  int drawNotes = 0;
  int hideNotes = 0;

  for (LiveSequencer::NotePair note : notePairs) {
    const bool isVisibleY = note.noteOn.note_in >= viewSettings.startOctave * 12 &&
      note.noteOn.note_in <= (viewSettings.startOctave + viewSettings.numOctaves) * 12;
    const bool isVisibleX = note.noteOn.patternNumber >= viewSettings.startPattern &&
      note.noteOn.patternNumber <= (viewSettings.startPattern + viewSettings.numPatterns);
    if(isVisibleX && isVisibleY) {
      drawNote(note);
      drawNotes++;
    }
    else {
      hideNotes++;
    }
    DBG_LOG(printf("draw notes: %i, hide nodes: %i\n", drawNotes, hideNotes));
  }
}

FLASHMEM void UI_LiveSeq_PianoRoll::drawNote(LiveSequencer::NotePair note) {
  const uint16_t numNotes = viewSettings.numOctaves * 12;
  const float noteHeight = CONTENT_HEIGHT / float(numNotes);
  const float contentLengthMs = (viewSettings.numPatterns * data.patternLengthMs);
  const float msToPixels = CONTENT_WIDTH / float(contentLengthMs);

  const uint16_t x = ROLL_WIDTH + midiEventTime(note.noteOn) * msToPixels;
  const uint16_t y = HEADER_HEIGHT + (note.noteOn.note_in - viewSettings.startOctave * 12) * noteHeight;
  const uint16_t h = noteHeight;
  const uint16_t w = (midiEventTime(note.noteOff) - midiEventTime(note.noteOn)) * msToPixels;

  display.fillRect(x, y, w, h, layerColors.at(note.noteOn.layer));
}

FLASHMEM uint32_t UI_LiveSeq_PianoRoll::midiEventTime(LiveSequencer::MidiEvent e) {
  return e.patternNumber * data.patternLengthMs + e.patternMs;
}

FLASHMEM void UI_LiveSeq_PianoRoll::initGUI(void) {
  const uint8_t BUTTON_SPACING = 4;  // center in screen
  uint16_t GRID_X[LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN];
  for (uint8_t i = 0; i < LiveSequencer::LIVESEQUENCER_TRACKS_PER_SCREEN; i++) {
    GRID_X[i] = i * (TouchButton::BUTTON_SIZE_X + BUTTON_SPACING);
  }

  buttons.push_back(new TouchButton(GRID_X[0], HEADER_HEIGHT + CONTENT_HEIGHT,
  [ this ] (auto *b) { // drawHandler
    b->draw("FOO", (data.arpSettings.enabled == 0) ? "OFF" : "ON", (data.arpSettings.enabled == 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_HIGHLIGHTED);
  }, [ this ] (auto *b) { // clickedHandler
    liveSeq.setArpEnabled(!data.arpSettings.enabled);
    b->drawNow();
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