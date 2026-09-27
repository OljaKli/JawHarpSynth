#ifndef _XIAO_MIDI_H_
#define _XIAO_MIDI_H_

#include "config.h"

#if defined XIAO_BLUETOOTH_MIDI
void xiao_midi_setup(void);
void xiao_midi_loop(void);
#endif

#endif // _XIAO_MIDI_H_