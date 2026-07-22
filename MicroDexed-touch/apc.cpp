#if defined APC

#include <Arduino.h>
#include "apc.h"
#include "config.h"
#include "drums.h"
#include "sequencer.h"
#include <LCDMenuLib2.h>
#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined RA_DISPLAY
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif
#include "UI.h"

extern void apc(uint8_t a, uint8_t b, uint8_t c);
extern drum_config_t drum_config[NUM_DRUMSET_CONFIG];
extern config_t configuration;
extern uint8_t activeSample;
extern uint8_t drum_midi_channel;
extern uint8_t APC_SAMPLE_SCROLL_OFFSET;
extern uint8_t APC_TONAL_SCROLL_OFFSET;
extern uint8_t APC_PREVIOUS_NOTE;
extern uint8_t APC_MODE;
#ifdef APC_VOLPANSENDDEV
enum_apcmixmod_t apc_mixmod = APC_EDIT_INIT_INVALID;
#endif /* ifdef APC_VOLPANSENDDEV */
extern bool APC_BUTTONS_RIGHT[8];
extern void print_current_sample_and_pitch_buffer();
extern void print_track_steps_detailed(int xpos, int ypos, uint8_t currentstep, bool init, bool allsteps);
extern void seq_printAllVelocitySteps();
extern void  seq_print_single_velocity_step(uint8_t step, int color);
extern void seq_printAllSeqSteps();
extern void pattern_editor_play_current_step(uint8_t step);
extern void insert_pattern_step(uint8_t step);
extern void  _seq_pattern_change();
extern void  print_content_type();
extern void virtual_keyboard_smart_preselect_mode();
extern void _velocity_editor_change_pattern();
extern void update_latch_button();
extern void handleStart();
extern void handleStop();
extern void print_small_scaled_bar(uint8_t x, uint8_t y, int16_t input_value, int16_t limit_min, int16_t limit_max, int16_t selected_option, boolean show_bar, const char* zero_name = NULL);
extern LCDMenuLib2 LCDML;

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined RA_DISPLAY
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

extern sequencer_t seq;
extern int apc_scroll_counter;
extern char apc_scrolltext[16];
extern uint8_t apc_scroll_message;
extern bool apc_shift_key;
extern int temp_int;
uint8_t apc_out_channel;
extern void  print_chain_matrix_in_song_page();
static int trigger_print_sample_or_step;
static uint8_t last_activeSample_selected_by_sample_pad;

FLASHMEM void set_apc_velocity_pad(uint8_t inData1)
{
  APC_BUTTONS_RIGHT[0] = false;
  APC_BUTTONS_RIGHT[1] = false;
  APC_BUTTONS_RIGHT[5] = false;
  APC_BUTTONS_RIGHT[6] = false;
  APC_BUTTONS_RIGHT[7] = false;

  if (inData1 > 23 && inData1 < 32) //lower row
  {
    APC_BUTTONS_RIGHT[3] = false;
    APC_BUTTONS_RIGHT[4] = true;
  }
  else if (inData1 > 31 && inData1 < 40) //upper row
  {
    APC_BUTTONS_RIGHT[3] = true;
    APC_BUTTONS_RIGHT[4] = false;
  }
}

FLASHMEM void sub_print_volume_pad(uint8_t in, uint8_t pos, uint8_t col)
{
#if (APC != 1) // regular apc mini mk2
  if (in < 10)
    apc(pos, 0, 6);
  else if (in < 40)
    apc(pos, col, 1);
  else  if (in < 60)
    apc(pos, col, 2);
  else  if (in < 70)
    apc(pos, col, 3);
  else  if (in < 80)
    apc(pos, col, 4);
  else  if (in < 100)
    apc(pos, col, 5);
  else
    apc(pos, col, 6);
#else
  //no brightness for APC Mini Mk1, send only on or off, to avoid freeze at slider move (sometimes still freezes when abusing slider)
  if (in < 10)
    apc(pos, 0, 6);
  else if (in > 40)
    apc(pos, col, 6);
#endif
}

extern void apc_drawChar(int x, unsigned char c);

FLASHMEM void apc_print_volume_pads()
{
  if (apc_scroll_message == false)
  {
    for (uint8_t i = 0; i < 8; i++)  //Velocity Pads
      if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3) //sample or sliced
        sub_print_volume_pad(drum_config[APC_SAMPLE_SCROLL_OFFSET + i - 10].vol_max, i, 5); //5=colour red for Mk2
  }
}

FLASHMEM void apc_clear_grid()
{
  for (uint8_t i = 0; i < 64; i++)
    apc(i, 0, 6);
}

FLASHMEM void apc_clear_right_buttons()
{
  for (uint8_t i = 0; i < 8; i++)
    apc(APC_MINI_BUTTON_RIGHT_FIRST + i, 0, 1); //starting with 112 or 82
}

#ifdef APC_VOLPANSENDDEV
FLASHMEM void apc_clear_bottom_buttons()
{
  for (uint8_t i = 0; i < 8; i++) //clear bottom row under the grid
    apc(APC_MINI_BUTTON_BOTTOM_FIRST + i, 0, 1);
  
  apc_mixmod = APC_EDIT_INIT_INVALID;
}

FLASHMEM void apc_set_bottom_button(int8_t volpansenddev)
{
  int new_mixmod;

  switch(volpansenddev)
  {
    case APC_EDIT_VOLUME:
      if(APC_EDIT_VOLUME <= apc_mixmod && apc_mixmod < APC_EDIT_VOL_ENVATT)
        new_mixmod = (int8_t)apc_mixmod + 1;
      else
        new_mixmod = APC_EDIT_VOLUME;
      break;
    case APC_EDIT_PAN:
      if(APC_EDIT_PAN <= apc_mixmod && apc_mixmod < APC_EDIT_PAN_ENVDECAY)
        new_mixmod = (int8_t)apc_mixmod + 1;
      else
        new_mixmod = APC_EDIT_PAN;
      break;
    case APC_EDIT_SEND_REV:
      if(APC_EDIT_SEND_REV <= apc_mixmod && apc_mixmod < APC_EDIT_SEND_DLAY2)
        new_mixmod = (int8_t)apc_mixmod + 1;
      else
        new_mixmod = APC_EDIT_SEND_REV;
      break;
    case APC_EDIT_DEV_PITCH:
      if(APC_EDIT_DEV_PITCH <= apc_mixmod && apc_mixmod < APC_EDIT_DEV_RESO)
        new_mixmod = (int8_t)apc_mixmod + 1;
      else
        new_mixmod = APC_EDIT_DEV_PITCH;
      break;
    default:
      new_mixmod = (int)apc_mixmod;
      break;
  }
  
  //10 = the step between categories, max 5 (half) enums per category are allowed
  //  in order to detect a change
  if(abs(new_mixmod - (int)apc_mixmod) >= /*5*/ APC_EDIT_PAN/2)
  {
    for(int i=0;i<4;i++)
      if(i == new_mixmod/10)
        apc(APC_MINI_BUTTON_BOTTOM_VOLUME + i, 1, 1); //set corresponding light
      else
        apc(APC_MINI_BUTTON_BOTTOM_VOLUME + i, 0, 1); //clear the others
  }
  else { /* no change of bottom led */ }
  apc_mixmod = (enum_apcmixmod_t)new_mixmod;
}
#endif /* ifdef APC_VOLPANSENDDEV */

FLASHMEM void apc_print_right_buttons()
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (APC_BUTTONS_RIGHT[i])
      apc(APC_MINI_BUTTON_RIGHT_FIRST + i, 1, 1); //starting with 112 or 82
    else
      apc(APC_MINI_BUTTON_RIGHT_FIRST + i, 0, 1); //starting with 112 or 82
  }
}

FLASHMEM void print_apc_source_selection_pads()
{
  if (apc_scroll_message == false)
  {
    if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3)  //drum or sliced
    {
      for (uint8_t i = 0; i < 8; i++)
      {
        if (i == activeSample - APC_SAMPLE_SCROLL_OFFSET + 10 && APC_BUTTONS_RIGHT[6])
        {
          apc(8 + i, 8 + i + APC_SAMPLE_SCROLL_OFFSET, 11); //11 means blinking 1/24 for Mini Mk2
        }
        else
          apc(8 + i, 8 + i + APC_SAMPLE_SCROLL_OFFSET, 6); //6 means full brightness for Mini Mk2
      }
    }
    else  // tonal instrument
    {
      for (uint8_t i = 0; i < 24; i++)
      {
        if (i == temp_int - APC_TONAL_SCROLL_OFFSET && APC_BUTTONS_RIGHT[6])
          apc(i, APC_TONAL_SCROLL_OFFSET + i, 11);
        else
          apc(i, APC_TONAL_SCROLL_OFFSET + i, 6);
      }
    }
  }
}

FLASHMEM void apc_trigger_print_in_lcd(void)
{
  if(trigger_print_sample_or_step)
  {
    if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor)) ||
        (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor)))
    {
      if(trigger_print_sample_or_step == -1) //for new drum sample selected
      {
        print_current_sample_and_pitch_buffer(); //refresh on the right side, on the SAMPLE BUFFER
      }
      else if((trigger_print_sample_or_step >= 1000) && (trigger_print_sample_or_step < 1016)) // for step modified
      {
        if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3)  //drum or sliced
          print_track_steps_detailed(0, CHAR_height * 4 + 3, 254, false, true); //refresh all drum steps, unable to refresh the new step only
        else  // tonal instrument
        { // Inst Track or Chord or Arp
	          /* Disabled because not working, APC-entered-steps are visible only in velocity row, not in piano roll.
	             Also since 1.9.9.0, it doesn't display entered steps in pattern piano roll, with encoder; it works with step record mode with virtual keyboard.
            print_single_pattern_pianoroll_in_pattern_editor(0, DISPLAY_HEIGHT, seq.active_pattern, trigger_print_sample_or_step-1000, true); 
	           */
        }
      }
      else
      {/* Not implemented. This is to refresh the lcd-top-left note, with new note selected in APC, for piano roll. */}
    }
    trigger_print_sample_or_step = 0;
  }
}

FLASHMEM void set_apc_pad_selection(uint8_t pad)
{
  APC_BUTTONS_RIGHT[0] = false;
  APC_BUTTONS_RIGHT[1] = false;
  APC_BUTTONS_RIGHT[3] = false;
  APC_BUTTONS_RIGHT[4] = false;

  if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3) //sample or sliced
  {
    for (uint8_t i = 0; i < 8; i++)
    {
      if (pad - 8 == i)
        activeSample = APC_SAMPLE_SCROLL_OFFSET + i;
      }
    }
  else //tonal instrument
  {
    for (uint8_t i = 0; i < 24; i++)
    {
      if (pad == i)
        temp_int = APC_TONAL_SCROLL_OFFSET + i;
    }
  }
  if (pad == APC_PREVIOUS_NOTE && APC_BUTTONS_RIGHT[6])
  {
    APC_BUTTONS_RIGHT[6] = false;
    APC_BUTTONS_RIGHT[5] = false;
    APC_BUTTONS_RIGHT[7] = false;
  }
  else
    if (APC_BUTTONS_RIGHT[6] == false)
    {
      APC_BUTTONS_RIGHT[6] = true;
      if (seq.content_type[seq.active_pattern] == 1 || seq.content_type[seq.active_pattern] == 2)
      {
        APC_BUTTONS_RIGHT[5] = true;
        APC_BUTTONS_RIGHT[7] = true;
      }
    }
  apc_print_right_buttons();
}

FLASHMEM void apc_fader_control(uint8_t in1, uint8_t in2)
{
  uint8_t old_activeSample;
  for (uint8_t i = 0; i < 8; i++)  //8 fader
  {
    if (in1 - 48 == i)  // fader start with cc 48
    {
      if (APC_BUTTONS_RIGHT[6])  //sample select mode
      {
        old_activeSample = activeSample;
        activeSample = in1 - 48 + APC_SAMPLE_SCROLL_OFFSET - 10;
        if(activeSample < sizeof(drum_config)/sizeof(drum_config[0])) //better check array out of bound, it may cause crash otherwise
        {
          bool refresh_drum;
          if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_drums)) &&
              (last_activeSample_selected_by_sample_pad == activeSample+10)) //10 is the same offset from line above, where activeSample is set. Better would be to compare with the index of sample displayed on screen, not activeSample
            //TODO refresh entire screen if other fader is moved (new activeSample).
            //For now just avoid to display volume for other sample, but even this doesn't work if sample bank is changed.
            refresh_drum = true;
          else refresh_drum = false;

          uint8_t scale_0_100 = (uint8_t)((int32_t)(in2*788)/1000); // translate midi 0-127 to 0-100
#ifdef APC_VOLPANSENDDEV
          switch(apc_mixmod) {
            case APC_EDIT_VOLUME:
              drum_config[activeSample].vol_max = scale_0_100;// max. Volume (0 - 100), translate midi 0-127 to 0-100
              //refresh sample volume on the Drums page, for the selected sample, it's 1st param
              if (refresh_drum) print_small_scaled_bar(0, 3, drum_config[activeSample].vol_max, 0, 200, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_VOL_ENVATT:
              drum_config[activeSample].env_attack = scale_0_100;//  (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 11, drum_config[activeSample].env_attack, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_PAN:
              drum_config[activeSample].pan = (int8_t)((((int32_t)in2 - 64)*1571)/1000);// panning (-99 to 99), translate midi 0-127 to -99..99
              if (refresh_drum) print_small_scaled_bar(0, 4, drum_config[activeSample].pan, -99, 99, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_PAN_ENVHOLD:
              drum_config[activeSample].env_hold = scale_0_100;//  (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 12, drum_config[activeSample].env_hold, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_PAN_ENVDECAY:
              drum_config[activeSample].env_decay = scale_0_100;//  (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 13, drum_config[activeSample].env_decay, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_SEND_REV:
              drum_config[activeSample].reverb_send = scale_0_100;// reverb (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 5, drum_config[activeSample].reverb_send, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_SEND_DLAY1:
              drum_config[activeSample].delay1 = scale_0_100;// delay1 (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 6, drum_config[activeSample].delay1, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_SEND_DLAY2:
              drum_config[activeSample].delay2 = scale_0_100;// delay1 (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 7, drum_config[activeSample].delay2, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_DEV_PITCH:
              drum_config[activeSample].pitch = (uint8_t)((int32_t)(in2*788)/500);// pitch (0 - 200), translate midi 0-127 to 0-200
              if (refresh_drum) print_small_scaled_bar(0, 8, drum_config[activeSample].pitch, 0, 200, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_DEV_CUTOFF:
              drum_config[activeSample].filter_freq = scale_0_100;// Filter cutoff (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 18, drum_config[activeSample].filter_freq, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_DEV_RESO:
              drum_config[activeSample].filter_q = scale_0_100;// Filter reso (0 - 100), translate midi 0-127 to 0-100
              if (refresh_drum) print_small_scaled_bar(0, 19, drum_config[activeSample].filter_q, 0, 100, 1/*default select_id*/, 1, NULL);
              break;
            case APC_EDIT_INIT_INVALID:
            default:
              break;
          }
#else
          drum_config[activeSample].vol_max = scale_0_100;// max. Volume (0 - 100), translate midi 0-127 to 0-100
          //refresh sample volume on the Drums page, for the selected sample, it's 1st param
          if (refresh_drum) print_small_scaled_bar(0, 3, drum_config[activeSample].vol_max, 0, 200, 1/*default select_id*/, 1, NULL);
#endif /* ifdef APC_VOLPANSENDDEV */
        }
        else
          activeSample = old_activeSample; //restore previous valid value
      }
      if (APC_BUTTONS_RIGHT[3]) //edit velocity 1st row (1-8)
      {
        if (seq.vel[seq.active_pattern][in1 - 48] < 210) //normal sample
        {
          seq.vel[seq.active_pattern][in1 - 48] = in2;
          seq_print_single_velocity_step(in1 - 48, GREY1);
        }
      }
      if (APC_BUTTONS_RIGHT[4]) //edit velocity 2nd row (9-16)
      {
        if (seq.vel[seq.active_pattern][in1 - 48 + 8] < 210) //normal sample
        {
          seq.vel[seq.active_pattern][in1 - 48 + 8] = in2;
          seq_print_single_velocity_step(in1 - 48 + 8, GREY1);
        }
      }
    }
  }
  if ((APC_BUTTONS_RIGHT[6])  //sample select mode
      && (activeSample < sizeof(drum_config)/sizeof(drum_config[0]))) //better check array out of bound, it may cause crash otherwise
  {
    apc_print_volume_pads();
    print_apc_source_selection_pads();
  }
}

FLASHMEM void apc_insert_pattern_step(uint8_t inData1)
{
  uint8_t input_step = 0;
  if (inData1 >= 64 - 8)
    input_step = inData1 - 56;
  else
    input_step = inData1 - 48 + 8;

  if (APC_BUTTONS_RIGHT[6] || apc_shift_key)  //insert mode or delete mode with shift key
  {
    insert_pattern_step(input_step);
    seq_printAllSeqSteps();
    seq_printAllVelocitySteps();
  }
  else  // edit mode
  {
    APC_BUTTONS_RIGHT[0] = true;
    APC_BUTTONS_RIGHT[1] = true;
    APC_BUTTONS_RIGHT[3] = false;
    APC_BUTTONS_RIGHT[4] = false;

    if (input_step > 7)
    {
      if (seq.vel[seq.active_pattern][input_step] < 210) //normal sample or tonal
      {
        if (seq.note_data[seq.active_pattern][input_step] - input_step > 0)
        {
          if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3)
            APC_SAMPLE_SCROLL_OFFSET = seq.note_data[seq.active_pattern][input_step] - input_step;
          else
          {
            APC_TONAL_SCROLL_OFFSET = seq.note_data[seq.active_pattern][input_step] - input_step;
            // if (seq.note_data[seq.active_pattern][input_step]==130)
            // apc_latch=true;
          }
        }
      }
      else  //pitched
      {
        //if (seq.note_data[seq.active_pattern][input_step] - input_step > 0)
       // {
          //APC_SAMPLE_SCROLL_OFFSET = 10 +8 - input_step ;

         // activeSample = seq.vel[seq.active_pattern][input_step] +8- input_step;
       //   activeSample = 0;
       // }
      }
    }
    else
    {
      if (seq.vel[seq.active_pattern][input_step] < 210) //normal sample tonal
      {
        if (seq.note_data[seq.active_pattern][input_step] - input_step - 8 > 0)
        {
          if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3)
            APC_SAMPLE_SCROLL_OFFSET = seq.note_data[seq.active_pattern][input_step] - input_step - 8;
          else
            APC_TONAL_SCROLL_OFFSET = seq.note_data[seq.active_pattern][input_step] - input_step - 8;
          // if (seq.note_data[seq.active_pattern][input_step] == 130)
          //   apc_latch = true;
        }
      }
      else  //pitched
      {
        // if (seq.note_data[seq.active_pattern][input_step] - input_step - 8 > 0)
        // {
        //   APC_SAMPLE_SCROLL_OFFSET = 10 - input_step;
        //  // activeSample = seq.vel[seq.active_pattern][input_step] - 210;
        // }
      }
    }
    seq_printAllSeqSteps();
    apc_print_volume_pads();
    pattern_editor_play_current_step(input_step);
  }
  apc_print_right_buttons();
}

FLASHMEM void apc_drawString_Background()
{
  uint8_t len = strlen(apc_scrolltext);
  if (apc_scroll_counter == 0)
  {
    if(apc_scroll_message == true)
      apc_clear_grid();
    else // it's 2 which means no scroll performed, smaller font 3x5 is used
    { // therefore clear only right vertical line, keep bottom lines for eye comfort, also no flicker
      for (uint8_t i = 0; i < 6; i++)
        apc(23 +i * 8, 0, 6);
    }
    delay(50);
  }

  if(apc_scroll_message == true) // scroll text with normal font 5x7, for welcome message
  {
    for (uint8_t i = 0; i < len; i++)
    {
      apc_drawChar(8 + i * 6 - apc_scroll_counter, apc_scrolltext[i]);
    }
    delay(60);
  }
  else // it's 2 which means no scroll performed, because smaller font 3x5 is used
  {
    for (uint8_t i = 0; i < len; i++)
    {
      apc_drawChar(i * 4, apc_scrolltext[i]);
    }
    delay(30); //smaller delay needed when not scrolling
  }

  if (apc_scroll_message && apc_scroll_counter < len * 6 + 8)
    apc_scroll_counter++;
  else
  {
    apc_scroll_message = false;
    apc_scroll_counter = 0;

    delay(50);
    if (APC_MODE == APC_PATTERN_EDITOR)
    {
      apc_clear_grid();
      apc_print_right_buttons();
      seq_printAllSeqSteps();
      seq_printAllVelocitySteps();
      apc_print_volume_pads();
      print_apc_source_selection_pads();
    }

    if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor))
    {
      _seq_pattern_change();
      display.setCursor(11 * CHAR_width_small, CHAR_height * 3 + 3);
      print_content_type();
      virtual_keyboard_smart_preselect_mode();
      update_latch_button();
    }
    else if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor))
    {
      _velocity_editor_change_pattern();
    }
  }
}

FLASHMEM void  apc_print_pattern_number()
{
  snprintf_P(apc_scrolltext, sizeof(apc_scrolltext), PSTR("%02d"), seq.active_pattern);
  apc_scroll_counter = 0;
  apc_scroll_message = 2; // use 2 instead of true, to print pattern number without scrolling it
}

FLASHMEM void  check_and_clear_row3()
{
  if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3)  //drum or sliced
  {
    for (uint8_t i = 0; i < 8; i++)
    {
      apc(16 + i, 0, 6);
    }
  }
}

FLASHMEM void apc_pattern_change(uint8_t inData1)
{
  if (inData1 == APC_MINI_BUTTON_RIGHT_PATTERN_UP) //112 or 82
  {
    seq.active_pattern = constrain(seq.active_pattern + 1, 0, NUM_SEQ_PATTERN - 1);
  }
  else if (inData1 == APC_MINI_BUTTON_RIGHT_PATTERN_DOWN) //113 or 83
  {
    seq.active_pattern = constrain(seq.active_pattern - 1, 0, NUM_SEQ_PATTERN - 1);
  }

  APC_BUTTONS_RIGHT[0] = true;
  APC_BUTTONS_RIGHT[1] = true;

  APC_BUTTONS_RIGHT[3] = false;
  APC_BUTTONS_RIGHT[4] = false;

  APC_BUTTONS_RIGHT[5] = false;
  APC_BUTTONS_RIGHT[6] = false;
  APC_BUTTONS_RIGHT[7] = false;
  //apc_print_right_buttons();
  apc_print_pattern_number();
  check_and_clear_row3();
}
extern void draw_button_on_grid(uint8_t x, uint8_t y, const char* t1, const char* t2, uint8_t color);

FLASHMEM void apc_mute_matrix()
{
  if (APC_MODE == APC_MUTE_MATRIX)
  {
    uint8_t track_count = 0;
    for (uint8_t y = 0; y < 2; y++)
    {
      for (uint8_t x = 0; x < 4; x++)
      {
        // if (seq.current_chain[track_count] != 99)
        // {
        if (seq.track_mute[track_count]) //muted
          apc(64 - 16 + x * 2 - 8 * y * 3, 1, 6);
        else
          apc(64 - 16 + x * 2 - 8 * y * 3, 78, 6);

        if (seq.content_type[seq.current_pattern[track_count]] > 0) // it is a Inst. pattern
        {
          if (seq.note_data[seq.current_pattern[track_count]][seq.step] > 12 && seq.note_data[seq.current_pattern[track_count]][seq.step] != 99)
          {
            if (seq.track_mute[track_count]) //muted
              apc(64 - 8 + x * 2 - 8 * y * 3, 2, 6);
            else
              apc(64 - 8 + x * 2 - 8 * y * 3, 40, 6);
          }
          else
          {
            apc(64 - 8 + x * 2 - 8 * y * 3, 0, 6);
          }
        }
        else    // it is a drum pattern
          if (seq.vel[seq.current_pattern[track_count]][seq.step] < 210) // is Drumtrack and not a pitched sample
          {
            bool found = false;
            for (uint8_t n = 0; n < NUM_DRUMSET_CONFIG - 1; n++)
            {
              if (seq.note_data[seq.current_pattern[track_count]][seq.step] == drum_config[n].midinote)
              {
                found = true;
                if (seq.track_mute[track_count]) //muted
                  apc(64 - 8 + x * 2 - 8 * y * 3, 2, 6);
                else
                  apc(64 - 8 + x * 2 - 8 * y * 3, 124, 6);
                break;
              }
            }
            if (found == false)
            {
              apc(64 - 8 + x * 2 - 8 * y * 3, 0, 6);
            }
          }
          else if (seq.vel[seq.current_pattern[track_count]][seq.step] > 209) // pitched sample
          {
            if (seq.track_mute[track_count]) //muted
              apc(64 - 8 + x * 2 - 8 * y * 3, 2, 6);
            else
              apc(64 - 8 + x * 2 - 8 * y * 3, 36, 6);
          }
        track_count++;
      }
    }
  }
}

FLASHMEM void apc_song()
{
  for (uint8_t x = 0; x < NUM_SEQ_TRACKS; x++)
  {
    for (uint8_t y = 0; y < 16; y++) // visible song steps
    {
      if (seq.song[x][y + seq.scrollpos] < 99)
      {
        if (y == seq.cursor_scroll && x == seq.selected_track && seq.edit_state == false && seq.loop_edit_step == 0 && seq.tracktype_or_instrument_assign == 0)
          apc(64 - 8 + x - (y * 8), seq.song[x][y + seq.scrollpos] * 3 + 1, 11);
        else
          apc(64 - 8 + x - (y * 8), seq.song[x][y + seq.scrollpos] * 3 + 1, 6);
      }
      else
      {
        if (y == seq.cursor_scroll && x == seq.selected_track && seq.edit_state == false && seq.loop_edit_step == 0 && seq.tracktype_or_instrument_assign == 0)
          apc(64 - 8 + x - (y * 8), 5, 11);
        else
          apc(64 - 8 + x - (y * 8), 0, 6);
      }
    }
  }
}

FLASHMEM void apc_NoteOn(byte inChannel, byte inData1, byte inData2)
{

  if (inData1 == APC_MINI_BUTTON_RIGHT_SEQ_START_STOP && !apc_shift_key) //start/stop 114 or 84
  {
    if (!seq.running)
      handleStart();
    else
      handleStop();
  }

  if (inData1 == APC_MINI_BUTTON_RIGHT_PATTERN_EDITOR && apc_shift_key) //switch to pattern editor (112 or 82)
  {
    APC_MODE = APC_PATTERN_EDITOR;
    apc_clear_grid();
    apc_print_right_buttons();
    seq_printAllSeqSteps();
    seq_printAllVelocitySteps();
    apc_print_volume_pads();
    print_apc_source_selection_pads();
#ifdef APC_VOLPANSENDDEV
    apc_set_bottom_button((int8_t)APC_EDIT_VOLUME);
#endif /* ifdef APC_VOLPANSENDDEV */
  }

  else if (inData1 == APC_MINI_BUTTON_RIGHT_SONG_EDITOR && apc_shift_key) //switch to song editor (113 or 83)
  {
    APC_MODE = APC_SONG;
    // LCDML.OTHER_jumpToFunc(UI_func_song);
    //print_chain_matrix_in_song_page();
#ifdef APC_VOLPANSENDDEV
    apc_clear_bottom_buttons();
#endif /* ifdef APC_VOLPANSENDDEV */
    apc_song();
  }

  else if (inData1 == APC_MINI_BUTTON_RIGHT_MUTE_MATRIX && apc_shift_key) //switch to mute matrix 114 or 84
  {
    APC_MODE = APC_MUTE_MATRIX;
    apc_clear_grid();
#ifdef APC_VOLPANSENDDEV
    apc_clear_bottom_buttons();
#endif /* ifdef APC_VOLPANSENDDEV */
    apc_mute_matrix();
  }
  
  if (APC_MODE == APC_MUTE_MATRIX)
  {
    if (inData1 > 23 && inData1 < 64 - 8)
    {
      uint8_t pad_count = 0;
      char buf[4];
      uint8_t spacerx = 90;
      for (uint8_t y = 0; y < 2; y++)
      {
        for (uint8_t x = 0; x < 4; x++)
        {
          if (inData1 == 64 - 16 + x * 2 - 8 * y * 3)
          {
            seq.track_mute[pad_count] = !seq.track_mute[pad_count];
            if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_mute_matrix))
              TouchButton::drawButton(x * spacerx, 95 + y * 55, "TRACK:", itoa(pad_count + 1, buf, 10), seq.track_mute[pad_count] ? TouchButton::BUTTON_NORMAL : TouchButton::BUTTON_ACTIVE);
          }
          pad_count++;
        }
      }
    }
    apc_mute_matrix();
  }
  else if (APC_MODE == APC_PATTERN_EDITOR)
  {
    if (inData1 == APC_MINI_BUTTON_BOTTOM_SAMPLE_P1) //track8 pad++ , 107 or 71
    {
      if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3) //sample or sliced
      {
        APC_SAMPLE_SCROLL_OFFSET++;
      }
      else
      {
        APC_TONAL_SCROLL_OFFSET++;
      }
      inChannel = 99;
    }
    else if (inData1 == APC_MINI_BUTTON_BOTTOM_SAMPLE_M1) //track7 pad-- , 106 or 70
    {
      if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3) //sample or sliced
      {
        if (APC_SAMPLE_SCROLL_OFFSET > 10)
          APC_SAMPLE_SCROLL_OFFSET--;
      }
      else
      {
        if (APC_TONAL_SCROLL_OFFSET > 10)
          APC_TONAL_SCROLL_OFFSET--;
      }
      inChannel = 99;
    }
    else if (inData1 == APC_MINI_BUTTON_BOTTOM_SAMPLE_M8) //track8 pad -8 , 105 or 69
    {
      if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3) //sample or sliced
      {
        if (APC_SAMPLE_SCROLL_OFFSET - 8 > 9)
          APC_SAMPLE_SCROLL_OFFSET = APC_SAMPLE_SCROLL_OFFSET - 8;
        else
          APC_SAMPLE_SCROLL_OFFSET = 10;
      }
      else
      {
        if (APC_TONAL_SCROLL_OFFSET - 8 > 9)
          APC_TONAL_SCROLL_OFFSET = APC_TONAL_SCROLL_OFFSET - 8;
        else
          APC_TONAL_SCROLL_OFFSET = 10;
      }
      inChannel = 99;
    }
    else if (inData1 == APC_MINI_BUTTON_BOTTOM_SAMPLE_P8) //track7 pad +8 , 104 or 68
    {
      if (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3) //sample or sliced
      {
        APC_SAMPLE_SCROLL_OFFSET = APC_SAMPLE_SCROLL_OFFSET + 8;
      }
      else
      {
        APC_TONAL_SCROLL_OFFSET = APC_TONAL_SCROLL_OFFSET + 8;
      }
      inChannel = 99;
    }
#ifdef APC_VOLPANSENDDEV
    else if (inData1 == APC_MINI_BUTTON_BOTTOM_VOLUME) //Volume edit
      apc_set_bottom_button((int8_t)APC_EDIT_VOLUME);
    else if (inData1 == APC_MINI_BUTTON_BOTTOM_PAN) //Pan edit
      apc_set_bottom_button((int8_t)APC_EDIT_PAN);
    else if (inData1 == APC_MINI_BUTTON_BOTTOM_SEND) //Send edit
      apc_set_bottom_button((int8_t)APC_EDIT_SEND_REV); // REVERB always the 1st as ref
    else if (inData1 == APC_MINI_BUTTON_BOTTOM_DEVICE) //Device edit
      apc_set_bottom_button((int8_t)APC_EDIT_DEV_PITCH); //PITCH always the 1st as ref
#endif /* ifdef APC_VOLPANSENDDEV */

    if (APC_SAMPLE_SCROLL_OFFSET < 10)
      APC_SAMPLE_SCROLL_OFFSET = 10;
    else if (APC_SAMPLE_SCROLL_OFFSET >= NUM_DRUMSET_CONFIG + 1)
      APC_SAMPLE_SCROLL_OFFSET = NUM_DRUMSET_CONFIG + 1;

    if (APC_TONAL_SCROLL_OFFSET < 10)
      APC_TONAL_SCROLL_OFFSET = 10;
    // if (apc_latch == false)
    // {
    if (APC_TONAL_SCROLL_OFFSET >= 160)
      APC_TONAL_SCROLL_OFFSET = 160;
    // }

    if (inData1 == APC_MINI_BUTTON_RIGHT_PATTERN_UP || inData1 == APC_MINI_BUTTON_RIGHT_PATTERN_DOWN) //pattern change 112 or 82 / 113 or 83
      if (!apc_shift_key)
        apc_pattern_change(inData1);

    if (inData1 == APC_MINI_BUTTON_RIGHT_VELOCITY_UPPER || inData1 == APC_MINI_BUTTON_RIGHT_VELOCITY_LOWER) // 115 or 85 / 116 or 86
    {
      APC_BUTTONS_RIGHT[0] = false;
      APC_BUTTONS_RIGHT[1] = false;
      APC_BUTTONS_RIGHT[6] = false;
    }
    if (inData1 == APC_MINI_BUTTON_RIGHT_VELOCITY_UPPER) //velocity upper row 115 or 85
    {
      APC_BUTTONS_RIGHT[3] = true;
      APC_BUTTONS_RIGHT[4] = false;
    }
    else if (inData1 == APC_MINI_BUTTON_RIGHT_VELOCITY_LOWER) //velocity lower row 116 or 86
    {
      APC_BUTTONS_RIGHT[3] = false;
      APC_BUTTONS_RIGHT[4] = true;
    }
    else  if (inData1 == APC_MINI_BUTTON_RIGHT_ENABLE_PADSEL || inData1 == APC_MINI_BUTTON_RIGHT_DISAB1_PADSEL || inData1 == APC_MINI_BUTTON_RIGHT_DISAB2_PADSEL) //enable/disable pad select mode 118/117/119
    {
      if ((APC_BUTTONS_RIGHT[6] && inData1 == APC_MINI_BUTTON_RIGHT_ENABLE_PADSEL/* 118*/) || (APC_BUTTONS_RIGHT[6] && (seq.content_type[seq.active_pattern] == 1 || seq.content_type[seq.active_pattern] == 2)))
      {
        APC_BUTTONS_RIGHT[5] = false;
        APC_BUTTONS_RIGHT[6] = false;
        APC_BUTTONS_RIGHT[7] = false;
      }
      else
        if (APC_PREVIOUS_NOTE != 254)
        {
          if (inData1 == APC_MINI_BUTTON_RIGHT_ENABLE_PADSEL) //118 or 88
            APC_BUTTONS_RIGHT[6] = true;

          if (seq.content_type[seq.active_pattern] == 1 || seq.content_type[seq.active_pattern] == 2)
          {
            APC_BUTTONS_RIGHT[5] = true;
            APC_BUTTONS_RIGHT[6] = true;
            APC_BUTTONS_RIGHT[7] = true;
          }
          APC_BUTTONS_RIGHT[0] = false;
          APC_BUTTONS_RIGHT[1] = false;
          APC_BUTTONS_RIGHT[3] = false;
          APC_BUTTONS_RIGHT[4] = false;
        }
    }
    else if ((inData1 > 7 && inData1 < 16) && (seq.content_type[seq.active_pattern] == 0 || seq.content_type[seq.active_pattern] == 3))
    {
      inChannel = drum_midi_channel;
      set_apc_pad_selection(inData1);
      APC_PREVIOUS_NOTE = inData1;
      apc_print_volume_pads();
      trigger_print_sample_or_step = -1; //new sample selected for pattern drum mode, to be refreshed on lcd
      last_activeSample_selected_by_sample_pad = activeSample; //needed to avoid update of volume gauge, when other fader is moved, in Drums screen
    }
    else if (inData1 < 24 && seq.content_type[seq.active_pattern] == 1) // tonal instrument type
    {
      set_apc_pad_selection(inData1);
      APC_PREVIOUS_NOTE = inData1;
      apc_print_volume_pads();
      trigger_print_sample_or_step = temp_int; //new note selected for pattern piano roll mode, TODO to be refreshed on lcd
    }
    else if ((inData1 >= 64 - 16 && inData1 < 64)) //step input to pattern . LUC PERHAPS CORRECT IS <64 AND NOT <=64
    {
      apc_insert_pattern_step(inData1);
      if (APC_BUTTONS_RIGHT[0] || APC_BUTTONS_RIGHT[6] || APC_BUTTONS_RIGHT[3] || APC_BUTTONS_RIGHT[4])
        inChannel = 99;
      trigger_print_sample_or_step = 1000 + inData1 - (64 - 16); //new step entered/deleted, 1st step=1000, 2nd=1001 ..., to be refreshed on lcd
    }
    else if (inData1 > 23 && inData1 < 40) // velocity steps
    {
      inChannel = drum_midi_channel;
      set_apc_velocity_pad(inData1);
      if (APC_BUTTONS_RIGHT[3] || APC_BUTTONS_RIGHT[4])
        inChannel = 99;

      seq_printAllVelocitySteps();
    }
    else if ((inData1 > (APC_MINI_BUTTON_BOTTOM_FIRST + 3) && inData1 < (APC_MINI_BUTTON_BOTTOM_FIRST + 8))) //update scrolling pad view 103 or 67 / 108 or 72
    {
      apc_print_volume_pads();
    }
    else
    {
      inChannel = 99;
    }
    apc_out_channel = inChannel;
  }
}

#endif
