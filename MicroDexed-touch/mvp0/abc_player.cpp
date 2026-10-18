#include "abc_player.h"

#include <ctype.h>
#include <string.h>

namespace jaw_harp_mvp0 {
namespace {

struct Parser {
  Score& score;
  uint32_t voice_tick[2] = {0, 0};
  uint32_t default_ticks = kTicksPerQuarter;
  uint8_t voice = 0;
  const char* error = nullptr;

  bool add(uint32_t tick, uint8_t note, bool note_on) {
    if (score.event_count >= kMaxEvents) {
      error = "too many events";
      return false;
    }
    score.events[score.event_count++] = {tick, note, voice, note_on};
    if (tick > score.end_tick) score.end_tick = tick;
    return true;
  }

  uint32_t duration(const char*& p) {
    uint64_t numerator = 0;
    while (isdigit(static_cast<unsigned char>(*p))) {
      numerator = numerator * 10 + (*p++ - '0');
      if (numerator > UINT32_MAX) error = "duration exceeds 32-bit tick range";
    }
    if (numerator == 0) numerator = 1;

    uint64_t denominator = 1;
    if (*p == '/') {
      ++p;
      denominator = 0;
      while (isdigit(static_cast<unsigned char>(*p))) {
        denominator = denominator * 10 + (*p++ - '0');
        if (denominator > UINT32_MAX) error = "duration exceeds 32-bit tick range";
      }
      if (denominator == 0) denominator = 2;
    }
    const uint64_t ticks = static_cast<uint64_t>(default_ticks) * numerator / denominator;
    if (ticks > UINT32_MAX) error = "duration exceeds 32-bit tick range";
    return error ? 0 : static_cast<uint32_t>(ticks == 0 ? 1 : ticks);
  }

  bool advance(uint32_t ticks, uint32_t& end_tick) {
    const uint64_t end = static_cast<uint64_t>(voice_tick[voice]) + ticks;
    if (end > UINT32_MAX) {
      error = "score exceeds 32-bit tick range";
      return false;
    }
    end_tick = static_cast<uint32_t>(end);
    return true;
  }

  bool note(const char*& p, uint8_t& midi_note) {
    int accidental = 0;
    while (*p == '^' || *p == '_' || *p == '=') {
      if (*p == '^') ++accidental;
      else if (*p == '_') --accidental;
      else accidental = 0;
      ++p;
    }
    if (!((*p >= 'A' && *p <= 'G') || (*p >= 'a' && *p <= 'g'))) return false;

    static const int8_t semitones[7] = {9, 11, 0, 2, 4, 5, 7};
    const bool lower = *p >= 'a';
    const char upper = static_cast<char>(lower ? *p - ('a' - 'A') : *p);
    int value = (lower ? 72 : 60) + semitones[upper - 'A'] + accidental;
    ++p;
    while (*p == ',' || *p == '\'') {
      value += (*p++ == ',') ? -12 : 12;
    }
    if (value < 0 || value > 127) {
      error = "note outside MIDI range";
      return false;
    }
    midi_note = static_cast<uint8_t>(value);
    return true;
  }

  bool music(const char* p, const char* end) {
    while (p < end) {
      if (isspace(static_cast<unsigned char>(*p)) || *p == '|') {
        ++p;
        continue;
      }
      if (*p == '%') break;
      if (*p == 'z' || *p == 'Z') {
        ++p;
        const uint32_t ticks = duration(p);
        uint32_t end_tick = 0;
        if (!ticks || !advance(ticks, end_tick)) return false;
        voice_tick[voice] = end_tick;
        continue;
      }
      if (*p == '[') {
        ++p;
        uint8_t notes[2];
        uint8_t count = 0;
        while (p < end && *p != ']') {
          if (isspace(static_cast<unsigned char>(*p))) {
            ++p;
            continue;
          }
          if (count == 2 || !note(p, notes[count])) {
            error = count == 2 ? "chord exceeds two notes" : "invalid chord note";
            return false;
          }
          ++count;
        }
        if (p == end || *p != ']' || count == 0) {
          error = "invalid chord";
          return false;
        }
        ++p;
        const uint32_t ticks = duration(p);
        uint32_t end_tick = 0;
        if (!ticks || !advance(ticks, end_tick)) return false;
        for (uint8_t i = 0; i < count; ++i) {
          if (!add(voice_tick[voice], notes[i], true) ||
              !add(end_tick, notes[i], false)) return false;
        }
        voice_tick[voice] = end_tick;
        continue;
      }

      uint8_t midi_note = 0;
      if (!note(p, midi_note)) {
        error = "unsupported ABC token";
        return false;
      }
      const uint32_t ticks = duration(p);
      uint32_t end_tick = 0;
      if (!ticks || !advance(ticks, end_tick)) return false;
      if (!add(voice_tick[voice], midi_note, true) ||
          !add(end_tick, midi_note, false)) return false;
      voice_tick[voice] = end_tick;
    }
    return true;
  }
};

bool eventBefore(const Event& left, const Event& right) {
  if (left.tick != right.tick) return left.tick < right.tick;
  return left.note_on < right.note_on;  // release before attack at one tick
}

}  // namespace

bool parseAbc(const char* abc, Score& score, const char** error) {
  if (error) *error = nullptr;
  score = {};
  score.bpm = 120;
  Parser parser{score};

  const char* line = abc;
  while (*line) {
    const char* end = strchr(line, '\n');
    if (!end) end = line + strlen(line);
    while (line < end && isspace(static_cast<unsigned char>(*line))) ++line;

    if (end - line >= 2 && line[1] == ':') {
      const char* value = line + 2;
      while (value < end && isspace(static_cast<unsigned char>(*value))) ++value;
      if (line[0] == 'V') {
        if (value == end || (*value != '1' && *value != '2')) parser.error = "only V:1 and V:2 are supported";
        else parser.voice = static_cast<uint8_t>(*value - '1');
      } else if (line[0] == 'L') {
        uint64_t numerator = 0;
        uint64_t denominator = 0;
        while (value < end && isdigit(static_cast<unsigned char>(*value))) numerator = numerator * 10 + (*value++ - '0');
        if (value < end && *value == '/') ++value;
        while (value < end && isdigit(static_cast<unsigned char>(*value))) denominator = denominator * 10 + (*value++ - '0');
        const uint64_t ticks = denominator ? static_cast<uint64_t>(kTicksPerQuarter) * 4u * numerator / denominator : 0;
        if (!numerator || !denominator || !ticks || ticks > UINT32_MAX) parser.error = "invalid L header";
        else parser.default_ticks = static_cast<uint32_t>(ticks);
      } else if (line[0] == 'Q') {
        const char* number = value;
        const char* equals = static_cast<const char*>(memchr(value, '=', static_cast<size_t>(end - value)));
        if (equals) number = equals + 1;
        unsigned bpm = 0;
        while (number < end && !isdigit(static_cast<unsigned char>(*number))) ++number;
        while (number < end && isdigit(static_cast<unsigned char>(*number))) bpm = bpm * 10 + (*number++ - '0');
        if (bpm < 20 || bpm > 400) parser.error = "Q must be 20..400 BPM";
        else score.bpm = static_cast<uint16_t>(bpm);
      } else if (line[0] == 'K') {
        const char* key_end = end;
        while (key_end > value && isspace(static_cast<unsigned char>(key_end[-1]))) --key_end;
        if (key_end - value != 1 || *value != 'C') parser.error = "only K:C is supported";
      }
    } else if (line < end && !parser.music(line, end)) {
      if (error) *error = parser.error;
      return false;
    }

    if (parser.error) {
      if (error) *error = parser.error;
      return false;
    }
    line = *end ? end + 1 : end;
  }

  for (size_t i = 1; i < score.event_count; ++i) {
    const Event value = score.events[i];
    size_t j = i;
    while (j > 0 && eventBefore(value, score.events[j - 1])) {
      score.events[j] = score.events[j - 1];
      --j;
    }
    score.events[j] = value;
  }

  if (parser.voice_tick[0] > score.end_tick) score.end_tick = parser.voice_tick[0];
  if (parser.voice_tick[1] > score.end_tick) score.end_tick = parser.voice_tick[1];

  int active = 0;
  for (size_t i = 0; i < score.event_count; ++i) {
    active += score.events[i].note_on ? 1 : -1;
    if (active < 0 || active > 2) {
      if (error) *error = "score exceeds two simultaneous notes";
      return false;
    }
  }
  if (score.event_count == 0) {
    if (error) *error = "score contains no notes";
    return false;
  }
  return true;
}

void Player::configure(const Score* score, EventHandler event_handler,
                       AllNotesOffHandler all_notes_off_handler, void* context) {
  score_ = score;
  event_handler_ = event_handler;
  all_notes_off_handler_ = all_notes_off_handler;
  context_ = context;
  state_ = State::stopped;
  next_event_ = 0;
  position_ticks_ = 0;
  memset(active_notes_, 0, sizeof(active_notes_));
}

uint32_t Player::elapsedTicks(uint32_t now_ms) const {
  const uint32_t elapsed_ms = now_ms - started_ms_;
  return static_cast<uint32_t>((static_cast<uint64_t>(elapsed_ms) * score_->bpm * kTicksPerQuarter) / 60000u);
}

void Player::start(uint32_t now_ms) {
  if (!score_) return;
  next_event_ = 0;
  position_ticks_ = 0;
  memset(active_notes_, 0, sizeof(active_notes_));
  started_ms_ = now_ms;
  state_ = State::playing;
  update(now_ms);
}

void Player::restart(uint32_t now_ms) {
  if (all_notes_off_handler_) all_notes_off_handler_(context_);
  start(now_ms);
}

void Player::pause(uint32_t now_ms) {
  if (state_ != State::playing) return;
  update(now_ms);  // consume every event up to the cut point; resume must not retrigger it
  if (state_ != State::playing) return;
  state_ = State::paused;
  if (all_notes_off_handler_) all_notes_off_handler_(context_);
}

void Player::resume(uint32_t now_ms) {
  if (state_ != State::paused || !score_) return;
  const uint32_t elapsed_ms = static_cast<uint64_t>(position_ticks_) * 60000u /
                              (static_cast<uint32_t>(score_->bpm) * kTicksPerQuarter);
  started_ms_ = now_ms - elapsed_ms;
  state_ = State::playing;
  if (event_handler_) {
    for (uint8_t voice = 0; voice < 2; ++voice) {
      for (uint8_t note = 0; note < 128; ++note) {
        if (active_notes_[voice][note]) event_handler_({position_ticks_, note, voice, true}, context_);
      }
    }
  }
}

void Player::update(uint32_t now_ms) {
  if (state_ != State::playing || !score_) return;
  position_ticks_ = elapsedTicks(now_ms);
  while (next_event_ < score_->event_count && score_->events[next_event_].tick <= position_ticks_) {
    const Event& event = score_->events[next_event_];
    active_notes_[event.voice][event.note] = event.note_on;
    if (event_handler_) event_handler_(event, context_);
    ++next_event_;
  }
  if (next_event_ == score_->event_count && position_ticks_ >= score_->end_tick) {
    state_ = State::ended;
    if (all_notes_off_handler_) all_notes_off_handler_(context_);
  }
}

}  // namespace jaw_harp_mvp0
