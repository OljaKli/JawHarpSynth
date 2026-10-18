#include "song_player.h"

#include <string.h>

#include "songs.h"

namespace jaw_harp_mvp0 {
namespace {

constexpr const char* kTrackNames[kTrackCount] = {
    "Katy Perry - I Kissed a Girl",
    "Bach - Toccata and Fugue BWV 565",
    "Rick Astley - Never Gonna Give You Up (chorus x4)",
    "Pink Floyd - Wish You Were Here",
};

int letterBase(char note) {
  switch (note) {
    case 'C': case 'c': return 0;
    case 'D': case 'd': return 2;
    case 'E': case 'e': return 4;
    case 'F': case 'f': return 5;
    case 'G': case 'g': return 7;
    case 'A': case 'a': return 9;
    case 'B': case 'b': return 11;
    default: return 0;
  }
}

bool isNoteLetter(char value) {
  return (value >= 'A' && value <= 'G') || (value >= 'a' && value <= 'g');
}

}  // namespace

void SongPlayer::configure(NoteHandler note_handler,
                           AllNotesOffHandler all_notes_off_handler, void* context) {
  note_handler_ = note_handler;
  all_notes_off_handler_ = all_notes_off_handler;
  context_ = context;
}

const char* SongPlayer::trackName() const {
  return kTrackNames[track_];
}

void SongPlayer::emit(uint8_t channel, uint8_t note, uint8_t velocity, bool note_on) {
  if (channel == 0 || channel > 16 || note > 127) return;
  active_notes_[channel - 1][note] = note_on ? velocity : 0;
  if (note_handler_) note_handler_(channel, note, velocity, note_on, context_);
}

void SongPlayer::resetBarAccidentals() {
  memset(bar_accidental_, 127, sizeof(bar_accidental_));
}

void SongPlayer::resetPlayback(uint32_t now_ms) {
  if (all_notes_off_handler_) all_notes_off_handler_(context_);
  memset(active_notes_, 0, sizeof(active_notes_));
  last_ms_ = now_ms;
  midi_position_ms_ = 0;
  midi_index_ = 0;
  pass_ = 0;
  bach_cursor_ = bachScore;
  resetBarAccidentals();
  bach_event_ready_ = false;
  bach_note_sounding_ = false;
  bach_note_ = -1;
  bach_event_elapsed_ms_ = 0;
  state_ = State::playing;
  update(now_ms);
}

void SongPlayer::select(uint8_t track, uint32_t now_ms) {
  track_ = track % kTrackCount;
  resetPlayback(now_ms);
}

void SongPlayer::restart(uint32_t now_ms) {
  resetPlayback(now_ms);
}

void SongPlayer::pause(uint32_t now_ms) {
  if (state_ != State::playing) return;
  update(now_ms);
  if (state_ != State::playing) return;
  state_ = State::paused;
  if (all_notes_off_handler_) all_notes_off_handler_(context_);
}

void SongPlayer::resume(uint32_t now_ms) {
  if (state_ != State::paused) return;
  last_ms_ = now_ms;
  state_ = State::playing;
  if (!note_handler_) return;
  for (uint8_t channel = 0; channel < 16; ++channel) {
    for (uint8_t note = 0; note < 128; ++note) {
      const uint8_t velocity = active_notes_[channel][note];
      if (velocity) note_handler_(channel + 1, note, velocity, true, context_);
    }
  }
}

void SongPlayer::finish() {
  if (all_notes_off_handler_) all_notes_off_handler_(context_);
  memset(active_notes_, 0, sizeof(active_notes_));
  state_ = State::ended;
}

void SongPlayer::update(uint32_t now_ms) {
  if (state_ != State::playing) return;
  const uint32_t delta_ms = now_ms - last_ms_;
  last_ms_ = now_ms;
  if (track_ == 1) updateBach(delta_ms);
  else updateMidi(delta_ms);
}

void SongPlayer::updateMidi(uint32_t delta_ms) {
  const MidiEvent* events = katyEvents;
  size_t count = KATY_EVENT_COUNT;
  uint32_t duration_ms = KATY_DURATION_MS;
  if (track_ == 2) {
    events = rickEvents;
    count = RICK_EVENT_COUNT;
    duration_ms = RICK_DURATION_MS;
  } else if (track_ == 3) {
    events = wishYouWereHereEvents;
    count = WISH_YOU_WERE_HERE_EVENT_COUNT;
    duration_ms = WISH_YOU_WERE_HERE_DURATION_MS;
  }
  midi_position_ms_ += delta_ms;

  while (midi_index_ < count && events[midi_index_].atMs <= midi_position_ms_) {
    const MidiEvent& event = events[midi_index_++];
    const uint8_t channel = (event.status & 0x0f) + 1;
    const bool note_on = (event.status & 0xf0) == 0x90 && event.b != 0;
    emit(channel, event.a, event.b, note_on);
  }

  if (midi_position_ms_ < duration_ms) return;
  if (track_ == 2 && ++pass_ < RICK_REPEATS) {
    if (all_notes_off_handler_) all_notes_off_handler_(context_);
    memset(active_notes_, 0, sizeof(active_notes_));
    midi_position_ms_ = 0;
    midi_index_ = 0;
    updateMidi(0);
    return;
  }
  finish();
}

float SongPlayer::parseBachDuration(const char*& cursor) const {
  uint32_t numerator = 1;
  uint32_t denominator = 1;
  bool has_numerator = false;
  if (*cursor >= '0' && *cursor <= '9') {
    has_numerator = true;
    numerator = 0;
    while (*cursor >= '0' && *cursor <= '9') numerator = numerator * 10 + (*cursor++ - '0');
  }
  if (*cursor == '/') {
    ++cursor;
    if (*cursor >= '0' && *cursor <= '9') {
      denominator = 0;
      while (*cursor >= '0' && *cursor <= '9') denominator = denominator * 10 + (*cursor++ - '0');
    } else {
      denominator = 2;
    }
  }
  if (!has_numerator) numerator = 1;
  if (!denominator) denominator = 1;
  return static_cast<float>(numerator) / denominator;
}

int SongPlayer::parseBachPitch(const char*& cursor) {
  int accidental = 0;
  bool explicit_accidental = false;
  if (*cursor == '^' || *cursor == '_' || *cursor == '=') {
    explicit_accidental = true;
    accidental = *cursor == '^' ? 1 : (*cursor == '_' ? -1 : 0);
    ++cursor;
  }
  const char letter = *cursor++;
  int midi = ((letter >= 'a' && letter <= 'g') ? 72 : 60) + letterBase(letter);
  while (*cursor == ',' || *cursor == '\'') midi += *cursor++ == ',' ? -12 : 12;
  const int natural = midi;
  if (explicit_accidental) {
    if (natural >= 0 && natural < 128) bar_accidental_[natural] = accidental;
    midi += accidental;
  } else if (natural >= 0 && natural < 128 && bar_accidental_[natural] != 127) {
    midi += bar_accidental_[natural];
  } else if (letter == 'B' || letter == 'b') {
    --midi;  // Bach source uses K:F (B-flat).
  }
  if (midi < 0) return 0;
  return midi > 127 ? 127 : midi;
}

bool SongPlayer::prepareBachEvent() {
  bach_note_ = -1;
  float units = 0;
  while (*bach_cursor_) {
    const char token = *bach_cursor_;
    if (token == '|') {
      resetBarAccidentals();
      ++bach_cursor_;
      continue;
    }
    if (token == 'z' || token == 'Z') {
      ++bach_cursor_;
      units = parseBachDuration(bach_cursor_);
      break;
    }
    if (token == '^' || token == '_' || token == '=' || isNoteLetter(token)) {
      bach_note_ = static_cast<int16_t>(parseBachPitch(bach_cursor_));
      units = parseBachDuration(bach_cursor_);
      while (*bach_cursor_ == '-') {
        ++bach_cursor_;
        const char* saved_cursor = bach_cursor_;
        int8_t saved_accidentals[128];
        memcpy(saved_accidentals, bar_accidental_, sizeof(saved_accidentals));
        if (!(*bach_cursor_ == '^' || *bach_cursor_ == '_' || *bach_cursor_ == '=' ||
              isNoteLetter(*bach_cursor_))) break;
        const int tied_note = parseBachPitch(bach_cursor_);
        const float extra = parseBachDuration(bach_cursor_);
        if (tied_note == bach_note_) units += extra;
        else {
          bach_cursor_ = saved_cursor;
          memcpy(bar_accidental_, saved_accidentals, sizeof(saved_accidentals));
          break;
        }
      }
      break;
    }
    ++bach_cursor_;
  }
  if (units <= 0) return false;

  bach_event_duration_ms_ = static_cast<uint32_t>(units * (30000.0f / 115.0f) + 0.5f);
  if (!bach_event_duration_ms_) bach_event_duration_ms_ = 1;
  bach_gate_duration_ms_ = bach_event_duration_ms_ * 9 / 10;
  bach_event_elapsed_ms_ = 0;
  bach_event_ready_ = true;
  if (bach_note_ >= 0) {
    emit(1, static_cast<uint8_t>(bach_note_), 96, true);
    bach_note_sounding_ = true;
  }
  return true;
}

void SongPlayer::updateBach(uint32_t delta_ms) {
  while (state_ == State::playing) {
    if (!bach_event_ready_ && !prepareBachEvent()) {
      finish();
      return;
    }
    if (!delta_ms) return;

    const uint32_t remaining = bach_event_duration_ms_ - bach_event_elapsed_ms_;
    const uint32_t step = delta_ms < remaining ? delta_ms : remaining;
    bach_event_elapsed_ms_ += step;
    delta_ms -= step;
    if (bach_note_sounding_ && bach_event_elapsed_ms_ >= bach_gate_duration_ms_) {
      emit(1, static_cast<uint8_t>(bach_note_), 0, false);
      bach_note_sounding_ = false;
    }
    if (bach_event_elapsed_ms_ < bach_event_duration_ms_) return;
    bach_event_ready_ = false;
  }
}

}  // namespace jaw_harp_mvp0
