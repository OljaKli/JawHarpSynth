/*
   MicroDexed

   MicroMDAEPiano is a port of the MDA-EPiano sound engine
   (https://sourceforge.net/projects/mda-vst/) for the Teensy-3.5/3.6/4.x with audio shield.

   (c)2019-2021 H. Wirtz <wirtz@parasitstudio.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef MIDI_DEVICES_H
#define MIDI_DEVICES_H

#include "config.h"

extern config_t configuration;

#include "sequencer.h"
extern sequencer_t seq;
extern int temp_int;
extern uint8_t GAMEPAD_UP_0;
extern uint8_t GAMEPAD_UP_1;
extern uint32_t GAMEPAD_UP_BUTTONS;
extern uint8_t GAMEPAD_DOWN_0;
extern uint8_t GAMEPAD_DOWN_1;
extern uint32_t GAMEPAD_DOWN_BUTTONS;
extern uint8_t GAMEPAD_RIGHT_0;
extern uint8_t GAMEPAD_RIGHT_1;
extern uint32_t GAMEPAD_RIGTH_BUTTONS;
extern uint8_t GAMEPAD_LEFT_0;
extern uint8_t GAMEPAD_LEFT_1;
extern uint32_t GAMEPAD_LEFT_BUTTONS;
extern uint32_t GAMEPAD_SELECT;
extern uint32_t GAMEPAD_START;
extern uint32_t GAMEPAD_BUTTON_A;
extern uint32_t GAMEPAD_BUTTON_B;

#include "MIDI.h"

/* #if defined(MIDI_DEVICE_USB)
  #include <midi_UsbTransport.h>
  #endif */
#ifdef MIDI_DEVICE_USB_HOST
  #include <USBHost_t36.h>
#endif
  

  
  const float midi_ticks_factor[10] = { 0.0, 0.25, 0.375, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };


void send_sysex_bank(uint8_t midi_channel, uint8_t* bank_data);
void send_sysex_param(uint8_t midi_channel, uint8_t var, uint8_t val, uint8_t param_group);
extern void handleNoteOn(byte inChannel, byte inNumber, byte inVelocity, byte device);
extern void handleNoteOff(byte inChannel, byte inNumber, byte inVelocity, byte device);
extern void handleAfterTouch(byte inChannel, byte inPressure);
extern void handlePitchBend(byte inChannel, int16_t inPitch);
extern void handleControlChange(byte inChannel, byte inData1, byte inData2);
extern void handleProgramChange(byte inChannel, byte inProgram);
extern void handleAfterTouchPoly(byte inChannel, byte inNumber, byte inVelocity);
extern void handleSystemExclusive(byte* data, unsigned len);
 // void handleSystemExclusiveChunk(const byte *data, uint len, bool last);
extern void handleTimeCodeQuarterFrame(byte data);
extern void handleSongSelect(byte inSong);
extern void handleTuneRequest(void);
extern void handleClock(void);
extern void handleStart(void);
extern void handleContinue(void);
extern void handleStop(void);
extern void handleActiveSensing(void);
extern void handleSystemReset(void);
// void handleRealTimeSystem(void);
void MD_sendControlChange(uint8_t channel, uint8_t cc, uint8_t value);
void setup_midi_devices(void);
void check_midi_devices(void);

#endif // MIDI_DEVICES_H
