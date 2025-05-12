#if defined APC

#if (APC != 1) //regular apc mini mk2
#define APC_MINI_BUTTON_RIGHT_FIRST  112 //1st scene/vertical button
#define APC_MINI_BUTTON_BOTTOM_FIRST 100 //1st track/horizontal button
#else
#define APC_MINI_BUTTON_RIGHT_FIRST   82 //1st scene/vertical button
#define APC_MINI_BUTTON_BOTTOM_FIRST  64 //1st track/horizontal button
#endif

#define APC_MINI_BUTTON_RIGHT_PATTERN_EDITOR APC_MINI_BUTTON_RIGHT_FIRST     //112 or 82
#define APC_MINI_BUTTON_RIGHT_SONG_EDITOR    (APC_MINI_BUTTON_RIGHT_FIRST+1) //113 or 83
#define APC_MINI_BUTTON_RIGHT_MUTE_MATRIX    (APC_MINI_BUTTON_RIGHT_FIRST+2) //114 or 84

#define APC_MINI_BUTTON_RIGHT_PATTERN_UP     APC_MINI_BUTTON_RIGHT_FIRST     //112 or 82
#define APC_MINI_BUTTON_RIGHT_PATTERN_DOWN   (APC_MINI_BUTTON_RIGHT_FIRST+1) //113 or 83
#define APC_MINI_BUTTON_RIGHT_SEQ_START_STOP (APC_MINI_BUTTON_RIGHT_FIRST+2) //114 or 84
#define APC_MINI_BUTTON_RIGHT_VELOCITY_UPPER (APC_MINI_BUTTON_RIGHT_FIRST+3) //115 or 85
#define APC_MINI_BUTTON_RIGHT_VELOCITY_LOWER (APC_MINI_BUTTON_RIGHT_FIRST+4) //116 or 86
#define APC_MINI_BUTTON_RIGHT_DISAB1_PADSEL  (APC_MINI_BUTTON_RIGHT_FIRST+5) //117 or 87
#define APC_MINI_BUTTON_RIGHT_ENABLE_PADSEL  (APC_MINI_BUTTON_RIGHT_FIRST+6) //118 or 88
#define APC_MINI_BUTTON_RIGHT_DISAB2_PADSEL  (APC_MINI_BUTTON_RIGHT_FIRST+7) //119 or 89

#define APC_MINI_BUTTON_BOTTOM_SAMPLE_P8     (APC_MINI_BUTTON_BOTTOM_FIRST+4) //104 or 68, pad +8
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_M8     (APC_MINI_BUTTON_BOTTOM_FIRST+5) //105 or 69, pad -8
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_M1     (APC_MINI_BUTTON_BOTTOM_FIRST+6) //106 or 70, pad -1
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_P1     (APC_MINI_BUTTON_BOTTOM_FIRST+7) //107 or 71, pad +1

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
