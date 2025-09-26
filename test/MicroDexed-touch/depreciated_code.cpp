// this file contains only outdated or no longer used code for easy retrieval in case parts of it might still be useful

// FLASHMEM uint8_t bit_reverse8(uint8_t v)
// {
//   uint8_t result = 0;
//   for (; v > 0; v >>= 1)
//     (result <<= 1) |= (v & 1);
//   return (result);
// }

// void update_pianoroll()
// {
//   uint8_t notes[64];
//   uint8_t lowest_note = 127;
//   int notes_display_shift = 0;
//   uint8_t last_valid_note = 254;
//   uint8_t patternspacer = 0;
//   uint8_t barspacer = 0;
//   int8_t current_chain = 99;
//   int8_t pattern[4] = { 99, 99, 99, 99 };

//   current_chain = seq.song[seq.active_track][0]; // so far only step 0 of chain is displayed

//   for (uint8_t d = 0; d < 4; d++)
//   {
//     pattern[d] = seq.chain[current_chain][d];
//   }

//   for (uint8_t f = 0; f < 16; f++) // Fill array with complete data from all chain parts of track
//   {
//     notes[f] = seq.note_data[pattern[0]][f];
//     notes[f + 16] = seq.note_data[pattern[1]][f];
//     notes[f + 32] = seq.note_data[pattern[2]][f];
//     notes[f + 48] = seq.note_data[pattern[3]][f];
//   }
//   // find lowest note
//   for (uint8_t f = 0; f < 64; f++)
//   {
//     if (notes[f] < lowest_note && notes[f] > 0)
//     {
//       lowest_note = notes[f];
//     }
//   }
//   if (lowest_note > 120)
//     lowest_note = 24;
//   notes_display_shift = lowest_note % 12;

//   for (uint8_t xcount = 0; xcount < 64; xcount++)
//   {
//     if (notes[xcount] > 0)
//     {
//       if (notes[xcount] == 130)
//       {
//         if (seq.step * seq.chain_counter[seq.active_track] != xcount)
//           display.fillRect(40 + patternspacer + barspacer + xcount * 4, DISPLAY_HEIGHT - CHAR_height - 10 - (8.15 * notes_display_shift) - (8.15 * (last_valid_note - lowest_note)), 3, 5, COLOR_PITCHSMP);
//         else
//           display.fillRect(40 + patternspacer + barspacer + xcount * 4, DISPLAY_HEIGHT - CHAR_height - 10 - (8.15 * notes_display_shift) - (8.15 * (last_valid_note - lowest_note)), 3, 5, RED);
//       }
//       else
//       {
//         if (seq.step * seq.chain_counter[seq.active_track] != xcount)
//           display.fillRect(40 + patternspacer + barspacer + xcount * 4, DISPLAY_HEIGHT - CHAR_height - 10 - (8.15 * notes_display_shift) - (8.15 * (notes[xcount] - lowest_note)), 3, 5, GREY1);
//         else
//           display.fillRect(40 + patternspacer + barspacer + xcount * 4, DISPLAY_HEIGHT - CHAR_height - 10 - (8.15 * notes_display_shift) - (8.15 * (notes[xcount] - lowest_note)), 3, 5, GREEN);
//         last_valid_note = notes[xcount];
//       }
//     }
//     if ((xcount + 1) % 16 == 0)
//       patternspacer = patternspacer + 2;
//     if ((xcount + 1) % 4 == 0)
//       barspacer = barspacer + 1;
//   }
// }

// void print_merged_pattern_pianoroll(int xpos, int ypos, uint8_t track_number)
// {
//   uint8_t notes[64];
//   uint8_t lowest_note = 127;
//   int notes_display_shift = 0;
//   uint8_t last_valid_note = 254;
//   uint8_t patternspacer = 0;
//   uint8_t barspacer = 0;

//   int8_t current_chain = 99;
//   int8_t pattern[4] = { 99, 99, 99, 99 };

//   current_chain = seq.song[track_number][0]; // so far only step 0 of chain is displayed

//   for (uint8_t d = 0; d < 4; d++)
//   {
//     pattern[d] = seq.chain[current_chain][d];
//   }

//   helptext_l("MOVE Y");
//   helptext_r("MOVE X");

//   display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
//   display.setCursor(CHAR_width * 2, 0);

//   display.print("[");
//   display.print(0);
//   display.print("]");

//   display.setTextColor(COLOR_SYSTEXT, COLOR_PITCHSMP);

//   display.print(F(" TRK:["));
//   display.print(track_number + 1);
//   display.print("] ");
//   display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
//   display.print(" ");
//   display.setTextColor(COLOR_SYSTEXT, COLOR_PITCHSMP);
//   display.print(F(" CHAIN: "));
//   display.print(current_chain);
//   display.print(F("  "));

//   print_formatted_number(pattern[0], 2);
//   display.write(25);
//   print_formatted_number(pattern[1], 2);
//   display.write(25);
//   print_formatted_number(pattern[2], 2);
//   display.write(25);
//   print_formatted_number(pattern[3], 2);
//   display.print(" ");

//   //    if (pattern[0] < NUM_SEQ_PATTERN && pattern[1] < NUM_SEQ_PATTERN
//   //        && pattern[2] < NUM_SEQ_PATTERN && pattern[3] < NUM_SEQ_PATTERN)
//   //    {

//   for (uint8_t f = 0; f < 16; f++) // Fill array with complete data from all chain parts of track
//   {
//     notes[f] = seq.note_data[pattern[0]][f];
//     notes[f + 16] = seq.note_data[pattern[1]][f];
//     notes[f + 32] = seq.note_data[pattern[2]][f];
//     notes[f + 48] = seq.note_data[pattern[3]][f];
//   }
//   // find lowest note
//   for (uint8_t f = 0; f < 64; f++)
//   {
//     if (notes[f] < lowest_note && notes[f] > 0)
//     {
//       lowest_note = notes[f];
//     }
//   }
//   if (lowest_note > 120)
//     lowest_note = 24;
//   notes_display_shift = lowest_note % 12;
//   print_keyboard(ypos, lowest_note / 12);
//   for (uint8_t xcount = 0; xcount < 64; xcount++)
//   {
//     if (notes[xcount] > 0)
//     {
//       if (notes[xcount] == 130)
//       {
//         display.fillRect(40 + patternspacer + barspacer + xcount * 4, ypos - 10 - (8.15 * notes_display_shift) - (8.15 * (last_valid_note - lowest_note)), 3, 5, COLOR_PITCHSMP);
//       }
//       else
//       {
//         display.fillRect(40 + patternspacer + barspacer + xcount * 4, ypos - 10 - (8.15 * notes_display_shift) - (8.15 * (notes[xcount] - lowest_note)), 3, 5, GREY1);
//         last_valid_note = notes[xcount];
//       }
//     }
//     if ((xcount + 1) % 16 == 0)
//       patternspacer = patternspacer + 2;
//     if ((xcount + 1) % 4 == 0)
//       barspacer = barspacer + 1;
//   }
// }


//old configurations

// ; [env:flashmem_psram_capacitive]
// ; ; FLASHMEM_PSRAM_CAPACITIVE
// ; build_flags = 
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_FLASH
// ;     -D PSRAM
// ;     -D CAPACITIVE_TOUCH_DISPLAY
// ;     ;-D DEBUG=1
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = FLASH_PSRAM_CAPACITIVE

// ; [env:flashmem_capacitive]
// ; ; FLASHMEM_CAPACITIVE
// ; build_flags = 
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_FLASH
// ;     -D CAPACITIVE_TOUCH_DISPLAY
// ;     ;-D DEBUG=1
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = FLASH_CAPACITIVE

// ; [env:flashmem_buttons]
// ; build_flags =
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_FLASH
// ;     -D ONBOARD_BUTTON_INTERFACE
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = FLASH_BUTTONS_INTERFACE

// ; [env:sdcard]
// ; build_flags =
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_SDCARD
// ; ; change MCU frequency
// ; ;board_build.f_cpu = 600000000L //default
// ; ;board_build.f_cpu = 816000000L
// ; custom_firmware_name = SDCARD

// ; [env:debug]
// ; ; DEBUG with PROGMEM
// ; ; build_type = debug
// ; build_flags =
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_PROGMEM
// ;     -D DEBUG=1
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = DEFAULT_DEBUG
// ; extra_scripts =
// ;     ${env.extra_scripts}
// ;     ; post:pio_scripts/increment_version.py


// ; [env:debug_flash]
// ; ; build_type = debug
// ; build_flags =
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_FLASH
// ;     -D DEBUG=1
// ; ;board_build.f_cpu = 720000000L
// ; custom_firmware_name = FLASH_DEBUG
// ; extra_scripts =
// ;     ${env.extra_scripts}
// ;     ; post:pio_scripts/increment_version.py
// ;     ; post:pio_scripts/zip_hex_files.py

// ; [env:progmem_psram]
// ; build_flags = 
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_PROGMEM
// ;     -D PSRAM
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = DEFAULT_PSRAM

// ; [env:progmem_buttons]
// ; ; BUTTONS + PROGMEM
// ; build_flags =
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_PROGMEM
// ;     -D ONBOARD_BUTTON_INTERFACE
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = DEFAULT_BUTTONS_INTERFACE

// ; [env:flashmem]
// ; ; FLASHMEM
// ; build_flags = 
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_FLASH
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = FLASH

// ; [env:flashmem_capacitive_buttons]
// ; build_flags = 
// ;     ${common.build_flags}
// ;      -D COMPILE_FOR_FLASH
// ;     -D CAPACITIVE_TOUCH_DISPLAY
// ;     -D ONBOARD_BUTTON_INTERFACE
// ;     ;-D DEBUG=1
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = FLASH_CAPACITIVE_BUTTONS_INTERFACE

// ; [env:flashmem_psram]
// ; ; FLASHMEM_PSRAM
// ; build_flags = 
// ;     ${common.build_flags}
// ;     -D COMPILE_FOR_FLASH
// ;     -D PSRAM
// ; ; lib_deps =
// ; ;     ${env.lib_deps}
// ; custom_firmware_name = FLASH_PSRAM

// [env:psram_capacitive_boot_delay]
// ; PSRAM_CAPACITIVE
// build_flags = 
//     ${common.build_flags}
//     -D COMPILE_FOR_PSRAM
//     -D PSRAM
//     -D CAPACITIVE_TOUCH_DISPLAY
//     -D BOOT_DELAY
//     ;-D MIDI_ACTIVITY_LIGHTS
//     ;-D DEBUG=1
// ; lib_deps =
// ;     ${env.lib_deps}
// custom_firmware_name = PSRAM-CAPACITIVE_TOUCH-BOOT_DELAY

// [env:progmem_capacitive_boot_delay]
// build_flags = 
//     ${common.build_flags}
//     -D COMPILE_FOR_PROGMEM
//     -D CAPACITIVE_TOUCH_DISPLAY
//     -D BOOT_DELAY
//     ;-D MIDI_ACTIVITY_LIGHTS
//     ;-D DEBUG=1
// ; lib_deps =
// ;     ${env.lib_deps}
// custom_firmware_name = PROGMEM-CAPACITIVE_TOUCH-BOOT_DELAY

// microdexed-touch.ino

// 2022/11/19 update SD: move banks into /DEXED
//     File sysex_dir;
//     for (i = 0; i < MAX_BANKS; i++)
//     {
//       snprintf_P(tmp, sizeof(tmp), PSTR("/%d"), i);
//       if (SD.exists(tmp))
//       {
//         sysex_dir = SD.open(tmp);

//         if (!sysex_dir)
//         {
//           break;
//         }

//         // move file into new /DEXED folder
//         File entry;
//         do
//         {
//           entry = sysex_dir.openNextFile();
//         } while (entry.isDirectory());

//         char sysexFilename[FILENAME_LEN];
//         char sysexPath[FILENAME_LEN];
//         strcpy(sysexFilename, entry.name());
//         snprintf_P(sysexPath, sizeof(sysexPath), PSTR("/%d/%s"), i, sysexFilename);
// #ifdef DEBUG
//         LOG.printf("move %s to /%s%s\n", sysexPath, DEXED_CONFIG_PATH, sysexPath);
// #endif
//         entry.close();
//         sysex_dir.close();

//         File myFileIn = SD.open(sysexPath, FILE_READ);
//         byte buffer[4104];
//         myFileIn.read(buffer, 4104);
//         myFileIn.close();
//         SD.remove(sysexPath);

//         snprintf_P(sysexPath, sizeof(sysexPath), PSTR("/%s/%d/%s"), DEXED_CONFIG_PATH, i, sysexFilename);
//         if (SD.exists(sysexPath))
//         {
//           SD.remove(sysexPath);
//         }
//         File myFileOut = SD.open(sysexPath, FILE_WRITE);
//         myFileOut.write(buffer, 4104);
//         myFileOut.close();

//         SD.rmdir(tmp);
//       }
//     }


  /*
      // create directories for configuration files
      snprintf_P(tmp, sizeof(tmp), PSTR("/%s"), VOICE_CONFIG_PATH);
      if (!SD.exists(tmp))
      {
      #ifdef DEBUG
      LOG.print(F("Creating directory "));
      LOG.println(tmp);
      #endif
      SD.mkdir(tmp);
      }
    snprintf_P(tmp, sizeof(tmp), PSTR("/%s"), PERFORMANCE_CONFIG_PATH);
    if (!SD.exists(tmp))
    {
#ifdef DEBUG
    LOG.print(F("Creating directory "));
    LOG.println(tmp);
#endif
    SD.mkdir(tmp);
}
  snprintf_P(tmp, sizeof(tmp), PSTR("/%s"), FX_CONFIG_PATH);
  if (!SD.exists(tmp))
  {
#ifdef DEBUG
    LOG.print(F("Creating directory "));
    LOG.println(tmp);
#endif
    SD.mkdir(tmp);
  }
  snprintf_P(tmp, sizeof(tmp), PSTR("/%s"), DRUM_CONFIG_PATH);
  if (!SD.exists(tmp))
  {
#ifdef DEBUG
    LOG.print(F("Creating directory "));
    LOG.println(tmp);
#endif
    SD.mkdir(tmp);
  }
  snprintf_P(tmp, sizeof(tmp), PSTR("/%s"), FAV_CONFIG_PATH);
  if (!SD.exists(tmp))
  {
#ifdef DEBUG
    LOG.print(F("Creating directory "));
    LOG.println(tmp);
#endif
    SD.mkdir(tmp);
  }
  */

  // 2022/11/22 move existing favourites into /DEXED
    // if (SD.exists("FAVCFG"))
    // {

    //   SD.mkdir(FAV_CONFIG_PATH);

    //   for (i = 0; i < MAX_BANKS; i++)
    //   {
    //     snprintf_P(tmp, sizeof(tmp), PSTR("/FAVCFG/%d"), i);

    //     if (SD.exists(tmp))
    //     {
    //       char favPath[FILENAME_LEN];
    //       char favFilename[FILENAME_LEN];

    //       strcpy(favPath, tmp);

    //       snprintf_P(tmp, sizeof(tmp), PSTR("/%s/%d"), FAV_CONFIG_PATH, i);
    //       SD.mkdir(tmp);

    //       File fav_bank = SD.open(favPath);
    //       while (true)
    //       {
    //         File fav = fav_bank.openNextFile();
    //         if (!fav)
    //           break;

    //         strcpy(favFilename, fav.name());
    //         snprintf_P(favPath, sizeof(favPath), PSTR("/%s/%d/%s"), "FAVCFG", i, favFilename);
    //         fav.close();

    //         SD.remove(favPath);

    //         snprintf_P(favPath, sizeof(favPath), PSTR("/%s/%d/%s"), FAV_CONFIG_PATH, i, favFilename);
    //         File myFileOut = SD.open(favPath, FILE_WRITE);
    //         myFileOut.close();
    //       }
    //       fav_bank.close();
    //       snprintf_P(tmp, sizeof(tmp), PSTR("FAVCFG/%d"), i);
    //       SD.rmdir(tmp);
    //     }
    //   }

    //   SD.rmdir("/FAVCFG");
    // }

    // uint8_t found_samples[20];

// FLASHMEM void init_found_samples_array()
// {
//   for (uint8_t i = 0; i < 20; i++)
//     found_samples[i] = 254; //set an empty value so sample #0 is unique and seachable, too.
// }

// FLASHMEM void print_used_samples()
// {

//   uint8_t count = 0;
//   uint8_t found_unique_sample_slot = 0;
//   bool found_previously = false;  //skip duplicates while searching
//   init_found_samples_array();
//   do
//   {
//     count++;
//     for (uint8_t d = 0; d < NUM_SEQ_PATTERN; d++)
//     {
//       if (seq.content_type[d] == 0)
//       {
//         for (uint8_t y = 0; y < 16; y++)
//         {
//           if ((drum_config[count].midinote == seq.note_data[d][y] && seq.vel[d][y] < 210) ||
//             (drum_config[count].midinote == seq.vel[d][y] && seq.vel[d][y] > 209))
//           {
//             found_previously = false;
//             for (uint8_t e = 0; e < 20; e++) {
//               if (found_samples[e] == drum_config[count].midinote)
//               {
//                 found_previously = true;
//               }
//             }
//             if (found_previously == false)
//             {
//               found_samples[found_unique_sample_slot] = drum_config[count].midinote;
//               found_unique_sample_slot++;
//             }

//           }
//         }
//       }
//     }
//   } while (count < NUM_DRUMSET_CONFIG - 2);

// #ifdef DEBUG
//   LOG.println(F("Found samples (MIDI Notes):"));
//   for (uint8_t e = 0; e < 20; e++)
//   {
//     if (found_samples[e] != 254)  //is not empty
//     {
//       LOG.print(found_samples[e]);
//       LOG.print(" ");
//     }
//   }
// #endif
// }

 // {
    //   display.setCursor(x + CHAR_width_small * 17, y);
    //   if (seq.note_data[seq.active_pattern][i] != 0) {
    //     if (seq.note_data[seq.active_pattern][i] == 130)  //it is a latched note
    //     {
    //       if (i == currentstep)
    //         display.setTextColor(COLOR_SYSTEXT, COLOR_PITCHSMP);
    //       else
    //         display.setTextColor(GREEN, COLOR_BACKGROUND);
    //       display.write(0x7E);
    //       display.print(F("LATCH"));  //Tilde Symbol for latched note
    //     } else {
    //       display.print(noteNames[seq.note_data[seq.active_pattern][i] % 12][0]);
    //       if (noteNames[seq.note_data[seq.active_pattern][i] % 12][1] != '\0') {
    //         display.print(noteNames[seq.note_data[seq.active_pattern][i] % 12][1]);
    //       }
    //       if (seq.vel[seq.active_pattern][i] < 200)  //print octave when it is not a chord
    //       {
    //         display.print((seq.note_data[seq.active_pattern][i] / 12) - 1);
    //         display.print(" ");
    //       }
    //       if (seq.vel[seq.active_pattern][i] > 199)  //is a chord
    //       {
    //         display.print(" ");
    //         print_chord_name(i);
    //       }
    //     }
    //   }

     //    // when in Voice select Menu, long left-press sets/unsets favorite
    //    if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_voice_select))
    //    save_favorite(configuration.dexed[selected_instance_id].bank, configuration.dexed[selected_instance_id].voice, selected_instance_id);
    //    // when not in Voice select Menu, long left-press starts/stops sequencer
    //    else if (LCDML.FUNC_getID() != LCDML.OTHER_getIDFromFunction(UI_func_voice_select))
    //    toggle_sequencer_play_status();

    // filter
    // ui.printLn("");
    // ui.printLn("FILTER", GREY2);
    // ui.addEditor("CUTOFF", FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX, &configuration.fx.filter_cutoff[0],
    //   &fx_current_instance_getter, [](Editor* editor, int16_t value)
    //   {
    //     fx_current_instance_setter(editor, value);
    //     MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 104, value);
    //     MicroDexed[selected_instance_id]->setFilterCutoff(mapfloat(value, FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX, 1.0, 0.0)); });
    // ui.addEditor("RESONANCE", FILTER_RESONANCE_MIN, FILTER_RESONANCE_MAX, &configuration.fx.filter_resonance[0],
    //   &fx_current_instance_getter, [](Editor* editor, int16_t value)
    //   {
    //     fx_current_instance_setter(editor, value);
    //     MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 103, value);
    //     MicroDexed[selected_instance_id]->setFilterResonance(mapfloat(value, FILTER_RESONANCE_MIN, FILTER_RESONANCE_MAX, 1.0, 0.0)); });

     //  if (seq.content_type[pattern] == 3) 
          // {
          //   for (uint8_t step = 0; step < 16; step++) 
          //   {
          //     found = ( (drum_config[note].midinote+24 == seq.note_data[pattern][step]  )||
          //    (drum_config[note].midinote+24 == seq.note_data[pattern][step]  ) );
          //     if (found) {
          //       result = note;
          //       break;
          //     }
          //   }
          // }

// void UI_func_seq_pianoroll(uint8_t param)
// {

  //for old sequencer

//   if (LCDML.FUNC_setup()) // ****** SETUP *********
//   {
//     // setup function
//     display.fillScreen(COLOR_BACKGROUND);
//     encoderDir[ENC_R].reset();

//     display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
//     display.setTextSize(1);
//     display.setCursor(0, 0);
//     display.print("SONG");
//     display.setCursor(0, CHAR_height / 2);
//     display.print("STEP");
//     // display.fillRect(1, 72, DISPLAY_WIDTH, DISPLAY_HEIGHT - 72, COLOR_BACKGROUND);
//     print_merged_pattern_pianoroll(1 * CHAR_width, DISPLAY_HEIGHT - CHAR_height, seq.active_track);
//   }
//   if (LCDML.FUNC_loop()) // ****** LOOP *********
//   {
//     if (LCDML.BT_checkDown())
//     {
//       seq.active_track = constrain(seq.active_track + ENCODER[ENC_R].speed(), 0, NUM_SEQ_TRACKS - 1);
//     }
//     else if (LCDML.BT_checkUp())
//     {
//       seq.active_track = constrain(seq.active_track - ENCODER[ENC_R].speed(), 0, NUM_SEQ_TRACKS - 1);
//     }
//     print_merged_pattern_pianoroll(1 * CHAR_width, DISPLAY_HEIGHT - CHAR_height, seq.active_track);
//   }
//   if (LCDML.FUNC_close()) // ****** STABLE END *********
//   {
//     display.fillScreen(COLOR_BACKGROUND);
//     encoderDir[ENC_R].reset();
//   }
// }

//void print_merged_pattern_pianoroll(int xpos, int ypos, uint8_t track_number)
//{  //for old sequencer

  // uint8_t notes[64];
  // uint8_t lowest_note = 127;
  // int notes_display_shift = 0;
  // uint8_t last_valid_note = 254;
  // uint8_t patternspacer = 0;
  // uint8_t barspacer = 0;

  // int8_t current_chain = 99;
  // int8_t pattern[4] = { 99, 99, 99, 99 };

  // current_chain = seq.song[track_number][0]; // so far only step 0 of chain is displayed

  // for (uint8_t d = 0; d < 4; d++)
  // {
  //   pattern[d] = seq.chain[current_chain][d];
  // }

  // helptext_l("MOVE Y");
  // helptext_r("MOVE X");

  // display.setTextColor(COLOR_SYSTEXT, COLOR_CHORDS);
  // display.setCursor(CHAR_width * 2, 0);

  // display.print("[");
  // display.print(0);
  // display.print("]");

  // display.setTextColor(COLOR_SYSTEXT, COLOR_PITCHSMP);

  // display.print(F(" TRK:["));
  // display.print(track_number + 1);
  // display.print("] ");
  // display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  // display.print(" ");
  // display.setTextColor(COLOR_SYSTEXT, COLOR_PITCHSMP);
  // display.print(F(" CHAIN: "));
  // display.print(current_chain);
  // display.print(F("  "));

  // print_formatted_number(pattern[0], 2);
  // display.write(25);
  // print_formatted_number(pattern[1], 2);
  // display.write(25);
  // print_formatted_number(pattern[2], 2);
  // display.write(25);
  // print_formatted_number(pattern[3], 2);
  // display.print(" ");

  //    if (pattern[0] < NUM_SEQ_PATTERN && pattern[1] < NUM_SEQ_PATTERN
  //        && pattern[2] < NUM_SEQ_PATTERN && pattern[3] < NUM_SEQ_PATTERN)
  //    {

  // for (uint8_t f = 0; f < 16; f++) // Fill array with complete data from all chain parts of track
  // {
  //   notes[f] = seq.note_data[pattern[0]][f];
  //   notes[f + 16] = seq.note_data[pattern[1]][f];
  //   notes[f + 32] = seq.note_data[pattern[2]][f];
  //   notes[f + 48] = seq.note_data[pattern[3]][f];
  // }
  // // find lowest note
  // for (uint8_t f = 0; f < 64; f++)
  // {
  //   if (notes[f] < lowest_note && notes[f] > 0)
  //   {
  //     lowest_note = notes[f];
  //   }
  // }
  // if (lowest_note > 120)
  //   lowest_note = 24;
  // notes_display_shift = lowest_note % 12;
  // print_keyboard(ypos, lowest_note / 12);
  // for (uint8_t xcount = 0; xcount < 64; xcount++)
  // {
  //   if (notes[xcount] > 0)
  //   {
  //     if (notes[xcount] == 130)
  //     {
  //       display.fillRect(40 + patternspacer + barspacer + xcount * 4, ypos - 10 - (8.15 * notes_display_shift) - (8.15 * (last_valid_note - lowest_note)), 3, 5, COLOR_PITCHSMP);
  //     }
  //     else
  //     {
  //       display.fillRect(40 + patternspacer + barspacer + xcount * 4, ypos - 10 - (8.15 * notes_display_shift) - (8.15 * (notes[xcount] - lowest_note)), 3, 5, GREY1);
  //       last_valid_note = notes[xcount];
  //     }
  //   }
  //   if ((xcount + 1) % 16 == 0)
  //     patternspacer = patternspacer + 2;
  //   if ((xcount + 1) % 4 == 0)
  //     barspacer = barspacer + 1;
  // }
//}

 //  mb_compressor_l_0.compression(mb_threshold_low * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_low );
  //  mb_compressor_r_0.compression(mb_threshold_low * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_low );
  //
  //  mb_compressor_l_1.compression(mb_threshold_mid * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_mid);
  //  mb_compressor_r_1.compression(mb_threshold_mid * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_mid);
  //
  //  mb_compressor_l_2.compression(mb_threshold_upper_mid * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_upper_mid);
  //  mb_compressor_r_2.compression(mb_threshold_upper_mid * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_upper_mid);
  //
  //  mb_compressor_l_3.compression(mb_threshold_high * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_high);
  //  mb_compressor_r_3.compression(mb_threshold_high * -1, 0.03f , 0.2f , mb_global_ratio, 0.0f , mb_gain_high);

  //  mb_filter_l_0.setLowpass(0, mb_cross_freq_low, mb_q_low);
  //  mb_filter_r_0.setLowpass(0, mb_cross_freq_low, mb_q_low);
  //  mb_filter_l_0.setLowpass(1, mb_cross_freq_low, mb_q_low * 2);
  //  mb_filter_r_0.setLowpass(1, mb_cross_freq_low, mb_q_low * 2);
  //  mb_filter_l_0.setLowpass(2, mb_cross_freq_low, mb_q_low);
  //  mb_filter_r_0.setLowpass(2, mb_cross_freq_low, mb_q_low);
  //  mb_filter_l_0.setLowpass(3, mb_cross_freq_low, mb_q_low * 2);
  //  mb_filter_r_0.setLowpass(3, mb_cross_freq_low, mb_q_low * 2);
  //
  //  mb_filter_l_1.setBandpass(0, mb_cross_freq_mid, mb_q_mid);
  //  mb_filter_r_1.setBandpass(0, mb_cross_freq_mid, mb_q_mid);
  //  mb_filter_l_1.setBandpass(1, mb_cross_freq_mid, mb_q_mid * 2);
  //  mb_filter_r_1.setBandpass(1, mb_cross_freq_mid, mb_q_mid * 2);
  //  mb_filter_l_1.setBandpass(2, mb_cross_freq_mid, mb_q_mid);
  //  mb_filter_r_1.setBandpass(2, mb_cross_freq_mid, mb_q_mid);
  //  mb_filter_l_1.setBandpass(3, mb_cross_freq_mid, mb_q_mid * 2);
  //  mb_filter_r_1.setBandpass(3, mb_cross_freq_mid, mb_q_mid * 2);
  //
  //  mb_filter_l_2.setBandpass(0, mb_cross_freq_upper_mid, mb_q_upper_mid);
  //  mb_filter_r_2.setBandpass(0, mb_cross_freq_upper_mid, mb_q_upper_mid);
  //  mb_filter_l_2.setBandpass(1, mb_cross_freq_upper_mid, mb_q_upper_mid * 2);
  //  mb_filter_r_2.setBandpass(1, mb_cross_freq_upper_mid, mb_q_upper_mid * 2);
  //  mb_filter_l_2.setBandpass(2, mb_cross_freq_upper_mid, mb_q_upper_mid);
  //  mb_filter_r_2.setBandpass(2, mb_cross_freq_upper_mid, mb_q_upper_mid);
  //  mb_filter_l_2.setBandpass(3, mb_cross_freq_upper_mid, mb_q_upper_mid * 2);
  //  mb_filter_r_2.setBandpass(3, mb_cross_freq_upper_mid, mb_q_upper_mid * 2);
  //
  //  mb_filter_l_3.setHighpass(0, mb_cross_freq_high, mb_q_high);
  //  mb_filter_r_3.setHighpass(0, mb_cross_freq_high, mb_q_high);
  //  mb_filter_l_3.setHighpass(1, mb_cross_freq_high, mb_q_high * 2);
  //  mb_filter_r_3.setHighpass(1, mb_cross_freq_high, mb_q_high * 2);
  //  mb_filter_l_3.setHighpass(2, mb_cross_freq_high, mb_q_high);
  //  mb_filter_r_3.setHighpass(2, mb_cross_freq_high, mb_q_high);
  //  mb_filter_l_3.setHighpass(3, mb_cross_freq_high, mb_q_high * 2);
  //  mb_filter_r_3.setHighpass(3, mb_cross_freq_high, mb_q_high * 2);

  // void UI_func_speedtest(uint8_t param)
//{ // ILI9341 478 msecs
//   // ILI9486 1242 msecs
//   if (LCDML.FUNC_setup())         // ****** SETUP *********
//   {
//     display.fillScreen(COLOR_BACKGROUND);
//     encoderDir[ENC_R].reset();
//     display.setTextSize(1);
//     display.setTextColor(GREY3);
//   }
//   if (LCDML.FUNC_loop())          // ****** LOOP *********
//   {
//     elapsedMillis msecs;
//     for (uint8_t t = 0; t < 30; t++)
//     {
//       for (uint8_t y = 0; y < 14; y++)
//       {
//         for (uint8_t x = 0; x < 25; x++)
//         {
//           setCursor_textGrid(x, y);
//           display.write(x * y + t);
//         }
//       }
//     }
//     display.setTextColor(GREEN);
//     display.setTextSize(2);
//     setCursor_textGrid(5, 5);
//     display.print(F("RESULT"));
//     setCursor_textGrid(5, 6);
//     display.print(msecs);
//     display.print(F(" msecs"));
//   }
//   if (LCDML.FUNC_close())     // ****** STABLE END *********
//   {
//     encoderDir[ENC_R].reset();
//     display.setTextSize(1);
//   }
// }

//   if (drum_config[i + fm.psram_skip_files].len / 1024 / 1024 > 0)
    // {
    //   snprintf_P(tmp, sizeof(tmp), PSTR("%4d"), (drum_config[i + fm.psram_skip_files].len / 1024 / 1024));
    //   display.print(tmp);
    //   display.print(" MB");
    // }

    // else
  // {
  //   display.setTextColor(RED, COLOR_BACKGROUND);
  //   display.setCursor(CHAR_width_small * 31, 6 * 11);
  //   display.print(F("NOT AVAILABLE"));
  //   display.setCursor(CHAR_width_small * 31, 7 * 11);
  //   display.print(F("WHILE SEQUENCER"));
  //   display.setCursor(CHAR_width_small * 31, 8 * 11);
  //   display.print(F("IS PLAYING"));
  // }

   // display.setCursor(CHAR_width_small * 14, 4 * CHAR_height_small);
    // display.setTextColor(fm.sd_is_folder ? GREEN : GREY2);
    // display.print(F("FOLDER"));
    // display.setCursor(CHAR_width_small * 9, 4 * CHAR_height_small);
    // display.setTextColor(fm.sd_is_folder ? GREY2 : GREEN);
    // display.print(F("FILE"));
    // display.setTextColor(fm.sd_is_folder ? COLOR_PITCHSMP : COLOR_SYSTEXT, COLOR_BACKGROUND);

    //void print_sidechain_level_indicators()
//{
  // if (sidechain_active && seq.running)
  // {
  //   // print_fast_level_indicator(17, 10, 100 - (float)sc_dexed1_current * 100, 0, 100);
  //   // print_fast_level_indicator(17, 11, 100 - (float)sc_dexed2_current * 100, 0, 100);
  //   // print_fast_level_indicator(17, 15, 100 - (float)sc_braids_current * 100, 0, 100);
  // }
  // print_fast_level_indicator(17, 12, random(100), 0, 100);
  // print_fast_level_indicator(17, 13, random(100), 0, 100);
  // print_fast_level_indicator(17, 14, random(100), 0, 100);

//}

// FLASHMEM uint8_t get_current_cursor_id(void)
// {
//   LCDMenuLib2_menu* tmp;

//   if ((tmp = LCDML.MENU_getCurrentObj()) != NULL)
//     return (tmp->getChild(LCDML.MENU_getCursorPosAbs())->getID());
//   else
//     return (0);
// }


// void seq_sub_print_track_assignments(int x, int y, bool init)
//{
//   if (seq.cycle_touch_element == 0)  // touch keyboard is off
//   {
//     display.setTextSize(1);
//     display.setTextColor(GREY2, COLOR_BACKGROUND);
//     for (uint8_t track = 0; track < NUM_SEQ_TRACKS; track++)
//     {
//       display.setCursor(CHAR_width_small * 36,  (track + 13) * (CHAR_height_small + 1) + 20  );
//       if (seq.menu - 21 == track)
//         set_track_type_color_inverted(track);
//       else
//         set_track_type_color(track);
//       display.print(F("T"));
//       display.print (track + 1);
//       display.setCursor(CHAR_width_small * 36 + 3 * CHAR_width_small,   (track + 13) * (CHAR_height_small + 1) + 20  );
//       if (seq.menu - 21 - NUM_SEQ_TRACKS == track)
//         set_track_type_color_inverted(track);
//       else
//         set_track_type_color(track);
//       if (seq.track_type[track] > 0 && seq.instrument[track] < 2)
//       {
//         display.print ("DEXED INST.");
//         display.print (seq.instrument[track] + 1);
//       }
//       else if (seq.track_type[track] > 0 && seq.instrument[track] == 2) //epiano
//       {
//         display.print ("ELECTR.PIANO");
//       }
//       else if (seq.track_type[track] > 0 && seq.instrument[track] == 3)  //MicroSynth 0
//       {
//         display.print ("MICROSYNTH 1");
//       }
//       else if (seq.track_type[track] > 0 && seq.instrument[track] == 4)  //MicroSynth 1
//       {
//         display.print ("MICROSYNTH 2");
//       }
//       else if (seq.instrument[track] == 5) //Braids
//       {
//         display.print ("BRAIDS OSC");
//       }
//       else if ( seq.instrument[track] > 5 && seq.instrument[track] < 16) //multisamples
//       {
//         display.print ("MULTISAMPLE");
//       }
//       else if (seq.instrument[track] > 15 && seq.instrument[track] < 32) //external MIDI USB
//       {
//         display.print ("MIDI USB #");
//         print_formatted_number(seq.instrument[track] - 15, 2);
//       }
//       else if (seq.instrument[track] > 31 && seq.instrument[track] < 48) //external MIDI MINI JACK/DIN
//       {
//         display.print ("MIDI DIN #");
//         print_formatted_number(seq.instrument[track] - 31, 2);
//       }
//       else
//       {
//         display.print ("-----------");
//       }
//     }
//   }
// }

// FLASHMEM void print_small_panbar_mixer(uint8_t x, uint8_t y, uint8_t input_value, uint8_t selected_option)
// {
//   setCursor_textGrid_small(x, y);
//   setModeColor(selected_option);
//   display.console = true;
//   display.drawRect(CHAR_width_small * x, 10 * y, 3 * CHAR_width_small, 7, COLOR_SYSTEXT);
//   display.console = true;
//   display.fillRect(CHAR_width_small * x + 1, 10 * y + 1, 3 * CHAR_width_small - 2, 5, COLOR_BACKGROUND);
//   display.fillRect(CHAR_width_small * x + 1 + input_value / 2.30, 10 * y + 1, 3, 5, COLOR_PITCHSMP);
// }

// Bar
  // if (vi == 0) {
  //   drawBitmap(CHAR_width, 2 * CHAR_height + 4, meter_bar[(uint8_t)(vf / 1.25 - 0.5)], 8, 8, COLOR_SYSTEXT, COLOR_BACKGROUND);
  //   for (uint8_t i = 1; i < display_cols - size; i++)
  //     display.fillRect(CHAR_width + i * 8, 2 * CHAR_height + 4, 8, 8, COLOR_BACKGROUND);  //empty block
  // } else if (vi == display_cols - size) {
  //   for (uint8_t i = 0; i < display_cols - size - 1; i++)
  //     display.fillRect(CHAR_width + i * 8, 2 * CHAR_height + 4, 8, 8, COLOR_BACKGROUND);  //empty block
  //   drawBitmap(CHAR_width + (display_cols - size - 1) * 8, 2 * CHAR_height + 4, meter_bar[7], 8, 8, COLOR_SYSTEXT, COLOR_BACKGROUND);
  // } else {
  //   for (uint8_t i = 0; i < display_cols - size + 2; i++)
  //     display.fillRect(CHAR_width + i * 8, 2 * CHAR_height + 4, 8, 8, COLOR_BACKGROUND);  //empty block
  //   drawBitmap(CHAR_width + vi * 8, 2 * CHAR_height + 4, meter_bar[(uint8_t)(vf / 1.25 - 0.5)], 8, 8, COLOR_SYSTEXT, COLOR_BACKGROUND);
  //  for (uint8_t i = vi + 1; i < display_cols - size + 2; i++)
  //    display.fillRect(CHAR_width + i * 8, 2 * CHAR_height + 4, 8, 8, COLOR_BACKGROUND);  //empty block
  // }

  //sample playing progress bar, only working in some cases

   // while (WAV_preview_PSRAM.isPlaying())
      // {
      //   display.fillRect(6, 189, (float)(DISPLAY_WIDTH - 8) / (WAV_preview_PSRAM.lengthMillis()) * WAV_preview_PSRAM.positionMillis() + 1, 7, RED);
      //   delay(25);
      // }
     // display.fillRect(6, 189, DISPLAY_WIDTH - 7, 7, COLOR_BACKGROUND);

 // while (WAV_preview_SD.isPlaying())
      // {
      //   display.fillRect(6, 189, (float)(DISPLAY_WIDTH - 8) / (WAV_preview_SD.lengthMillis()) * WAV_preview_SD.positionMillis() + 1, 7, RED);
      //   delay(25);
      // }
     // display.fillRect(6, 189, DISPLAY_WIDTH - 7, 7, COLOR_BACKGROUND);
    // while (WAV_preview_SD.isPlaying())
    // {
    //   display.fillRect(6, 222, (float)(DISPLAY_WIDTH - 8) / (WAV_preview_SD.lengthMillis()) * WAV_preview_SD.positionMillis() + 1, 5, RED);
    //   delay(25);
    // }
    // delay(15);
    // display.fillRect(6, 222, DISPLAY_WIDTH - 7, 5, COLOR_BACKGROUND);
    
// FLASHMEM void stop_preview_sample()
// {

// #ifdef COMPILE_FOR_SDCARD
//   ;
// #endif

//   if (WAV_preview_SD.isPlaying())
//     WAV_preview_SD.stop();

// #if defined(COMPILE_FOR_FLASH)
//   if (WAV_preview_FLASH.isPlaying())
//     WAV_preview_FLASH.stop();
// #endif

// #if defined(COMPILE_FOR_PSRAM)
//   if (WAV_preview_PSRAM.isPlaying())
//     WAV_preview_PSRAM.stop();
// #endif
// }


// [env:psram_resistive_touch]
// build_flags = 
//     ${common.build_flags}
//     -D COMPILE_FOR_PSRAM
//     -D PSRAM
//     ;-D ONBOARD_BUTTON_INTERFACE
//     ;-D MIDI_ACTIVITY_LIGHTS
// ; lib_deps =
// ;     ${env.lib_deps}
// custom_firmware_name = PSRAM-RESISTIVE_TOUCH


// [env:psram_capacitive_rgb_2sd]
// ; PSRAM_CAPACITIVE
// build_flags = 
//     ${common.build_flags}
//     -D COMPILE_FOR_PSRAM
//     -D PSRAM
//     -D CAPACITIVE_TOUCH_DISPLAY
//     -D DRUMS_FILTERS
//     -D MIDI_ACTIVITY_LIGHTS
//     -D RGB_ENCODERS
//     -D BUTTON_LEDS
//     ;-D ONBOARD_BUTTON_INTERFACE
//     ;-D MCP_23017
//     -D MCP_23008
//     -D MUSIKANDMORE
//     ;-D APC
//     ;-D DEBUG=1
// ; lib_deps =
// ;     ${env.lib_deps}
// custom_firmware_name = PSRAM-CAPACITIVE_TOUCH-RGB-2SD

// [env:progmem_capacitive_buttons]
// build_flags = 
//     ${common.build_flags}
//     -D COMPILE_FOR_PROGMEM
//     -D CAPACITIVE_TOUCH_DISPLAY
//     -D ONBOARD_BUTTON_INTERFACE
//     -D MIDI_ACTIVITY_LIGHTS
//     ;-D DEBUG=1
// ; lib_deps =
// ;     ${env.lib_deps}
// custom_firmware_name = PROGMEM-CAPACITIVE_TOUCH-BUTTONS_INTERFACE


