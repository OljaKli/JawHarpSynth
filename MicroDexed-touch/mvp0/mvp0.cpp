#ifdef JAW_HARP_MVP0

#include "mvp0.h"

#include <Arduino.h>
#include <Audio.h>
#include <Encoder.h>
#include <string.h>

#include "embedded_presets.h"
#include "song_player.h"
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
constexpr uint8_t kVelocity = 100;
constexpr uint16_t kDebounceMs = 20;
constexpr uint16_t kLongPressMs = 500;
constexpr uint16_t kStartupDelayMs = 500;
constexpr int kEncoderCountsPerDetent = 4;

Encoder encoder(kEncoderA, kEncoderB);
SongPlayer player;
uint8_t internal_note_references[128] = {};
uint8_t din_note_references[16][128] = {};
uint8_t track_index = 0;
int encoder_detent = 0;
uint32_t boot_ms = 0;
uint32_t button_changed_ms = 0;
uint32_t button_pressed_ms = 0;
bool button_raw = true;
bool button_stable = true;
bool long_press_fired = false;
bool ready = false;
bool auto_started = false;
SongPlayer::State logged_state = SongPlayer::State::stopped;

void allNotesOff() {
  for (uint16_t note = 0; note < 128; ++note) {
    if (internal_note_references[note] && MicroDexed[0]) {
      MicroDexed[0]->keyup(static_cast<uint8_t>(note));
    }
    internal_note_references[note] = 0;
  }
  for (uint8_t channel = 0; channel < 16; ++channel) {
    for (uint16_t note = 0; note < 128; ++note) {
      while (din_note_references[channel][note]) {
        midiNoteOff(static_cast<uint8_t>(note), 0, channel + 1, 2);
        --din_note_references[channel][note];
      }
    }
  }
}

void routeEvent(uint8_t channel, uint8_t note, uint8_t velocity, bool note_on, void*) {
  uint8_t& internal_references = internal_note_references[note];
  uint8_t& din_references = din_note_references[channel - 1][note];
  if (note_on) {
    if (internal_references++ == 0 && MicroDexed[0]) {
      MicroDexed[0]->keydown(note, velocity ? velocity : kVelocity);
    }
    ++din_references;
    midiNoteOn(note, velocity, channel, 2);
  } else {
    if (internal_references && --internal_references == 0 && MicroDexed[0]) {
      MicroDexed[0]->keyup(note);
    }
    if (din_references) --din_references;
    midiNoteOff(note, 0, channel, 2);
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
  Serial.printf("MVP0: restarted %s\n", player.trackName());
}

void shortPress(uint32_t now_ms) {
  if (player.state() == SongPlayer::State::playing) {
    player.pause(now_ms);
    Serial.println("MVP0: paused");
  } else if (player.state() == SongPlayer::State::paused) {
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

void selectTrack(uint8_t index, uint32_t now_ms) {
  track_index = index % kTrackCount;
  player.select(track_index, now_ms);
  auto_started = true;
  Serial.printf("MVP0: track %u/%u %s\n", track_index + 1, kTrackCount, player.trackName());
}

void updateEncoder(uint32_t now_ms) {
  const int detent = static_cast<int>(encoder.read() / kEncoderCountsPerDetent);
  if (detent == encoder_detent) return;
  const int delta = detent - encoder_detent;
  encoder_detent = detent;
  int next = static_cast<int>(track_index) + delta;
  while (next < 0) next += kTrackCount;
  selectTrack(static_cast<uint8_t>(next % kTrackCount), now_ms);
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

  player.configure(routeEvent, allNotesOffCallback, nullptr);
  loadPreset(0);
  boot_ms = millis();
  ready = true;
  Serial.println("MVP0: ready, turn selects Katy/Bach/Rick; press pauses; hold restarts");
}

void jawHarpMvp0Loop() {
  using namespace jaw_harp_mvp0;
  if (!ready) return;
  const uint32_t now_ms = millis();
  updateEncoder(now_ms);
  updateButton(now_ms);

  if (!auto_started && now_ms - boot_ms >= kStartupDelayMs) {
    selectTrack(0, now_ms);
  }
  player.update(now_ms);
  if (player.state() != logged_state) {
    logged_state = player.state();
    if (logged_state == SongPlayer::State::ended) Serial.println("MVP0: song ended; turn or hold to play");
  }
}

#endif  // JAW_HARP_MVP0
