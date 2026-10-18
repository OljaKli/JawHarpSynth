#pragma once

#include <stddef.h>
#include <stdint.h>

namespace jaw_harp_mvp0 {

constexpr uint16_t kTicksPerQuarter = 96;
constexpr size_t kMaxEvents = 96;

struct Event {
  uint32_t tick;
  uint8_t note;
  uint8_t voice;
  bool note_on;
};

struct Score {
  Event events[kMaxEvents];
  size_t event_count;
  uint16_t bpm;
  uint32_t end_tick;
};

bool parseAbc(const char* abc, Score& score, const char** error = nullptr);

class Player {
public:
  enum class State : uint8_t { stopped, playing, paused, ended };
  using EventHandler = void (*)(const Event&, void*);
  using AllNotesOffHandler = void (*)(void*);

  void configure(const Score* score, EventHandler event_handler,
                 AllNotesOffHandler all_notes_off_handler, void* context);
  void start(uint32_t now_ms);
  void restart(uint32_t now_ms);
  void pause(uint32_t now_ms);
  void resume(uint32_t now_ms);
  void update(uint32_t now_ms);

  State state() const { return state_; }
  uint32_t positionTicks() const { return position_ticks_; }

private:
  uint32_t elapsedTicks(uint32_t now_ms) const;

  const Score* score_ = nullptr;
  EventHandler event_handler_ = nullptr;
  AllNotesOffHandler all_notes_off_handler_ = nullptr;
  void* context_ = nullptr;
  State state_ = State::stopped;
  size_t next_event_ = 0;
  uint32_t started_ms_ = 0;
  uint32_t position_ticks_ = 0;
  bool active_notes_[2][128] = {};
};

}  // namespace jaw_harp_mvp0
