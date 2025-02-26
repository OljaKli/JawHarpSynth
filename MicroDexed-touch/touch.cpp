#include "touch.h"
#include "sequencer.h"
#include <LCDMenuLib2.h>
#include "ILI9341_t3n.h"
#include <Audio.h>
#include "template_mixer.hpp"
#include "UI.h"
#include "touchbutton.h"
#include "virtualkeyboard.h"

extern ILI9341_t3n display;
extern config_t configuration;
extern uint8_t selected_instance_id;
extern LCDMenuLib2 LCDML;
extern bool remote_touched;
extern sequencer_t seq;
extern void handleStop(void);
extern void handleStart(void);

extern uint8_t activeSample;
extern bool generic_full_draw_required;
extern bool mb_solo_low;
extern bool mb_solo_mid;
extern bool mb_solo_upper_mid;
extern bool mb_solo_high;
extern bool multiband_active;
extern uint8_t generic_active_function;
extern uint8_t generic_temp_select_menu;
extern uint8_t last_menu_depth;

dexed_live_mod_t dexed_live_mod; // dexed quick live modifiers for attack and release
extern int temp_int;

ts_t ts;                         // touch screen
fm_t fm;                         // file manager
bool loop_start_button = false;
bool loop_end_button = false;
bool isButtonTouched = false;
int numTouchPoints = 0;

extern bool wakeScreenFlag;

#if defined APC
extern void apc_mute_matrix();
#endif

TouchFn currentTouchHandler;
FLASHMEM void registerTouchHandler(TouchFn touchFn) {
  currentTouchHandler = touchFn;
}

FLASHMEM void unregisterTouchHandler(void) {
  currentTouchHandler = 0;
}

TouchFn getCurrentTouchHandler(void) {
  return currentTouchHandler;
}

FLASHMEM void updateTouchScreen() {
  if (remote_touched) {
    numTouchPoints = 1;
  }
  else {
    // no remote touch, so update to check for real touch
    numTouchPoints = touch.touched();
    if (numTouchPoints > 0) {

#if defined GENERIC_DISPLAY    
      // Scale from ~0->4000 to tft
      ts.p = touch.getPoint();
      ts.p.x = map(ts.p.x, 205, 3860, 0, TFT_HEIGHT);
      ts.p.y = map(ts.p.y, 310, 3720, 0, TFT_WIDTH);
#endif

#ifdef CAPACITIVE_TOUCH_DISPLAY
      // Retrieve a point
      TS_Point p = touch.getPoint();

      switch (configuration.sys.touch_rotation) {
      case 1: //damster capacitive touch rotation (1)
        ts.p.x = p.y;
        ts.p.y = DISPLAY_HEIGHT - p.x;
        break;

      case 0: //positionhigh capacitive touch rotation (0)
      default:// in case configuration.sys.touch_rotation in config-file has stored 2 or 3 from the old screen, better behave like new default for now
        ts.p.x = DISPLAY_WIDTH - p.y;
        ts.p.y = p.x;
        break;
      }
#endif
    }
    else {
      isButtonTouched = false;
    }
  }
  wakeScreenFlag |= (numTouchPoints > 0);
}

FLASHMEM void helptext_l(const char* str)
{
  display.setTextSize(1);
  uint8_t l = strlen(str);

  display.setCursor(0, DISPLAY_HEIGHT - CHAR_height_small);
  display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
  display.print(str);

  if (l < ts.old_helptext_length[0])
  {
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    for (uint8_t x = 0; x < ts.old_helptext_length[0] - l; x++)
      display.print(" ");
  }

  ts.old_helptext_length[0] = l;
}
extern void clear_bottom_half_screen_without_backbutton();

FLASHMEM void draw_back_touchbutton()
{
  if (ts.keyb_in_menu_activated == false)
  {
    // back text can be drawn as a touch button
    if (LCDML.MENU_getLayer() != 0 && LCDML.FUNC_getID() == 255) //not in root menu but in menu
    {
      TouchButton::drawButton(GRID.X[0], GRID.Y[5], "GO", back_text, TouchButton::BUTTON_NORMAL);
    }
    else
      if (LCDML.MENU_getLayer() == 0 && LCDML.FUNC_getID() == 255) // root menu, no where to go back
      {
        TouchButton::clearButton(GRID.X[0], GRID.Y[5], COLOR_BACKGROUND);

      }
  }
  if (legacy_touch_button_back_page() && (LCDML.FUNC_getID() > _LCDML_DISP_cnt))
  {
    //remove unusable touch buttons from screen
    clear_bottom_half_screen_without_backbutton();
    //draw_button_on_grid(2, 25, "GO", back_text, 0);  //the back button should already be there

  }
  //else if (legacy_touch_button_back_page()&& ts.keyb_in_menu_activated  )
  else if (legacy_touch_button_back_page())
  { // but when virtual keyboard was active when coming from main menu, it must be redrawn
    TouchButton::drawButton(GRID.X[0], GRID.Y[5], "GO", back_text, TouchButton::BUTTON_NORMAL);
  }
  else if (touch_button_back_page() && seq.cycle_touch_element != 1)
  {
    TouchButton::drawButton(GRID.X[0], GRID.Y[5], "GO", back_text, TouchButton::BUTTON_NORMAL);
  }
}

FLASHMEM void helptext_r(const char* str)
{
  display.setTextSize(1);
  uint8_t l = strlen(str);
  display.setCursor(DISPLAY_WIDTH - CHAR_width_small * l, DISPLAY_HEIGHT - CHAR_height_small);
  display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
  display.print(str);
  if (l < ts.old_helptext_length[1])
  {
    display.setCursor(DISPLAY_WIDTH - CHAR_width_small * (ts.old_helptext_length[1]), DISPLAY_HEIGHT - CHAR_height_small);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    for (uint8_t x = 0; x < ts.old_helptext_length[1] - l; x++)
      display.print(" ");
  }
  ts.old_helptext_length[1] = l;
}

FLASHMEM void helptext_c(const char* str)
{
  display.setTextSize(1);
  uint8_t l = strlen(str);
  display.setCursor(DISPLAY_WIDTH / 2 - (l / 2) * CHAR_width_small, DISPLAY_HEIGHT - CHAR_height_small);
  display.setTextColor(COLOR_SYSTEXT, DX_DARKCYAN);
  display.print(str);
  if (l < ts.old_helptext_length[2])
  {
    display.setCursor(DISPLAY_WIDTH / 2 - (ts.old_helptext_length[2] / 2) * CHAR_width_small, DISPLAY_HEIGHT - CHAR_height_small);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    for (uint8_t x = 0; x < ts.old_helptext_length[2] - l; x++)
      display.print(" ");
  }
  ts.old_helptext_length[2] = l;
}

FLASHMEM uint16_t RGB24toRGB565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r / 8) << 11) | ((g / 4) << 5) | (b / 8);
}

FLASHMEM uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val)
{
  // hue: 0-359, sat: 0-255, val (lightness): 0-255
  int r = 0, g = 0, b = 0, base;

  base = ((255 - sat) * val) >> 8;
  switch (hue / 60)
  {
  case 0:
    r = val;
    g = (((val - base) * hue) / 60) + base;
    b = base;
    break;
  case 1:
    r = (((val - base) * (60 - (hue % 60))) / 60) + base;
    g = val;
    b = base;
    break;
  case 2:
    r = base;
    g = val;
    b = (((val - base) * (hue % 60)) / 60) + base;
    break;
  case 3:
    r = base;
    g = (((val - base) * (60 - (hue % 60))) / 60) + base;
    b = val;
    break;
  case 4:
    r = (((val - base) * (hue % 60)) / 60) + base;
    g = base;
    b = val;
    break;
  case 5:
    r = val;
    g = base;
    b = (((val - base) * (60 - (hue % 60))) / 60) + base;
    break;
  }
  return RGB24toRGB565(r, g, b);
}

FLASHMEM bool check_button_on_grid(uint8_t x, uint8_t y)
{
  bool result = false;
  if (ts.p.x > x * CHAR_width_small && ts.p.x < (x + button_size_x) * CHAR_width_small && ts.p.y > y * CHAR_height_small && ts.p.y < (y + button_size_y) * CHAR_height_small) {
    if (isButtonTouched == false) {
      isButtonTouched = true;
      result = true;
    }
  }
  return result;
}

FLASHMEM void print_current_chord()
{
  for (uint8_t x = 0; x < 7; x++)
  {
    display.print(seq.chord_names[seq.arp_chord][x]);
  }
}

extern void sub_step_recording(bool touchinput, uint8_t touchparam);
extern void play_sample_off_virtual_drumpads(uint8_t note);
extern void UI_func_livesequencer(uint8_t param);

extern uint8_t dexed_onscreen_algo;

FLASHMEM void print_perf_modifier_buttons() {
  print_perfmod_buttons();
  print_perfmod_lables();
  delay(100);
}

extern void draw_algo();
extern uint8_t get_algo();
extern uint8_t set_algo(uint8_t value);
extern void panic_dexed_current_instance();

FLASHMEM void handle_touchscreen_voice_select()
{
  if (numTouchPoints > 0)
  {
    if (check_button_on_grid(45, 1))
    {
      if (seq.cycle_touch_element == 1)
      {
        border3_large_clear();
        seq.cycle_touch_element = 0;
        // display.drawRect(DISPLAY_WIDTH / 2, CHAR_height * 6 - 4 , DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - 1,  GREY4);
        draw_button_on_grid(45, 1, "", "", 99); // print keyboard icon
        dexed_onscreen_algo = 88; //dummy value to force draw on screen init
        print_voice_settings_in_dexed_voice_select(true, true);
        print_perfmod_buttons();
        print_perfmod_lables();
        print_voice_select_default_help();
      }
      else
      {
        border3_large_clear();
        seq.cycle_touch_element = 1;
        draw_button_on_grid(45, 1, "DEXED", "DETAIL", 0);
        drawVirtualKeyboard();
      }
    }
    if (check_button_on_grid(37, 1))
    {
      save_favorite(configuration.dexed[selected_instance_id].pool, configuration.dexed[selected_instance_id].bank, configuration.dexed[selected_instance_id].voice, selected_instance_id);
    }
    else if (check_button_on_grid(45, 11)) //toggle fav. preset search modes
    {
      configuration.sys.favorites++;
      if (configuration.sys.favorites > 3)
        configuration.sys.favorites = 0;
      print_voice_select_fav_search_button();
    }
    else {
      if (seq.cycle_touch_element != 1)
      {
        if (check_button_on_grid(37, 23)) //algo
        {
          uint8_t value = get_algo();

          value++;
          if (value > 31)
            value = 0;
          panic_dexed_current_instance();
          set_algo(value);
          draw_algo();
        }

        else  if (check_button_on_grid(20, 23)) {
          reset_live_modifiers();
          print_perf_modifier_buttons();
        }
        else if (check_button_on_grid(45, 23))
          LCDML.OTHER_jumpToFunc(UI_func_voice_editor);

        else if (check_button_on_grid(2, 23))
        {
          if (dexed_live_mod.active_button != 1)
            dexed_live_mod.active_button = 1;
          else
            dexed_live_mod.active_button = 0;
          print_perf_modifier_buttons();
        }
        else if (check_button_on_grid(11, 23))
        {
          if (dexed_live_mod.active_button != 2)
            dexed_live_mod.active_button = 2;
          else
            dexed_live_mod.active_button = 0;
          print_perf_modifier_buttons();
        }
        else //if touched somewhere else reset modifier edit mode
        {
          dexed_live_mod.active_button = 0;
          print_perf_modifier_buttons();
        }

        if (dexed_live_mod.active_button > 0 && dexed_live_mod.active_button < 5)
        {
          helptext_r("< > CHANGE MODIFIER VALUE");
          display.setCursor(0, DISPLAY_HEIGHT - (CHAR_height_small * 2) - 2);
          print_empty_spaces(38,1);
          display.setCursor(9 * CHAR_width_small, DISPLAY_HEIGHT - CHAR_height_small * 1);
          print_empty_spaces(9,1);
          display.setCursor(CHAR_width_small * 38 + 2, DISPLAY_HEIGHT - (CHAR_height_small * 2) - 2);
          display.print(F(" PUSH TO RETURN"));
        }
        else
        {
          print_voice_select_default_help();
        }
        print_voice_settings_in_dexed_voice_select(false, true);
      }
    }
  }
  if (seq.cycle_touch_element == 1) {
    handleTouchVirtualKeyboard();
  }
}


FLASHMEM void update_latch_button()
{
  if (seq.cycle_touch_element == 1)
  {
    if (seq.content_type[seq.active_pattern] == 0)
      draw_button_on_grid(36, 6, "LATCH", "NOTE", 98);
    else
      draw_button_on_grid(36, 6, "LATCH", "NOTE", 1);
  }
}

FLASHMEM void update_step_rec_buttons()
{
  if (seq.cycle_touch_element == 1)
  {
    if (seq.content_type[seq.active_pattern] == 0)
      draw_button_on_grid(36, 6, "LATCH", "NOTE", 98);
    else
      draw_button_on_grid(36, 6, "LATCH", "NOTE", 1);

    draw_button_on_grid(45, 6, "EMPTY", "STEP", 1);

    if (seq.auto_advance_step == 1)
      draw_button_on_grid(45, 11, "AUTO", "ADV.", 1); // print step recorder icon
    else if (seq.auto_advance_step == 2)
      draw_button_on_grid(45, 11, "A.ADV.", ">STOP", 1); // print step recorder icon
    else
      draw_button_on_grid(45, 11, "KEEP", "STEP", 1); // print step recorder icon
  }
  if (seq.step_recording)
  {
    draw_button_on_grid(36, 1, "RECORD", "ACTIVE", 2); // print step recorder icon
  }
  else
  {
    draw_button_on_grid(36, 1, "STEP", "RECORD", 1); // print step recorder icon
  }
}

FLASHMEM void handle_touchscreen_pattern_editor()
{
  if (numTouchPoints > 0)
  {
    if (seq.cycle_touch_element == 1)
    {
      if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor)) ||
        (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor)))
      {
        if (ts.p.y > 7 * CHAR_height_small && ts.p.y < 12 * CHAR_height_small + 20 && ts.p.x < 230)
        {
          ts.virtual_keyboard_velocity = ts.p.x - 70;
          if (ts.p.x - 70 < 1)
            ts.virtual_keyboard_velocity = 0;
          else
            if (ts.virtual_keyboard_velocity > 127)
              ts.virtual_keyboard_velocity = 127;
          virtual_keyboard_print_velocity_bar();
        }
      }
      if ((LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor)) ||
        (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_vel_editor)))
      {
        if (check_button_on_grid(45, 10) && seq.step_recording)
        {
          seq.auto_advance_step++;
          if (seq.auto_advance_step > 2)
            seq.auto_advance_step = 0;
          update_step_rec_buttons();
        }
      }
    }
    if (check_button_on_grid(36, 1) && seq.running == false)
    {
      seq.note_in = 0;
      seq.step_recording = !seq.step_recording;
      update_step_rec_buttons();
      virtual_keyboard_print_velocity_bar();
    }
    else if (check_button_on_grid(45, 1))
    {
      border3_large();
      border3_large_clear();
      if (seq.cycle_touch_element == 1)
      {
        seq.cycle_touch_element = 0;
        draw_button_on_grid(45, 1, "", "", 99); // print keyboard icon
        display.console = true;
        display.fillRect(0, CHAR_height_small * 12 + 1, DISPLAY_WIDTH, 1, COLOR_BACKGROUND);
        display.console = true;
        display.fillRect(215, 48, 97, 42, COLOR_BACKGROUND);
        display.console = false;
        seq_pattern_editor_update_dynamic_elements();
      }
      else
      {
        seq.cycle_touch_element = 1;
        draw_button_on_grid(45, 1, back_text, "TO SEQ", 0);
        display.console = true;
        display.fillRect(216, CHAR_height_small * 6, 95, CHAR_height_small * 6 + 1, COLOR_BACKGROUND);
        display.console = true;
        display.fillRect(0, CHAR_height_small * 10 + 1, 195, CHAR_height_small * 2 + 1, COLOR_BACKGROUND);
        display.console = true;
        display.fillRect(0, CHAR_height_small * 10 + 1, 2, CHAR_height_small * 12 + 1, COLOR_BACKGROUND);
        display.console = false;
        update_step_rec_buttons();
        virtual_keyboard_print_velocity_bar();
        drawVirtualKeyboard();
      }
    }
    else if (check_button_on_grid(45, 6) && seq.cycle_touch_element == 1)//rest button
    {
      sub_step_recording(true, 1);
    }
    else if (check_button_on_grid(36, 6) && seq.cycle_touch_element == 1)//latch button
    {
      sub_step_recording(true, 2);
    }

    if (seq.cycle_touch_element != 1)
    {
      if (check_button_on_grid(36, 16)) // toggle seq. playmode song/pattern only
      {
        seq.play_mode = !seq.play_mode;

        if (seq.play_mode == false) // is in full song more
        {
          draw_button_on_grid(36, 16, "PLAYNG", "SONG", 0);
          seq.hunt_pattern = false;
          draw_button_on_grid(45, 22, "HUNT", "PATT", 0);
        }
        else // play only current pattern
          draw_button_on_grid(36, 16, "LOOP", "PATT", 2);
      }
      else if (check_button_on_grid(45, 22)) // hunt pattern
      {
        seq.hunt_pattern = !seq.hunt_pattern;

        if (seq.hunt_pattern == false)
          draw_button_on_grid(45, 22, "HUNT", "PATT", 0);
        else // play only current pattern
          draw_button_on_grid(45, 22, "HUNT", "PATT", 2);
      }
      else if (check_button_on_grid(36, 22) && seq.cycle_touch_element != 1) // jump song editor
      {
        LCDML.OTHER_jumpToFunc(UI_func_song);
      }
      else if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_seq_pattern_editor) && seq.cycle_touch_element != 1)
        if (check_button_on_grid(45, 16)) // jump pattern editor functions
        {
          if (seq.content_type[seq.active_pattern] == 0)
          {
            activeSample = NUM_DRUMSET_CONFIG;
          }
          else
          {
            temp_int = 111;
          }
          draw_button_on_grid(45, 16, "JUMP", "TOOLS", 2);
          seq.menu = 0;
          seq.active_function = 0;
          pattern_editor_menu_0();
        }
    }
  }

  if (seq.cycle_touch_element == 1) {
    handleTouchVirtualKeyboard();
  }
}

FLASHMEM void handle_touchscreen_microsynth()
{
  if (TouchButton::isPressed(GRID.X[5], GRID.Y[0]))
  {
    display.console = true;
    display.fillRect(0, VIRT_KEYB_YPOS - 6 * CHAR_height_small, DISPLAY_WIDTH,
      DISPLAY_HEIGHT - VIRT_KEYB_YPOS + 6 * CHAR_height_small, COLOR_BACKGROUND);
    display.console = false;
    if (seq.cycle_touch_element == 1)
    {
      seq.cycle_touch_element = 0;
      TouchButton::drawVirtualKeyboardButton(GRID.X[5], GRID.Y[0]);
      generic_full_draw_required = true;
      microsynth_refresh_lower_screen_static_text();
      microsynth_refresh_lower_screen_dynamic_text();
      draw_back_touchbutton();
      generic_full_draw_required = false;
    }
    else
    {
      seq.cycle_touch_element = 1;
      TouchButton::drawButton(GRID.X[5], GRID.Y[0], "MORE", "PARAM.", TouchButton::BUTTON_ACTIVE);
      drawVirtualKeyboard();
    }
  }
  if (seq.cycle_touch_element == 1) {
    handleTouchVirtualKeyboard();
  }
}

extern int menuhelper_previous_val;

FLASHMEM void print_file_manager_buttons()
{
  if (menuhelper_previous_val != fm.sd_mode)
  {
    TouchButton::drawButton(GRID.X[0], GRID.Y[5], "BROWSE", "FILES", (fm.sd_mode == FM_BROWSE_FILES) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL);
    TouchButton::drawButton(GRID.X[1], GRID.Y[5], "DELETE", "FILE", (fm.sd_mode == FM_DELETE_FILE) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL);
#ifdef COMPILE_FOR_PSRAM
    TouchButton::drawButton(GRID.X[3], GRID.Y[5], "COPY >", "PSRAM", (fm.sd_mode == FM_COPY_TO_PSRAM) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL);
    TouchButton::drawButton(GRID.X[4], GRID.Y[5], "DELETE", "PSRAM", (fm.sd_mode == FM_DELETE_FROM_PSRAM) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL);
#endif

    //TouchButton::drawButton(GRID.X[5], GRID.Y[5], "PLAY", "SAMPLE", (WavFileIsPlaying()) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL);
    TouchButton::drawButton(GRID.X[5], GRID.Y[5], "PLAY", "SAMPLE", TouchButton::BUTTON_NORMAL);

    display.setTextSize(1);
    display.setCursor(CHAR_width_small, (CHAR_height_small * 23) + 5);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
    if (fm.sd_mode != FM_BROWSE_FILES && fm.sd_mode != FM_PLAY_SAMPLE) {
      display.print(F("SELECT, THEN CONFIRM OPERATION > PUSH ENCODER[R]"));
    }
    else
    {
      print_empty_spaces(48,1);
    }
  }
  menuhelper_previous_val = fm.sd_mode;
}

FLASHMEM void print_file_manager_active_border()
{
  // active_window   0 = left window (SDCARD) , 1 = PSRAM
  if (fm.active_window == 0)
  {
    display.console = true;
    display.drawRect(CHAR_width_small * 28 - 1, 0, CHAR_width_small * 25 + 3, CHAR_height_small * 23, GREY2);
    display.console = true;
    display.drawRect(0, 0, CHAR_width_small * 28, CHAR_height_small * 23, COLOR_SYSTEXT);
  }
  else
  {
    display.console = true;
    display.drawRect(0, 0, CHAR_width_small * 28, CHAR_height_small * 23, GREY2);
    display.console = true;
    display.drawRect(CHAR_width_small * 28 - 1, 0, CHAR_width_small * 25 + 3, CHAR_height_small * 23, COLOR_SYSTEXT);
  }
}

#ifdef COMPILE_FOR_PSRAM
extern void  show_smallfont_noGrid(int pos_y, int pos_x, uint8_t field_size, const char* str);
#endif

FLASHMEM void handle_touchscreen_file_manager()
{
  if (TouchButton::isInArea(0, 0, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - TouchButton::BUTTON_SIZE_Y)) {
    fm.active_window = 0;
    print_file_manager_active_border();
  }
#if defined COMPILE_FOR_PSRAM
  else if (TouchButton::isInArea(DISPLAY_WIDTH / 2, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT - TouchButton::BUTTON_SIZE_Y)) {
    fm.active_window = 1;
    print_file_manager_active_border();
  }
#endif
  else if (TouchButton::isPressed(GRID.X[0], GRID.Y[5])) {
    fm.sd_mode = FM_BROWSE_FILES;
    print_file_manager_buttons();
  }
  else if (TouchButton::isPressed(GRID.X[1], GRID.Y[5])) {
    fm.sd_mode = FM_DELETE_FILE;
    print_file_manager_buttons();
  }
#if defined COMPILE_FOR_PSRAM
  else if (TouchButton::isPressed(GRID.X[3], GRID.Y[5])) {
    fm.sd_mode = FM_COPY_TO_PSRAM;
    print_file_manager_buttons();
  }
  else if (TouchButton::isPressed(GRID.X[4], GRID.Y[5])) {
    fm.sd_mode = FM_DELETE_FROM_PSRAM;
    print_file_manager_buttons();
  }
#endif
  else if (TouchButton::isPressed(GRID.X[5], GRID.Y[5])) {
    if ((!fm.sd_is_folder && fm.active_window == 0) || (fm.active_window == 1))
    {
      fm.sd_mode = FM_PLAY_SAMPLE;
      previewWavFilemanager();
      print_file_manager_buttons();
    }
  }
}

FLASHMEM void update_midi_learn_button()
{
  draw_button_on_grid(45, 1, "MIDI", "LEARN", seq.midi_learn_active ? 2 : 0); // RED or grey button
}

FLASHMEM void handle_touchscreen_custom_mappings()
{
  if (numTouchPoints > 0)
  {
    if (check_button_on_grid(45, 1))
    {
      seq.midi_learn_active = !seq.midi_learn_active;
      update_midi_learn_button();
    }
  }
}

extern void play_sample(uint8_t note);
FLASHMEM void handle_touchscreen_drums()
{
  if (TouchButton::isPressed(GRID.X[1], GRID.Y[5]))
  {
    TouchButton::drawButton(GRID.X[1], GRID.Y[5], "PLAY", "SAMPLE", TouchButton::BUTTON_RED);
    play_sample(activeSample);
    delay(60);
    TouchButton::drawButton(GRID.X[1], GRID.Y[5], "PLAY", "SAMPLE", TouchButton::BUTTON_ACTIVE);
  }
}

FLASHMEM void handle_touchscreen_mute_matrix()
{
  uint8_t button_count = 0;
  char buf[4];
  uint8_t spacerx = 90;
  for (uint8_t y = 0; y < 3; y++)
  {
    for (uint8_t x = 0; x < 4; x++)
    {
      if (y < 2)
      {
        if (TouchButton::isPressed(x * spacerx, 95 + y * 55))
        {
          seq.track_mute[button_count] = !seq.track_mute[button_count];
          //const uint8_t color = seq.track_mute[button_count] ? 0 : 1; // grey or active color
          //draw_button_on_grid(2 + x * 14, 12 + y * 8, "TRACK:", itoa(button_count + 1, buf, 10), color);
          TouchButton::drawButton(x * spacerx, 95 + y * 55, "TRACK:", itoa(button_count + 1, buf, 10), seq.track_mute[button_count] ? TouchButton::BUTTON_NORMAL : TouchButton::BUTTON_ACTIVE);
        }
        button_count++;
      }
      else
      {
        if (TouchButton::isPressed(x * spacerx, 35))
        {
          if (x == 1)
            seq.mute_mode = 0;
          else if (x == 2)
            seq.mute_mode = 1;
          else if (x == 3)
            seq.mute_mode = 2;

          if (seq.mute_mode == 0)
            TouchButton::drawButton(1 * spacerx, 35, "REAL", "TIME", TouchButton::BUTTON_ACTIVE);
          else
            TouchButton::drawButton(1 * spacerx, 35, "REAL", "TIME", TouchButton::BUTTON_NORMAL);
          if (seq.mute_mode == 1)
            TouchButton::drawButton(2 * spacerx, 35, "NEXT", "PATTRN", TouchButton::BUTTON_ACTIVE);
          else
            TouchButton::drawButton(2 * spacerx, 35, "NEXT", "PATTRN", TouchButton::BUTTON_NORMAL);
          if (seq.mute_mode == 2)
            TouchButton::drawButton(3 * spacerx, 35, "SONG", "STEP", TouchButton::BUTTON_ACTIVE);
          else
            TouchButton::drawButton(3 * spacerx, 35, "SONG", "STEP", TouchButton::BUTTON_NORMAL);
        }
      }
    }
  }
#if defined APC
  apc_mute_matrix();
#endif
}

FLASHMEM void handle_touchscreen_arpeggio()
{
  if (numTouchPoints > 0)
  {
    if (check_button_on_grid(2, 23))
    {
      if (seq.running)
        handleStop();
      else
        handleStart();
    }
  }
}

FLASHMEM void handle_touchscreen_braids()
{
  if (numTouchPoints > 0)
  {
    seq.cycle_touch_element = 1;
  }
  if (seq.cycle_touch_element == 1) {
    handleTouchVirtualKeyboard();
  }
}

FLASHMEM void draw_menu_ui_icons()
{
  TouchButton::drawButton(GRID.X[0], GRID.Y[3], "DEXED", "VOICE", TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[1], GRID.Y[3], "SONG", "EDITOR", TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[2], GRID.Y[3], "PATT", "EDITOR", TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[3], GRID.Y[3], "LIVE", "SEQ", TouchButton::BUTTON_NORMAL);

  if (seq.running)
    TouchButton::drawButton(GRID.X[5], GRID.Y[3], "SEQ.", "STOP", TouchButton::BUTTON_RED);
  else
    TouchButton::drawButton(GRID.X[5], GRID.Y[3], "SEQ.", "START", TouchButton::BUTTON_ACTIVE);

  TouchButton::drawButton(GRID.X[2], GRID.Y[5], "LOAD", "PERF", TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[3], GRID.Y[5], "SAVE", "PERF", TouchButton::BUTTON_NORMAL);

  TouchButton::drawButton(GRID.X[4], GRID.Y[5], "MULTI", "BAND", multiband_active ? TouchButton::BUTTON_RED : TouchButton::BUTTON_NORMAL);
  TouchButton::drawButton(GRID.X[5], GRID.Y[5], "MAIN", "MIXER", TouchButton::BUTTON_NORMAL);

  TouchButton::drawVirtualKeyboardButton(GRID.X[5], GRID.Y[2]);
}

FLASHMEM void handle_touchscreen_menu()
{
  if ((ts.keyb_in_menu_activated == false) && TouchButton::isPressed(GRID.X[4], GRID.Y[5]))
  {
    multiband_active = !multiband_active;
    TouchButton::drawButton(GRID.X[4], GRID.Y[5], "MULTI", "BAND", multiband_active ? TouchButton::BUTTON_RED : TouchButton::BUTTON_NORMAL);

    mb_set_mutes();
    mb_set_master();
    mb_set_compressor();
  }

  if (LCDML.FUNC_getID() > _LCDML_DISP_cnt) // only when not in UI_func_volume
  {
    if (ts.touch_ui_drawn_in_menu == false)
    {
      if (ts.keyb_in_menu_activated)
      {
        drawVirtualKeyboard();
        TouchButton::drawButton(GRID.X[5], GRID.Y[2], "V.KEYB.", "OFF", TouchButton::BUTTON_ACTIVE);
      }
      else
      {
        draw_menu_ui_icons();
      }
      ts.touch_ui_drawn_in_menu = true;
    }

    if (TouchButton::isPressed(GRID.X[5], GRID.Y[2]))
    {
      display.console = true;
      display.fillRect(0, GRID.Y[3], DISPLAY_WIDTH, DISPLAY_HEIGHT - GRID.Y[3], COLOR_BACKGROUND);
      display.console = false;
      ts.keyb_in_menu_activated = !ts.keyb_in_menu_activated;
      if (ts.keyb_in_menu_activated)
      {
        drawVirtualKeyboard();
        TouchButton::drawButton(GRID.X[5], GRID.Y[2], "V.KEYB.", "OFF", TouchButton::BUTTON_ACTIVE);
      }
      else
      {
        draw_menu_ui_icons();
        if (LCDML.MENU_getLayer() > last_menu_depth)
        {
          draw_back_touchbutton();
        }
      }
    }
    if (ts.keyb_in_menu_activated == false)
    {
      if (TouchButton::isPressed(GRID.X[0], GRID.Y[3]))
      {
        LCDML.OTHER_jumpToFunc(UI_func_voice_select);
      }
      else if (TouchButton::isPressed(GRID.X[1], GRID.Y[3]))
      {
        LCDML.OTHER_jumpToFunc(UI_func_song);
      }
      else if (TouchButton::isPressed(GRID.X[2], GRID.Y[3]))
      {
        LCDML.OTHER_jumpToFunc(UI_func_seq_pattern_editor);
      }
      else if (TouchButton::isPressed(GRID.X[3], GRID.Y[3]))
      {
        LCDML.OTHER_jumpToFunc(UI_func_livesequencer);
      }
      else if (TouchButton::isPressed(GRID.X[5], GRID.Y[3]))
      {
        if (seq.running)
        {
          handleStop();
        }
        else
        {
          handleStart();
        }
      }
      else if (TouchButton::isPressed(GRID.X[2], GRID.Y[5]))
      {
        display.setTextSize(2);
        display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        LCDML.OTHER_jumpToFunc(UI_func_load_performance);
      }
      else if (TouchButton::isPressed(GRID.X[3], GRID.Y[5]))
      {
        display.setTextSize(2);
        display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
        LCDML.OTHER_jumpToFunc(UI_func_save_performance);
      }
      else if (TouchButton::isPressed(GRID.X[5], GRID.Y[5]))
      {
        LCDML.OTHER_jumpToFunc(UI_func_mixer);
      }
      else if (TouchButton::isPressed(GRID.X[0], GRID.Y[5]))// back button
      {
        LCDML.BT_quit();
      }
    }

    ts.touch_ui_drawn_in_menu = true;
  }
  if (ts.keyb_in_menu_activated) {
    handleTouchVirtualKeyboard();
  }
  display.setTextSize(2);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
}

FLASHMEM void toggle_generic_active_function()
{
  if (generic_active_function == 0)
    generic_active_function = 1;
  else
    generic_active_function = 0;
}

FLASHMEM void handle_touchscreen_mixer()
{
  draw_volmeters_mixer();
}

FLASHMEM void handle_touchscreen_multiband()
{
  if (numTouchPoints > 0)
  {
    if (multiband_active)
    {
      if (check_button_on_grid(12, 8))
      {
        mb_solo_high = !mb_solo_high;
        if (mb_solo_high)
        {
          draw_button_on_grid(9, 8, "SOLO", "ON", mb_solo_high + 1);
        }
        else
          draw_button_on_grid(9, 8, "SOLO", "  ", mb_solo_high);
      }
      else if (check_button_on_grid(12, 14))
      {
        mb_solo_upper_mid = !mb_solo_upper_mid;
        if (mb_solo_upper_mid)
        {
          draw_button_on_grid(9, 14, "SOLO", "ON", mb_solo_upper_mid + 1);
        }
        else
          draw_button_on_grid(9, 14, "SOLO", "  ", mb_solo_upper_mid);
      }
      else if (check_button_on_grid(12, 20))
      {
        mb_solo_mid = !mb_solo_mid;
        if (mb_solo_mid)
        {
          draw_button_on_grid(9, 20, "SOLO", "ON", mb_solo_mid + 1);
        }
        else
          draw_button_on_grid(9, 20, "SOLO", "  ", mb_solo_mid);
      }
      else if (check_button_on_grid(12, 26))
      {
        mb_solo_low = !mb_solo_low;
        if (mb_solo_low)
        {
          draw_button_on_grid(9, 26, "SOLO", "ON", mb_solo_low + 1);
        }
        else
          draw_button_on_grid(9, 26, "SOLO", "  ", mb_solo_low);
      }
      if (mb_solo_low && mb_solo_upper_mid && mb_solo_mid && mb_solo_high)
      {
        mb_solo_low = false;
        mb_solo_mid = false;
        mb_solo_upper_mid = false;
        mb_solo_high = false;
        draw_button_on_grid(9, 8, "SOLO", "   ", mb_solo_high);
        draw_button_on_grid(9, 14, "SOLO", "   ", mb_solo_upper_mid);
        draw_button_on_grid(9, 20, "SOLO", "   ", mb_solo_mid);
        draw_button_on_grid(9, 26, "SOLO", "   ", mb_solo_low);
      }
      mb_set_mutes();

      if (check_button_on_grid(38, 8))
      {
        toggle_generic_active_function();
        generic_temp_select_menu = 7;
      }
      else if (check_button_on_grid(38, 14))
      {
        toggle_generic_active_function();
        generic_temp_select_menu = 12;
      }
      else if (check_button_on_grid(38, 20))
      {
        toggle_generic_active_function();
        generic_temp_select_menu = 17;
      }
      else if (check_button_on_grid(38, 26))
      {
        toggle_generic_active_function();
        generic_temp_select_menu = 22;
      }
    }
  }
  draw_volmeters_multiband_compressor();
}

extern int temp_int;
extern uint8_t num_slices[2];
extern void draw_waveform_sample_editor(uint8_t sliceno);
extern void fill_slices();
extern void clear_slices();
extern bool slices_autoalign;
extern  uint16_t slices_scrollspeed;
extern uint8_t selected_slice_sample[2];
extern void print_sliced_status();
extern void check_and_print_sliced_message();

FLASHMEM void assign_free_slices_or_increment()
{
  if (num_slices[0] == 0 && selected_slice_sample[0] == 99)
  {
    num_slices[0] = 1;
    selected_slice_sample[0] = temp_int; // set sliced sample
    draw_button_on_grid(45, 23, "PLAY", "SLC", 0);
  }
  else if (num_slices[1] == 0 && selected_slice_sample[1] == 99 && selected_slice_sample[0] != 99 && selected_slice_sample[0] != temp_int)
  {
    num_slices[1] = 1;
    selected_slice_sample[1] = temp_int; // set sliced sample
    draw_button_on_grid(45, 23, "PLAY", "SLC", 0);
  }
  else if (selected_slice_sample[0] == temp_int && selected_slice_sample[0] != 99)
  {
    num_slices[0] = num_slices[0] * 2;
    if (num_slices[0] > 16)
    {
      clear_slices();
      num_slices[0] = 0;
      selected_slice_sample[0] = 99; // clear selected sliced sample 1
      draw_button_on_grid(45, 23, "PLAY", "SAMPLE", 0);
      //generic_temp_select_menu = 6;
      display.fillRect(CHAR_width_small * 26, 4 * CHAR_height_small - 2, 160, 39, COLOR_BACKGROUND);  //slices + scrollbar
    }
  }
  else if (selected_slice_sample[1] == temp_int && selected_slice_sample[1] != 99)
  {
    num_slices[1] = num_slices[1] * 2;
    if (num_slices[1] > 16)
    {
      clear_slices();
      num_slices[1] = 0;
      selected_slice_sample[1] = 99; // clear selected sliced sample 2
      draw_button_on_grid(45, 23, "PLAY", "SAMPLE", 0);
      //generic_temp_select_menu = 6;
      display.fillRect(CHAR_width_small * 26, 4 * CHAR_height_small - 2, 160, 39, COLOR_BACKGROUND);  //slices + scrollbar
    }
  }
  fill_slices();
  print_sliced_status();
  draw_waveform_sample_editor(99);
  check_and_print_sliced_message();
}

FLASHMEM void draw_loop_buttons()
{
  if (fm.sample_source == 2 || fm.sample_source == 3)
  {
    draw_button_on_grid(1, 23, "LOOP", "START", 1 + loop_start_button);
    draw_button_on_grid(9, 23, "LOOP", "END", 1 + loop_end_button);
  }
  else  if (fm.sample_source == 4)
  {
    draw_button_on_grid(1, 23, "LOOP", "START", 0);
    draw_button_on_grid(9, 23, "LOOP", "END", 0);
  }
}

extern void print_loop_data();

FLASHMEM void handle_touchscreen_sample_editor()
{
  if (numTouchPoints > 0)
  {
    if (check_button_on_grid(45, 23)) {
      previewWavSampleEditor();
    }

    if (check_button_on_grid(1, 23) && fm.sample_source > 1 && fm.sample_source < 4) //loop start
    {
      // draw_button_on_grid(1, 23, "LOOP", "START", 0);
      if (loop_start_button == false) {
        loop_start_button = true;
        loop_end_button = false;
        seq.edit_state = 1;
        generic_temp_select_menu = 3;
      }
      else {
        loop_start_button = false;
        seq.edit_state = 0;
      }
      draw_loop_buttons();
      print_loop_data();

    }
    else  if (check_button_on_grid(9, 23) && fm.sample_source > 1 && fm.sample_source < 4)  //loop end
    {
      draw_button_on_grid(9, 23, "LOOP", "END", 0);
      if (loop_end_button == false) {
        loop_end_button = true;
        loop_start_button = false;
        seq.edit_state = 1;
        generic_temp_select_menu = 4;
      }
      else {
        loop_end_button = false;
        seq.edit_state = 0;
      }
      draw_loop_buttons();
      print_loop_data();
    }

    if (check_button_on_grid(33, 23) && fm.sample_source == 4) // num slices
    {
      assign_free_slices_or_increment();
    }

    if (check_button_on_grid(25, 23) && fm.sample_source == 4) // auto align (slices only)
    {
      slices_autoalign = !slices_autoalign;
      draw_button_on_grid(25, 23, "AUTO", "ALIGN", 1 + slices_autoalign);
    }

    if (check_button_on_grid(17, 23)) // scrolling speed
    {
      slices_scrollspeed = slices_scrollspeed * 2;
      if (slices_scrollspeed > 512)
        slices_scrollspeed = 1;

      char buf[4] = { 0 };
      draw_button_on_grid(17, 23, "SCRSPD", itoa(slices_scrollspeed, buf, 10), 1);
    }
  }
}

FLASHMEM void handle_touchscreen_settings_button_test()
{
  static bool button_state = false;

  if (numTouchPoints > 0)
  {
    if (check_button_on_grid(42, 1))
    {
      draw_button_on_grid(42, 1, "TOUCH", button_state ? "OK" : "TEST", button_state ? 2 : 0);
      button_state = !button_state;
    }
  }
}

FLASHMEM void handle_touchscreen_test_touchscreen()
{
  if (numTouchPoints > 0)
  {
    if (check_button_on_grid(42, 1))
      sub_touchscreen_test_page_init();
    display.console = true;
    display.fillRect(ts.p.x, ts.p.y, 2, 2, COLOR_SYSTEXT);
    display.console = false;
  }
}

FLASHMEM void handle_page_with_touch_back_button()
{
  if (numTouchPoints > 0)
  {
    // if (seq.cycle_touch_element != 1 && check_button_on_grid(2, 25)) // back button
    if (seq.cycle_touch_element != 1 && TouchButton::isPressed(GRID.X[0], GRID.Y[5])) // back button
    {
      LCDML.BT_quit();
    }
  }
}
