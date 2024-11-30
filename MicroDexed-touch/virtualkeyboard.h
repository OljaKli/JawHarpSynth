/*
   MicroDexed-touch

   MicroDexed is a port of the Dexed sound engine
   (https://github.com/asb2m10/dexed) for the Teensy-3.5/3.6/4.x with audio shield.
   Dexed ist heavily based on https://github.com/google/music-synthesizer-for-android

   (c)2018-2021 H. Wirtz <wirtz@parasitstudio.de>
   (c)2021      H. Wirtz <wirtz@parasitstudio.de>, M. Koslowski <positionhigh@gmx.de>

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

#ifndef _VIRTUALKEYBOARD_H
#define _VIRTUALKEYBOARD_H

#include <stdint.h>

enum VirtualKeyboardFlags : uint8_t {
    VK_CLEARSCREEN = 1 << 0,
    VK_DRAW_INSTRUMENT_BUTTONS = 1 << 1,
    VK_DRAW_OCTAVE_BUTTONS = 1 << 2,
    VK_DRAW_KEYS = 1 << 3,
    VK_DRAW_OCTAVE = 1 << 4,
    VK_DRAW_INSTUMENT_NAME = 1 << 5,
    VK_DRAW_ALL = 0xFF
};

void virtual_keyboard_print_velocity_bar();
void drawVirtualKeyboard(uint8_t drawFlags = VK_DRAW_ALL);
void handleVirtualKeyboardKeys();
void processOctaveButtons();
void touch_check_all_keyboard_buttons();
void virtual_keyboard_smart_preselect_mode();


#endif //_VIRTUALKEYBOARD_H