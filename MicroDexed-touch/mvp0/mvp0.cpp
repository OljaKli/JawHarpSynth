#ifdef JAW_HARP_MVP0

#include "mvp0.h"

#include <Arduino.h>
#include <Audio.h>
#include <Encoder.h>
#include <string.h>

#include "abc_player.h"
#include "embedded_presets.h"
#include "config.h"
#include "synth_dexed.h"

extern AudioSynthDexed* MicroDexed[NUM_DEXED];
extern config_t configuration;
extern char g_voice_name[NUM_DEXED][VOICE_NAME_LEN];
extern char g_bank_name[NUM_DEXED][BANK_NAME_LEN];
extern void midiNoteOn(uint8_t note, uint8_t velocity, uint8_t channel, uint8_t device);
extern void midiNoteOff(uint8_t note, uint8_t velocity, uint8_t channel, uint8_t device);

namespace jaw_harp_mvp0 {
namespace {

constexpr uint8_t kEncoderA = 2;
constexpr uint8_t kEncoderB = 3;
constexpr uint8_t kEncoderButton = 4;
constexpr uint8_t kMidiChannel = 1;
constexpr uint8_t kVelocity = 100;
constexpr uint16_t kDebounceMs = 20;
constexpr uint16_t kLongPressMs = 500;
constexpr uint16_t kStartupDelayMs = 500;
constexpr int kEncoderCountsPerDetent = 4;

constexpr char kTestPhrase[] =
    "X:1\n"
    "T:Jaw Harp MVP0\n"
    "M:4/4\n"
    "L:1/4\n"
    "Q:120\n"
    "K:C\n"
    "V:1\n"
    "C D E F | [EG]2 z2 |\n"
    "V:2\n"
    "C,2 G,2 | z4 |\n";

Encoder encoder(kEncoderA, kEncoderB);
Score score;
Player player;
uint8_t note_references[128] = {};
uint8_t preset_index = 0;
int encoder_detent = 0;
uint32_t boot_ms = 0;
uint32_t button_changed_ms = 0;
uint32_t button_pressed_ms = 0;
bool button_raw = true;
bool button_stable = true;
bool long_press_fired = false;
bool ready = false;
bool auto_started = false;
Player::State logged_state = Player::State::stopped;

void allNotesOff() {
  if (!MicroDexed[0]) return;
  for (uint16_t note = 0; note < 128; ++note) {
    if (!note_references[note]) continue;
    MicroDexed[0]->keyup(static_cast<uint8_t>(note));
    midiNoteOff(static_cast<uint8_t>(note), 0, kMidiChannel, 2);
    note_references[note] = 0;
  }
}

void routeEvent(const Event& event, void*) {
  if (!MicroDexed[0]) return;
  uint8_t& references = note_references[event.note];
  if (event.note_on) {
    if (references++ == 0) {
      MicroDexed[0]->keydown(event.note, kVelocity);
      midiNoteOn(event.note, kVelocity, kMidiChannel, 2);
    }
  } else if (references && --references == 0) {
    MicroDexed[0]->keyup(event.note);
    midiNoteOff(event.note, 0, kMidiChannel, 2);
  }
}

void allNotesOffCallback(void*) {
  allNotesOff();
}

void loadPreset(uint8_t index) {
  if (!MicroDexed[0]) return;
  allNotesOff();

  uint8_t packed[128];
  uint8_t decoded[156];
  memcpy(packed, kPresets[index].packed_voice, sizeof(packed));
  if (!MicroDexed[0]->decodeVoice(decoded, packed)) {
    Serial.println("MVP0: preset decode failed");
    return;
  }

  AudioNoInterrupts();
  MicroDexed[0]->loadVoiceParameters(decoded);
  MicroDexed[0]->setTranspose(configuration.dexed[0].transpose);
  AudioInterrupts();

  memset(g_voice_name[0], 0, VOICE_NAME_LEN);
  strncpy(g_voice_name[0], kPresets[index].name, VOICE_NAME_LEN - 1);
  memset(g_bank_name[0], 0, BANK_NAME_LEN);
  strncpy(g_bank_name[0], "MVP0 EMBED", BANK_NAME_LEN - 1);
  Serial.printf("MVP0: preset %u/%u %s\n", index + 1, kPresetCount, kPresets[index].name);
}

void restart(uint32_t now_ms) {
  player.restart(now_ms);
  auto_started = true;
  Serial.println("MVP0: restarted");
}

void shortPress(uint32_t now_ms) {
  if (player.state() == Player::State::playing) {
    player.pause(now_ms);
    Serial.println("MVP0: paused");
  } else if (player.state() == Player::State::paused) {
    player.resume(now_ms);
    Serial.println("MVP0: resumed");
  }
}

void updateButton(uint32_t now_ms) {
  const bool raw = digitalRead(kEncoderButton) != LOW;
  if (raw != button_raw) {
    button_raw = raw;
    button_changed_ms = now_ms;
  }
  if (raw != button_stable && now_ms - button_changed_ms >= kDebounceMs) {
    button_stable = raw;
    if (!button_stable) {
      button_pressed_ms = now_ms;
      long_press_fired = false;
    } else if (!long_press_fired) {
      shortPress(now_ms);
    }
  }
  if (!button_stable && !long_press_fired && now_ms - button_pressed_ms >= kLongPressMs) {
    long_press_fired = true;
    restart(now_ms);
  }
}

void updateEncoder() {
  const int detent = static_cast<int>(encoder.read() / kEncoderCountsPerDetent);
  if (detent == encoder_detent) return;
  const int delta = detent - encoder_detent;
  encoder_detent = detent;
  int next = static_cast<int>(preset_index) + delta;
  while (next < 0) next += kPresetCount;
  preset_index = static_cast<uint8_t>(next % kPresetCount);
  loadPreset(preset_index);
}

}  // namespace
}  // namespace jaw_harp_mvp0

void jawHarpMvp0Setup() {
  using namespace jaw_harp_mvp0;
  pinMode(kEncoderButton, INPUT_PULLUP);
  encoder.write(0);
  encoder_detent = 0;
  button_raw = button_stable = digitalRead(kEncoderButton) != LOW;
  button_changed_ms = millis();

  const char* error = nullptr;
  if (!parseAbc(kTestPhrase, score, &error)) {
    Serial.printf("MVP0: ABC error: %s\n", error ? error : "unknown");
    return;
  }

  player.configure(&score, routeEvent, allNotesOffCallback, nullptr);
  loadPreset(preset_index);
  boot_ms = millis();
  ready = true;
  Serial.printf("MVP0: ready, %u events, encoder pins 2/3/4, MIDI DIN channel %u\n",
                static_cast<unsigned>(score.event_count), kMidiChannel);
}

void jawHarpMvp0Loop() {
  using namespace jaw_harp_mvp0;
  if (!ready) return;
  const uint32_t now_ms = millis();
  updateEncoder();
  updateButton(now_ms);

  if (!auto_started && now_ms - boot_ms >= kStartupDelayMs) {
    restart(now_ms);
  }
  player.update(now_ms);
  if (player.state() != logged_state) {
    logged_state = player.state();
    if (logged_state == Player::State::ended) Serial.println("MVP0: phrase ended");
  }
}

#endif  // JAW_HARP_MVP0
