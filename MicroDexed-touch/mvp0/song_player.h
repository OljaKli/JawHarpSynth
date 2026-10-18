#pragma once

#include <stdint.h>

namespace jaw_harp_mvp0 {

constexpr uint8_t kTrackCount = 3;

class SongPlayer {
public:
  enum class State : uint8_t { stopped, playing, paused, ended };
  using NoteHandler = void (*)(uint8_t channel, uint8_t note, uint8_t velocity,
                               bool note_on, void* context);
  using AllNotesOffHandler = void (*)(void* context);

  void configure(NoteHandler note_handler, AllNotesOffHandler all_notes_off_handler,
                 void* context);
  void select(uint8_t track, uint32_t now_ms);
  void restart(uint32_t now_ms);
  void pause(uint32_t now_ms);
  void resume(uint32_t now_ms);
  void update(uint32_t now_ms);

  uint8_t track() const { return track_; }
  const char* trackName() const;
  State state() const { return state_; }

private:
  void resetPlayback(uint32_t now_ms);
  void emit(uint8_t channel, uint8_t note, uint8_t velocity, bool note_on);
  void finish();
  void updateMidi(uint32_t delta_ms);
  void updateBach(uint32_t delta_ms);
  bool prepareBachEvent();
  void resetBarAccidentals();
  int parseBachPitch(const char*& cursor);
  float parseBachDuration(const char*& cursor) const;

  NoteHandler note_handler_ = nullptr;
  AllNotesOffHandler all_notes_off_handler_ = nullptr;
  void* context_ = nullptr;
  State state_ = State::stopped;
  uint8_t track_ = 0;
  uint32_t last_ms_ = 0;
  uint8_t active_notes_[16][128] = {};

  uint32_t midi_position_ms_ = 0;
  uint16_t midi_index_ = 0;
  uint8_t pass_ = 0;

  const char* bach_cursor_ = nullptr;
  int8_t bar_accidental_[128] = {};
  bool bach_event_ready_ = false;
  bool bach_note_sounding_ = false;
  int16_t bach_note_ = -1;
  uint32_t bach_event_duration_ms_ = 0;
  uint32_t bach_gate_duration_ms_ = 0;
  uint32_t bach_event_elapsed_ms_ = 0;
};

}  // namespace jaw_harp_mvp0
