/*
   MicroDexed-touch

   Song Arranger - Song section arranger keyboard for LSDJ-style sequencer

   (c)2018-2021 H. Wirtz <wirtz@parasitstudio.de>
   (c)2021-2026 H. Wirtz <wirtz@parasitstudio.de>, M. Koslowski <positionhigh@gmx.de>

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

#ifndef _UI_song_arranger_H_
#define _UI_song_arranger_H_

#include "config.h"

// Song Arranger functions
void UI_func_song_arranger(uint8_t param);
void draw_song_arranger_buttons(void);
void draw_song_arranger_config_grid(void);
void draw_song_arranger_enable_button(void);
void draw_song_arranger_midi_note_selector(void);
void handle_song_arranger_touch(uint8_t button_index);
void init_song_arranger(void);
uint8_t get_song_arranger_part_for_song_step(uint8_t step);
uint16_t get_song_arranger_color_for_song_step(uint8_t step);
void update_song_arranger_midi_notes(void);

// Flag to track if Song Arranger data has been loaded from a performance
extern bool song_arranger_data_loaded_from_performance;

#endif // _UI_song_arranger_H_