#if defined APC

/*
Note by lucian2599 13.05.2025: 
this adds support for older Akai APC Mini Mk1. 
One advantage over Akai APC Mini Mk2 is that Mk1 needs only one midi channel, not all,
 because Mk1 has only 4 colours (red, green, yellow, white), with optional blinking.

Very helpful was the detailed APC Mk1 picture from
 https://github.com/TomasHubelbauer/akai-apc-mini 

Differences summary between Akai APC Mini Mk2 and Mk1:
| MIDI Diffs      |   APC Mini MK2    |   APC Mini MK1      |
|-----------------|-------------------|---------------------|
|NoteOn Channel   | brightness,blink  | not used, always 0  |
|NoteOn Note      | Pad number  0-63  | Pad number  0-63    |
|NoteOn Velocity  | 128 colours       | 4 colours and blink |
|NoteOn Tracks1-8 | notes 100-107     | notes 64-71         |
|NoteOn Scenes1-8 | notes 112-119     | notes 82-89         |
|Shift key        | note  122         | note  98            |
|Faders 1-9       | CC 48-56          | CC 48-56 (the same) |

*/
#if (APC != 1) //regular apc mini mk2
#define APC_MINI_BUTTON_RIGHT_FIRST  112 //1st scene/vertical button
#define APC_MINI_BUTTON_BOTTOM_FIRST 100 //1st track/horizontal button
#define APC_MINI_BUTTON_BOTTOM_VOLUME        (APC_MINI_BUTTON_BOTTOM_FIRST+0) //100, volume sample
#define APC_MINI_BUTTON_BOTTOM_PAN           (APC_MINI_BUTTON_BOTTOM_FIRST+1) //101, panorama sample
#define APC_MINI_BUTTON_BOTTOM_SEND          (APC_MINI_BUTTON_BOTTOM_FIRST+2) //102, send reverb,delay1,delay2 sample
#define APC_MINI_BUTTON_BOTTOM_DEVICE        (APC_MINI_BUTTON_BOTTOM_FIRST+3) //103, device pitch,cutoff,reso sample
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_P8     (APC_MINI_BUTTON_BOTTOM_FIRST+4) //104, pad +8
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_M8     (APC_MINI_BUTTON_BOTTOM_FIRST+5) //105, pad -8
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_M1     (APC_MINI_BUTTON_BOTTOM_FIRST+6) //106, pad -1
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_P1     (APC_MINI_BUTTON_BOTTOM_FIRST+7) //107, pad +1
#else // for Akai APC Mini Mk1
#define APC_MINI_BUTTON_RIGHT_FIRST   82 //1st scene/vertical button
#define APC_MINI_BUTTON_BOTTOM_FIRST  64 //1st track/horizontal button
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_P8     (APC_MINI_BUTTON_BOTTOM_FIRST+0) //64, pad +8
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_M8     (APC_MINI_BUTTON_BOTTOM_FIRST+1) //65, pad -8
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_M1     (APC_MINI_BUTTON_BOTTOM_FIRST+2) //66, pad -1
#define APC_MINI_BUTTON_BOTTOM_SAMPLE_P1     (APC_MINI_BUTTON_BOTTOM_FIRST+3) //67, pad +1
#define APC_MINI_BUTTON_BOTTOM_VOLUME        (APC_MINI_BUTTON_BOTTOM_FIRST+4) //68, volume sample
#define APC_MINI_BUTTON_BOTTOM_PAN           (APC_MINI_BUTTON_BOTTOM_FIRST+5) //69, panorama sample
#define APC_MINI_BUTTON_BOTTOM_SEND          (APC_MINI_BUTTON_BOTTOM_FIRST+6) //70, send reverb,delay1,delay2 sample
#define APC_MINI_BUTTON_BOTTOM_DEVICE        (APC_MINI_BUTTON_BOTTOM_FIRST+7) //71, device pitch,cutoff,reso sample
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


#ifndef apc_h_
#define apc_h_
extern bool APC_BUTTONS_RIGHT[8];
void apc(uint8_t a, uint8_t b, uint8_t c);
void apc_clear_grid();
void apc_clear_right_buttons();

/* In pattern mode, use Shift with the arrow buttons to switch control to 
Attack, Hold/Decay, Sustain, Release for the drum sample with the corresponding fader.
Switch between Hold/Decay by repeating Shift+down-arrow.
Or press (without shift) Volume to switch control to sample volume,
or press Pan for sample panorama.
Press Send multiple times to change send level for reverb, delay1, delay2.
Press Device multiple times to change pitch, filter cutoff, resonance.
*/
void apc_clear_bottom_buttons();
#define APC_EDIT_OFFSET 10 /* the mega offset for 2 adjacent buttons, it's big to have more mini-offsets when pressing same button multiple times */
typedef enum {
#if (APC != 1) //regular apc mini mk2 starts Volume/Pan/Send/Device and arrows (P8,M8,P1,M1)
      APC_EDIT_VOLUME    = 0,
      APC_EDIT_P8_ENVATT = APC_EDIT_OFFSET * 4,
#else //APC Mk1 starts with arrows (P8,M8,P1,M1) and then Volume/Pan/Send/Device
      APC_EDIT_VOLUME    = APC_EDIT_OFFSET * 4,
      APC_EDIT_P8_ENVATT = 0,
#endif
      APC_EDIT_PAN        = APC_EDIT_VOLUME + APC_EDIT_OFFSET,
      APC_EDIT_SEND_REV   = APC_EDIT_VOLUME + APC_EDIT_OFFSET * 2,
      APC_EDIT_SEND_DLAY1 = APC_EDIT_SEND_REV + 1,
      APC_EDIT_SEND_DLAY2 = APC_EDIT_SEND_REV + 2,
      APC_EDIT_DEV_PITCH  = APC_EDIT_VOLUME + APC_EDIT_OFFSET * 3,
      APC_EDIT_DEV_CUTOFF = APC_EDIT_DEV_PITCH + 1,
      APC_EDIT_DEV_RESO   = APC_EDIT_DEV_PITCH + 2,

      APC_EDIT_M8_ENVHOLD = APC_EDIT_P8_ENVATT + APC_EDIT_OFFSET,
      APC_EDIT_M8_ENVDEC  = APC_EDIT_M8_ENVHOLD + 1,
      APC_EDIT_M1_ENVSUS  = APC_EDIT_P8_ENVATT + APC_EDIT_OFFSET * 2,
      APC_EDIT_P1_ENVREL  = APC_EDIT_P8_ENVATT + APC_EDIT_OFFSET * 3,

      APC_EDIT_INIT_INVALID} enum_apcmixmod_t;
void apc_set_bottom_button(int8_t volpansenddev);
void print_apc_source_selection_pads();
void apc_print_right_buttons();
void apc_trigger_print_in_lcd(void);
void apc_print_volume_pads();
void apc_NoteOn(byte inChannel, byte inData1, byte inData2);
void apc_fader_control(uint8_t in1, uint8_t in2);
#endif
#endif
