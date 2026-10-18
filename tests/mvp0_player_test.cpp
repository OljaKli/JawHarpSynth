#include <assert.h>
#include <stdint.h>

#include "../MicroDexed-touch/mvp0/abc_player.h"

using namespace jaw_harp_mvp0;

namespace {

constexpr char kPhrase[] =
    "X:1\nT:test\nM:4/4\nL:1/4\nQ:120\nK:C\n"
    "V:1\nC D E F | [EG]2 z2 |\n"
    "V:2\nC,2 G,2 | z4 |\n";

struct Recorder {
  unsigned events = 0;
  unsigned note_ons = 0;
  unsigned all_offs = 0;
};

void recordEvent(const Event& event, void* context) {
  auto& recorder = *static_cast<Recorder*>(context);
  ++recorder.events;
  if (event.note_on) ++recorder.note_ons;
}

void recordAllOff(void* context) {
  ++static_cast<Recorder*>(context)->all_offs;
}

}  // namespace

int main() {
  Score score;
  const char* error = nullptr;
  assert(parseAbc(kPhrase, score, &error));
  assert(score.bpm == 120);
  assert(score.event_count == 16);
  assert(score.end_tick == 768);

  Score long_note;
  assert(parseAbc("L:1/4\nK:C\nC700", long_note, &error));
  assert(long_note.end_tick == 67200);
  assert(long_note.events[1].tick == 67200);

  Score unsupported_key;
  assert(!parseAbc("K:G\nF", unsupported_key, &error));

  Score invalid;
  assert(!parseAbc("L:1/4\n[CEG]", invalid, &error));

  Recorder recorder;
  Player player;
  player.configure(&score, recordEvent, recordAllOff, &recorder);
  player.start(0);
  assert(player.state() == Player::State::playing);
  assert(recorder.events == 2);
  assert(recorder.note_ons == 2);

  player.pause(250);  // both opening notes are still active
  assert(player.state() == Player::State::paused);
  assert(recorder.events == 2);
  assert(recorder.all_offs == 1);

  player.update(2000);
  assert(recorder.events == 2);
  player.resume(2000);
  assert(recorder.events == 4);
  assert(recorder.note_ons == 4);
  player.update(2250);
  assert(recorder.events == 6);

  player.update(7000);
  assert(player.state() == Player::State::ended);
  assert(recorder.events == score.event_count + 2);
  assert(recorder.all_offs == 2);

  player.restart(8000);
  assert(player.state() == Player::State::playing);
  assert(recorder.all_offs == 3);
  assert(recorder.events == score.event_count + 4);
}
