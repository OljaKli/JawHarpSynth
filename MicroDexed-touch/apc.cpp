#if defined APC

#include <Arduino.h>
#include "apc.h"
#include "config.h"
#include "drums.h"
#include "sequencer.h"
#include <LCDMenuLib2.h>
#include "ILI9341_t3n.h"
#include "UI.h"

extern void apc(uint8_t a, uint8_t b, uint8_t c);
extern drum_config_t drum_config[NUM_DRUMSET_CONFIG];
extern uint8_t activeSample;
extern uint8_t drum_midi_channel;
extern uint8_t APC_SCROLL_OFFSET;
extern uint8_t APC_PREVIOUS_NOTE;
extern bool APC_BUTTONS_RIGHT[8];
extern void seq_printAllVelocitySteps();
extern void seq_printAllSeqSteps();
extern void pattern_editor_play_current_step(uint8_t step);
extern void insert_pattern_step(uint8_t step);
extern void  _seq_pattern_change();
extern void  print_content_type();
extern void update_latch_button();
extern void virtual_keyboard_smart_preselect_mode();
extern void _velocity_editor_change_pattern();
extern void handleStart();
extern void handleStop();
extern LCDMenuLib2 LCDML;
extern ILI9341_t3n display;
extern sequencer_t seq;

FLASHMEM void set_apc_velocity_pad(uint8_t inData1)
{
  APC_BUTTONS_RIGHT[0] = false;
  APC_BUTTONS_RIGHT[1] = false;
  APC_BUTTONS_RIGHT[6] = false;

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
}


FLASHMEM void apc_print_sample_volume_pads()
{
  for (uint8_t i = 0; i < 8; i++)  //Velocity Pads
    sub_print_volume_pad(drum_config[APC_SCROLL_OFFSET + i - 10].vol_max * 100, i, 5);
}

FLASHMEM void apc_clear_grid()
{
  for (uint8_t i = 0; i < 64; i++)
    apc(i, 0, 6);
}

FLASHMEM void apc_clear_right_buttons()
{
  for (uint8_t i = 0; i < 8; i++)
    apc(112 + i, 0, 1);
}

FLASHMEM void apc_print_right_buttons()
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (APC_BUTTONS_RIGHT[i])
      apc(112 + i, 1, 1);
    else
      apc(112 + i, 0, 1);
  }
}


FLASHMEM void print_apc_sample_selection_pads()
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (i == activeSample - APC_SCROLL_OFFSET + 10 && APC_BUTTONS_RIGHT[6])
    {
      apc(8 + i, 72 + i - 64 + APC_SCROLL_OFFSET, 11);
    }
    else
      apc(8 + i, 72 + i - 64 + APC_SCROLL_OFFSET, 6);
  }
}

FLASHMEM void set_apc_sample_selection(uint8_t pad)
{
  APC_BUTTONS_RIGHT[0] = false;
  APC_BUTTONS_RIGHT[1] = false;
  APC_BUTTONS_RIGHT[3] = false;
  APC_BUTTONS_RIGHT[4] = false;

  for (uint8_t i = 0; i < 8; i++)
  {
    if (pad - APC_SCROLL_OFFSET == i)
    {
      activeSample = pad - APC_SCROLL_OFFSET;
    }
  }
  if (pad == APC_PREVIOUS_NOTE && APC_BUTTONS_RIGHT[6])
  {
    APC_BUTTONS_RIGHT[6] = false;
  }
  else
    if (APC_BUTTONS_RIGHT[6] == false)
    {
      APC_BUTTONS_RIGHT[6] = true;
    }
  apc_print_right_buttons();
}


FLASHMEM void apc_fader_control(uint8_t in1, uint8_t in2)
{
  for (uint8_t i = 0; i < 8; i++)  //8 fader
  {
    if (in1 - 48 == i)  // fader start with cc 48
    {
      if (APC_BUTTONS_RIGHT[6])  //sample select mode
      {
        //sub_print_volume_pad(in2, i, 5);
        activeSample = in1 - 48 + APC_SCROLL_OFFSET - 10;
        drum_config[activeSample].vol_max = (float)in2 / 100;
      }
      if (APC_BUTTONS_RIGHT[3])
      {
        seq.vel[seq.active_pattern][in1 - 48] = in2;
        seq_printAllVelocitySteps();
      }
      if (APC_BUTTONS_RIGHT[4])
      {
        seq.vel[seq.active_pattern][in1 - 48 + 8] = in2;
        seq_printAllVelocitySteps();
      }
    }
  }
  if (APC_BUTTONS_RIGHT[6])
  {
    apc_print_sample_volume_pads();
    print_apc_sample_selection_pads();
  }
}

void  apc_insert_pattern_step(uint8_t inData1)
{
  uint8_t input_step = 0;

  if (inData1 >= 64 - 8)
    input_step = inData1 - 56;
  else
    input_step = inData1 - 48 + 8;

  if (APC_BUTTONS_RIGHT[6])  //insert mode
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
      if (seq.note_data[seq.active_pattern][input_step] - input_step > 0)
        APC_SCROLL_OFFSET = seq.note_data[seq.active_pattern][input_step] - input_step;
    }
    else
    {
      if (seq.note_data[seq.active_pattern][input_step] - input_step - 8 > 0)
        APC_SCROLL_OFFSET = seq.note_data[seq.active_pattern][input_step] - input_step - 8;
    }
    // handleNoteOn(drum_midi_channel, seq.note_data[seq.active_pattern][input_step], seq.vel[seq.active_pattern][input_step], 0);
    seq_printAllSeqSteps();
    apc_print_sample_volume_pads();
    pattern_editor_play_current_step(input_step);
  }
  apc_print_right_buttons();
}

FLASHMEM void  apc_pattern_change(uint8_t inData1)
{
  if (inData1 == 112)
  {
    seq.active_pattern = constrain(seq.active_pattern + 1, 0, NUM_SEQ_PATTERN - 1);
  }
  else if (inData1 == 113)
  {
    seq.active_pattern = constrain(seq.active_pattern - 1, 0, NUM_SEQ_PATTERN - 1);
  }
  APC_BUTTONS_RIGHT[6] = false;
  APC_BUTTONS_RIGHT[3] = false;
  APC_BUTTONS_RIGHT[4] = false;
  APC_BUTTONS_RIGHT[0] = true;
  APC_BUTTONS_RIGHT[1] = true;

  apc_print_right_buttons();
  seq_printAllSeqSteps();
  seq_printAllVelocitySteps();
  apc_print_sample_volume_pads();

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

void apc_NoteOn(byte inChannel, byte inData1, byte inData2)
{

  if (inData1 == 107) //track8 pad++
  {
    APC_SCROLL_OFFSET++;
    inChannel = 99;
  }
  else if (inData1 == 106) //track7 pad--
  {
    if (APC_SCROLL_OFFSET > 10)
      APC_SCROLL_OFFSET--;
    inChannel = 99;
  }
  else if (inData1 == 105) //track8 pad -8
  {
    if (APC_SCROLL_OFFSET - 8 > 9)
      APC_SCROLL_OFFSET = APC_SCROLL_OFFSET - 8;
    else
      APC_SCROLL_OFFSET = 10;
    inChannel = 99;
  }
  else if (inData1 == 104) //track7 pad +8
  {
    APC_SCROLL_OFFSET = APC_SCROLL_OFFSET + 8;
    inChannel = 99;
  }

  if (APC_SCROLL_OFFSET < 10)
    APC_SCROLL_OFFSET = 10;
  else if (APC_SCROLL_OFFSET >= NUM_DRUMSET_CONFIG + 1)
    APC_SCROLL_OFFSET = NUM_DRUMSET_CONFIG + 1;

  if ((inData1 >= 64 - 16 && inData1 <= 64)) //step input to pattern
  {
    apc_insert_pattern_step(inData1);
    if (APC_BUTTONS_RIGHT[0] || APC_BUTTONS_RIGHT[6] || APC_BUTTONS_RIGHT[3] || APC_BUTTONS_RIGHT[4])
      inChannel = 99;
  }

  else  if (inData1 == 112 || inData1 == 113) //pattern change
    apc_pattern_change(inData1);

  if (inData1 == 115 || inData1 == 116)
  {
    APC_BUTTONS_RIGHT[0] = false;
    APC_BUTTONS_RIGHT[1] = false;
    APC_BUTTONS_RIGHT[6] = false;
  }

  if (inData1 == 115) //velocity upper row
  {
    APC_BUTTONS_RIGHT[3] = true;
    APC_BUTTONS_RIGHT[4] = false;
  }
  else if (inData1 == 116) //velocity lower row
  {
    APC_BUTTONS_RIGHT[3] = false;
    APC_BUTTONS_RIGHT[4] = true;

  }

  else  if (inData1 == 118) //enable/disable sample select mode
  {
    if (APC_BUTTONS_RIGHT[6])
    {
      APC_BUTTONS_RIGHT[6] = false;
    }

    else
      if (APC_PREVIOUS_NOTE != 254)
      {
        APC_BUTTONS_RIGHT[6] = true;
        APC_BUTTONS_RIGHT[0] = false;
        APC_BUTTONS_RIGHT[1] = false;
        APC_BUTTONS_RIGHT[3] = false;
        APC_BUTTONS_RIGHT[4] = false;
      }
  }
  else if (inData1 == 119) //start/stop
  {
    if (!seq.running)
      handleStart();
    else
      handleStop();
  }

  else if (inData1 > 7 && inData1 < 16) // drum pad select row
  {
    inChannel = drum_midi_channel;
    inData1 = inData1 + APC_SCROLL_OFFSET;
    set_apc_sample_selection(inData1);
    APC_PREVIOUS_NOTE = inData1;
    apc_print_sample_volume_pads();
  }

  else if (inData1 > 23 && inData1 < 40) // velocity steps
  {
    inChannel = drum_midi_channel;
    //inData1 = inData1 + APC_SCROLL_OFFSET;
    set_apc_velocity_pad(inData1);
    if (APC_BUTTONS_RIGHT[3] || APC_BUTTONS_RIGHT[4])
      inChannel = 99;

    seq_printAllVelocitySteps();
  }

  else if ((inData1 > 103 && inData1 < 108)) //update scrolling pad view
  {
    apc_print_sample_volume_pads();
  }
  else
  {
    // inData1 = 0;
    inChannel = 99;
  }
}

#endif