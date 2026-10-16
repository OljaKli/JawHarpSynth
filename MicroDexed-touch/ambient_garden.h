/*
   MicroDexed-touch - Ambient Garden
   AMB-1 (X0X), the generative ambient garden, as a page of MDT: AMB-1's own
   engine (amb.c / amb.h) in MDT's audio graph, played through MDT's plate
   reverb and delays. See ambient_garden_impl.h.

   Timing: the engine keeps its own, sample-exact, in the audio render.
   Start and stop are MDT's handleStart() / handleStop(); the tempo is
   seq.bpm, restored on exit. Level meters and the garden drawing run from
   the touch handler.
*/
#ifndef _AMBIENT_GARDEN_H_
#define _AMBIENT_GARDEN_H_

#include <Arduino.h>

#include "AudioStream.h"

#define AG_NPIECES      8
#define AG_NBELLPRESETS 7

// AMB-1's engine as an audio object. One instance, ambient_garden_engine,
// defined in MicroDexed-touch.ino; update() is in ambient_garden_impl.h.
// Outputs: 0 dry left, 1 dry right, 2 reverb send (mono), 3 echo send (mono).
class AudioSynthAmbientGarden : public AudioStream
{
public:
    AudioSynthAmbientGarden() : AudioStream(0, NULL) {}
    virtual void update(void);
    volatile bool running = false;                  // renders only while the garden page is open
};

void ambient_garden_init(void);
void ambient_garden_start(void);
void ambient_garden_stop(void);
void ambient_garden_panic(void);
void ambient_garden_set_piece(int i);
const char* ambient_garden_piece_name(int i);

void UI_func_ambient_garden(uint8_t param);

// MDT's transport on the garden page (LCDML.FUNC_getID() == UI_func_ambient_garden):
void ambient_garden_start_playing(void);   // handleStart(): before the sequencer timer starts
void ambient_garden_stop_playing(void);    // handleStop(): after the sequencer timer stopped
void ambient_garden_sequencer(void);       // sequencer(): one tick
bool ambient_garden_playing(void);         // the garden is playing (handleStop() uses its branch)

// Called from lcdml_menu_control() when the left encoder is turned on the
// garden page. Adjusts the master volume by the given delta (typically +1 or
// -1), applies it via set_volume(), and refreshes the on-screen readout on
// the row below the BPM display. Replaces the previous jump to UI_func_volume
// so the garden keeps playing while the user rides the volume.
void ambient_garden_adjust_volume(int delta);

// Called from the main loop of MicroDexed-touch.ino on every iteration,
// regardless of which page is currently active. Drains the garden's MIDI
// out ring so a starved garden page cannot silently orphan bell note-offs
// (which would leave stuck Dexed voices, crackling audio, and eventually
// an unresponsive UI). Cheap when the ring is empty.
void ambient_garden_tick(void);

#endif