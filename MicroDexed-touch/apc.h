#if defined APC

#ifndef apc_h_
#define apc_h_
extern bool APC_BUTTONS_RIGHT[8];
void apc(uint8_t a, uint8_t b, uint8_t c);
void apc_clear_grid();
void apc_clear_right_buttons();
void print_apc_source_selection_pads();
void apc_print_right_buttons();
void apc_fader_control(uint8_t inData1, uint8_t inData2);
void apc_print_volume_pads();
void apc_NoteOn(byte inChannel, byte inData1, byte inData2);
void apc_fader_control(uint8_t in1, uint8_t in2);
#endif
#endif
