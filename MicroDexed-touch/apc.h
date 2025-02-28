#if defined APC

#ifndef apc_h_
#define apc_h_

void apc_clear_grid();
void apc_clear_right_buttons();
void print_apc_source_selection_pads();
void apc_print_right_buttons();
void apc_fader_control(uint8_t inData1, uint8_t inData2);
void apc_print_volume_pads();
uint8_t apc_NoteOn(byte inChannel, byte inData1, byte inData2);

#endif
#endif
