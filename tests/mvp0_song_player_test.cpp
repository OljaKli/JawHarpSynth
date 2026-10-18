#include <assert.h>

#include "../MicroDexed-touch/mvp0/song_player.h"

using namespace jaw_harp_mvp0;

namespace {

struct Recorder {
  unsigned events = 0;
  unsigned note_ons = 0;
  unsigned all_offs = 0;
};

void recordNote(uint8_t, uint8_t, uint8_t, bool note_on, void* context) {
  auto& recorder = *static_cast<Recorder*>(context);
  ++recorder.events;
  if (note_on) ++recorder.note_ons;
}

void recordAllOff(void* context) {
  ++static_cast<Recorder*>(context)->all_offs;
}

}  // namespace

int main() {
  Recorder recorder;
  SongPlayer player;
  player.configure(recordNote, recordAllOff, &recorder);

  player.select(0, 100);
  assert(player.track() == 0);
  assert(player.state() == SongPlayer::State::playing);
  assert(recorder.note_ons == 2);  // Katy accompaniment and kick at t=0.

  player.pause(200);
  assert(player.state() == SongPlayer::State::paused);
  assert(recorder.all_offs == 2);  // select reset + pause cut.
  player.resume(1000);
  assert(recorder.note_ons == 4);  // active notes are restored.

  player.select(1, 2000);
  assert(player.track() == 1);
  assert(recorder.note_ons == 5);  // Bach begins immediately on channel 1.

  const unsigned before_rick = recorder.note_ons;
  player.select(2, 3000);
  assert(player.track() == 2);
  assert(recorder.note_ons > before_rick);  // Rick has a chord at t=0.
}
