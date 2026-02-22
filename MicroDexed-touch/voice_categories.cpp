/*
 * voice_categories.cpp - Voice category management for MicroDexed
 * Stores category metadata per voice in separate JSON files per bank
 * 
 * Part of MicroDexed
 * 
 * MIT License
 */

#include "voice_categories.h"
#include "config.h"
#include "dexed_sd.h"
#include "touchbutton.h"
#include "UI.h"
#include "ILI9341_t3n.h"

// External references
extern ILI9341_t3n display;
extern uint8_t selected_instance_id;
extern void print_voice_settings_in_dexed_voice_select(bool, bool);
extern void print_voice_select_default_help();
extern void printLiveModButtons();
extern void drawVoiceNameButton();
extern void UI_update_instance_icons();
extern void draw_favorite_icon(uint8_t p, uint8_t b, uint8_t v, uint8_t instance_id);
extern void draw_algo(bool force);
extern void reset_live_modifiers();

// Global state
bool category_mode_active = false;
uint8_t category_edit_mode = 0; // 0 = assign categories to current voice, 1 = search by categories
uint8_t selected_categories[MAX_CATEGORIES] = {0};
uint8_t selected_category_count = 0;

// Cache for current button states (to minimize redraws)
static bool button_state_cache[MAX_CATEGORIES] = {false};
static uint8_t cached_pool = 0;
static uint8_t cached_bank = 0;
static uint8_t cached_voice = 0;

// Saved favorite setting
static uint8_t saved_favorite_setting = 0;

// Category names for two-row display (max 7 chars per row)
// Updated category names array with characteristics as the 4th row
static const char* category_names[] = {
// Row 1-3: Instrument categories (existing, 23 including NONE)
"NONE",
"PIANO",
"E.PIANO",
"ORGAN",
"STRINGS",
"PAD",
"LEAD",
"BASS",
"SYNH.BASS",
"SYNH.LEAD",
"BELLS",
"GUITAR",
"DRUMS.PERC",
"FX",
"BRASS",
"SYNTH.BRASS",
"WOOD.WIND",
"VOCAL",
"KEYS",
"SLOW.ATK",   // Slow attack
"FAST.ATK",   // Fast attack
"LONG.REL",   // Long release
"SHORT.DCY",  // Short decay
"HARD",       // Hard/bright
"SOFT"        // Soft/mellow
};;

// Helper function to split category name into two rows (max 7 chars each)
FLASHMEM void get_category_labels(uint8_t category, char *row1, char *row2) {
  const char* name = "";
  if (category < sizeof(category_names)/sizeof(category_names[0])) {
    name = category_names[category];
  } else {
    sprintf(row1, "Cat%d", category);
    row2[0] = '\0';
    return;
  }
  
  // Split name into two rows
  uint8_t len = strlen(name);
  
  if (len <= 7) {
    // Short name fits in one row
    strcpy(row1, name);
    row2[0] = '\0';
  } else if (strstr(name, ".") != NULL) {
    // Has a dot, split at dot
    char* dot = strstr(name, ".");
    uint8_t split = dot - name;
    strncpy(row1, name, split);
    row1[split] = '\0';
    strcpy(row2, dot + 1);
  } else {
    // Split at 7 chars
    strncpy(row1, name, 7);
    row1[7] = '\0';
    strcpy(row2, name + 7);
  }
}

FLASHMEM void get_category_name(uint8_t category, char *name) {
  if (category < sizeof(category_names)/sizeof(category_names[0])) {
    strcpy(name, category_names[category]);
  } else {
    sprintf(name, "Cat%d", category);
  }
}

FLASHMEM bool load_bank_categories(uint8_t pool, uint8_t bank, BankCategoryData *data) {
  char filename[CONFIG_FILENAME_LEN];
  File catFile;
  
  pool = constrain(pool, 0, DEXED_POOLS - 1);
  bank = constrain(bank, 0, MAX_BANKS - 1);
  
  // Initialize with no data
  data->has_data = false;
  for (uint8_t v = 0; v < MAX_VOICES_PER_BANK; v++) {
    data->voice_category_counts[v] = 0;
    data->custom_categories[v][0] = '\0';
  }
  
  snprintf_P(filename, sizeof(filename), PSTR("/%s/%d/%d/categories.json"), 
             DEXED_CONFIG_PATH, pool, bank);
  
  if (sd_card_internal > 0 && SD.exists(filename)) {
    catFile = SD.open(filename);
    if (catFile) {
      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, catFile);
      catFile.close();
      
      if (!error) {
        JsonArray voices = doc["voices"].as<JsonArray>();
        uint8_t voice_index = 0;
        
        for (JsonVariant voice : voices) {
          if (voice_index >= MAX_VOICES_PER_BANK) break;
          
          JsonArray cats = voice["cat"].as<JsonArray>();
          data->voice_category_counts[voice_index] = 0;
          
          for (JsonVariant v : cats) {
            if (data->voice_category_counts[voice_index] < MAX_CATEGORIES_PER_VOICE) {
              data->voice_categories[voice_index][data->voice_category_counts[voice_index]++] = v.as<uint8_t>();
            }
          }
          
          if (voice.containsKey("custom")) {
            strlcpy(data->custom_categories[voice_index], voice["custom"], MAX_CATEGORY_NAME_LEN);
          }
          
          voice_index++;
        }
        
        data->has_data = true;
        
#ifdef DEBUG
        LOG.print(F("Loaded categories from: "));
        LOG.println(filename);
#endif
        
        return true;
      }
#ifdef DEBUG
      else {
        LOG.print(F("Error parsing categories file: "));
        LOG.println(filename);
      }
#endif
    }
  }
  
  return false;
}

FLASHMEM bool save_bank_categories(uint8_t pool, uint8_t bank, BankCategoryData *data) {
  char filename[100];  // Increased from CONFIG_FILENAME_LEN
  char folder[100];    // Increased from CONFIG_FILENAME_LEN
  File catFile;
  
  pool = constrain(pool, 0, DEXED_POOLS - 1);
  bank = constrain(bank, 0, MAX_BANKS - 1);
  
  snprintf_P(folder, sizeof(folder), PSTR("/%s/%d/%d"), 
             DEXED_CONFIG_PATH, pool, bank);
  snprintf_P(filename, sizeof(filename), PSTR("%s/categories.json"), folder);
  
  if (sd_card_internal > 0) {
    // Create folder if needed (but never delete it)
    if (!SD.exists(folder)) {
      SD.mkdir(folder);
    }
    
    // Check if there's any data to save
    bool has_any_data = false;
    for (uint8_t v = 0; v < MAX_VOICES_PER_BANK; v++) {
      if (data->voice_category_counts[v] > 0 || data->custom_categories[v][0] != '\0') {
        has_any_data = true;
        break;
      }
    }
    
    // If no data, remove the category file if it exists
    if (!has_any_data) {
      if (SD.exists(filename)) {
        SD.remove(filename);
#ifdef DEBUG
        LOG.print(F("Removed categories file: "));
        LOG.println(filename);
#endif
      }
      // DO NOT delete the folder - it contains the sysex file!
      return true;
    }
    
    // Remove existing file before writing new one
    if (SD.exists(filename)) {
      SD.remove(filename);
    }
    
    catFile = SD.open(filename, FILE_WRITE);
    if (catFile) {
      StaticJsonDocument<2048> doc;
      JsonArray voices = doc.createNestedArray("voices");
      
      for (uint8_t v = 0; v < MAX_VOICES_PER_BANK; v++) {
        JsonObject voiceObj = voices.createNestedObject();
        JsonArray cats = voiceObj.createNestedArray("cat");
        
        for (uint8_t c = 0; c < data->voice_category_counts[v]; c++) {
          cats.add(data->voice_categories[v][c]);
        }
        
        if (data->custom_categories[v][0] != '\0') {
          voiceObj["custom"] = data->custom_categories[v];
        }
      }
      
      serializeJson(doc, catFile);
      catFile.close();
      
#ifdef DEBUG
      LOG.print(F("Saved categories file: "));
      LOG.println(filename);
#endif
      
      return true;
    }
  }
  return false;
}

FLASHMEM void update_button_state_cache() {
  // Update cache for current voice
  for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
    button_state_cache[cat] = voice_has_category(
      configuration.dexed[selected_instance_id].pool,
      configuration.dexed[selected_instance_id].bank,
      configuration.dexed[selected_instance_id].voice,
      cat
    );
  }
  cached_pool = configuration.dexed[selected_instance_id].pool;
  cached_bank = configuration.dexed[selected_instance_id].bank;
  cached_voice = configuration.dexed[selected_instance_id].voice;
}

FLASHMEM void toggle_category_for_voice(uint8_t pool, uint8_t bank, uint8_t voice, uint8_t category) {
  BankCategoryData data;
  bool new_state = false; // Initialize to false
  
  // Load current data
  if (!load_bank_categories(pool, bank, &data)) {
    // Initialize new data structure
    memset(&data, 0, sizeof(BankCategoryData));
  }
  
  voice = constrain(voice, 0, MAX_VOICES_PER_BANK - 1);
  
  // Check if category already exists for this voice
  bool found = false;
  uint8_t found_index = 0;
  
  for (uint8_t i = 0; i < data.voice_category_counts[voice]; i++) {
    if (data.voice_categories[voice][i] == category) {
      found = true;
      found_index = i;
      break;
    }
  }
  
  if (found) {
    // Remove category
    for (uint8_t i = found_index; i < data.voice_category_counts[voice] - 1; i++) {
      data.voice_categories[voice][i] = data.voice_categories[voice][i + 1];
    }
    data.voice_category_counts[voice]--;
    new_state = false;
  } else if (data.voice_category_counts[voice] < MAX_CATEGORIES_PER_VOICE) {
    // Add category
    data.voice_categories[voice][data.voice_category_counts[voice]++] = category;
    new_state = true;
  }
  
  // Save back to SD
  save_bank_categories(pool, bank, &data);
  
  // Update cache
  button_state_cache[category] = new_state;
  
  // Redraw only this button if in assign mode
  if (category_mode_active && category_edit_mode == 0) {
    // Find button position
    uint8_t button_index = 0;
    for (uint8_t cat = 1; cat < category; cat++) {
      if (cat < MAX_CATEGORIES) button_index++;
    }
    uint8_t row = (button_index) / 6;
    uint8_t col = (button_index) % 6;
    
    uint16_t x = GRID.X[col];
    uint16_t y = GRID.Y[2 + row];
    
    char row1[8], row2[8];
    get_category_labels(category, row1, row2);
    
    TouchButton::Color button_color = new_state ? 
                                     TouchButton::BUTTON_HIGHLIGHTED : 
                                     TouchButton::BUTTON_NORMAL;
    TouchButton::drawButton(x, y, row1, row2, button_color);
  }
}

FLASHMEM bool voice_has_category(uint8_t pool, uint8_t bank, uint8_t voice, uint8_t category) {
  BankCategoryData data;
  
  if (!load_bank_categories(pool, bank, &data)) {
    return false;
  }
  
  voice = constrain(voice, 0, MAX_VOICES_PER_BANK - 1);
  
  for (uint8_t i = 0; i < data.voice_category_counts[voice]; i++) {
    if (data.voice_categories[voice][i] == category) {
      return true;
    }
  }
  return false;
}

FLASHMEM void draw_category_buttons(bool filter_mode) {
  char row1[8];
  char row2[8];
  uint8_t button_index = 0;
  
  // Draw title
  display.setTextSize(1);
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setCursor(10, GRID.Y[2] - 10);
  
  // if (filter_mode) {
   
  //   if (selected_category_count > 0) {
     
  //     for (uint8_t i = 0; i < min(selected_category_count, (uint8_t)3); i++) {
  //       get_category_name(selected_categories[i], row1);
  //       display.print(row1);
  //       if (i < min(selected_category_count, (uint8_t)3) - 1) display.print(", ");
  //     }
  //     if (selected_category_count > 3) display.print("...");
  //   }
  // } 
  
  // Check if we need to update all buttons (voice/bank/pool changed)
  bool full_redraw_needed = false;
  if (cached_pool != configuration.dexed[selected_instance_id].pool ||
      cached_bank != configuration.dexed[selected_instance_id].bank ||
      cached_voice != configuration.dexed[selected_instance_id].voice) {
    full_redraw_needed = true;
    update_button_state_cache();
  }
  
  // Draw category buttons - 6 columns x 3 rows using grid
  for (uint8_t cat = 1; cat < MAX_CATEGORIES && button_index < MAX_CATEGORIES; cat++) {
    uint8_t row = button_index / 6; // 6 buttons per row
    uint8_t col = button_index % 6;
    
    // Use grid positions - categories start at row 2, columns 0-5
    uint16_t x = GRID.X[col];
    uint16_t y = GRID.Y[2 + row]; // Stack vertically using grid rows
    
    get_category_labels(cat, row1, row2);
    
    TouchButton::Color button_color = TouchButton::BUTTON_NORMAL;
    bool current_state = false;
    
    if (filter_mode) {
      // In filter mode, check selected categories
      for (uint8_t i = 0; i < selected_category_count; i++) {
        if (selected_categories[i] == cat) {
          current_state = true;
          break;
        }
      }
      // In filter mode, always redraw all buttons (simpler)
      button_color = current_state ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL;
      TouchButton::drawButton(x, y, row1, row2, button_color);
    } else {
      // In assign mode, use cached state and only redraw if changed
      current_state = button_state_cache[cat];
      button_color = current_state ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL;
      
      // Only redraw if full redraw needed or this button's state changed
      if (full_redraw_needed || current_state != button_state_cache[cat]) {
        TouchButton::drawButton(x, y, row1, row2, button_color);
      }
    }
    
    button_index++;
  }
  
  TouchButton::drawButton(GRID.X[5], GRID.Y[0], "MODE:", 
                         filter_mode ? "FILTER" : "ASSIGN", 
                         TouchButton::BUTTON_ACTIVE);
  
}

FLASHMEM void handle_category_touch() {
  uint8_t button_index = 0;
  
  for (uint8_t cat = 1; cat < MAX_CATEGORIES && button_index < MAX_CATEGORIES; cat++) {
    uint8_t row = button_index / 6;
    uint8_t col = button_index % 6;
    
    uint16_t x = GRID.X[col];
    uint16_t y = GRID.Y[2 + row];
    
    if (TouchButton::isPressed(x, y)) {
      if (category_edit_mode == 0) {
        // Assign mode - toggle category for current voice
        toggle_category_for_voice(configuration.dexed[selected_instance_id].pool,
                                 configuration.dexed[selected_instance_id].bank,
                                 configuration.dexed[selected_instance_id].voice,
                                 cat);
        // Redraw only this button to show updated state
        char row1[8], row2[8];
        get_category_labels(cat, row1, row2);
        TouchButton::Color button_color = button_state_cache[cat] ? 
                                         TouchButton::BUTTON_HIGHLIGHTED : 
                                         TouchButton::BUTTON_NORMAL;
        TouchButton::drawButton(x, y, row1, row2, button_color);
      } else {
        // Filter mode - toggle selected category
        bool found = false;
        uint8_t found_index = 0;
        
        for (uint8_t i = 0; i < selected_category_count; i++) {
          if (selected_categories[i] == cat) {
            found = true;
            found_index = i;
            break;
          }
        }
        
        if (found) {
          // Remove from filter
          for (uint8_t i = found_index; i < selected_category_count - 1; i++) {
            selected_categories[i] = selected_categories[i + 1];
          }
          selected_category_count--;
        } else if (selected_category_count < MAX_CATEGORIES) {
          // Add to filter
          selected_categories[selected_category_count++] = cat;
        }
        

        // Redraw all buttons in filter mode (simpler than tracking filter state)
        draw_category_buttons(true);
        
        // If we have filter categories, find first matching voice
        // if (selected_category_count > 0) {
        //   find_first_matching_voice();
        // }
      }
    }
    
    button_index++;
  }
}

FLASHMEM bool voice_matches_categories(uint8_t pool, uint8_t bank, uint8_t voice) {
  if (selected_category_count == 0) return true; // No filter active
  
  BankCategoryData data;
  
  if (!load_bank_categories(pool, bank, &data)) {
    return false; // No category data for this bank
  }
  
  voice = constrain(voice, 0, MAX_VOICES_PER_BANK - 1);
  
  // Check if voice has all selected categories
  for (uint8_t i = 0; i < selected_category_count; i++) {
    bool found = false;
    for (uint8_t j = 0; j < data.voice_category_counts[voice]; j++) {
      if (data.voice_categories[voice][j] == selected_categories[i]) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  
  return true;
}

FLASHMEM void find_first_matching_voice() {
  if (selected_category_count == 0) return;
  
  uint8_t start_bank = configuration.dexed[selected_instance_id].bank;
  uint8_t start_voice = configuration.dexed[selected_instance_id].voice;
  uint8_t attempts = 0;
  bool found = false;
  
  // Start from current position and search forward
  uint8_t current_bank = start_bank;
  uint8_t current_voice = start_voice;
  
  do {
    if (voice_matches_categories(configuration.dexed[selected_instance_id].pool,
                                current_bank, current_voice)) {
      found = true;
      break;
    }
    
    // Move to next voice
    current_voice++;
    if (current_voice >= MAX_VOICES_PER_BANK) {
      current_voice = 0;
      current_bank++;
      if (current_bank >= MAX_BANKS) {
        current_bank = 0;
      }
    }
    
    attempts++;
    
  } while (attempts < (MAX_BANKS * MAX_VOICES_PER_BANK) && 
           !(current_bank == start_bank && current_voice == start_voice));
  
  if (found) {
    configuration.dexed[selected_instance_id].bank = current_bank;
    configuration.dexed[selected_instance_id].voice = current_voice;
    load_sd_voice(configuration.dexed[selected_instance_id].pool,
                 configuration.dexed[selected_instance_id].bank,
                 configuration.dexed[selected_instance_id].voice,
                 selected_instance_id);
    reset_live_modifiers();
    
    // Update only the buttons that changed
    if (category_mode_active && category_edit_mode == 0) {
      update_category_buttons_for_new_voice();
    }
  }
}

extern FLASHMEM void print_formatted_number(uint16_t number, uint8_t length);

FLASHMEM void clear_search_text(){
 display.setCursor(29 * CHAR_width_small, 2 * CHAR_height_small + 3);
  display.setTextSize(2);
  display.print(F("   "));
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
}

FLASHMEM void print_cat_search_text(uint8_t direction, uint8_t bank) {
  display.setCursor(11 * CHAR_width_small, 2 * CHAR_height_small + 3);
  display.setTextSize(2);
  display.setTextColor(COLOR_PITCHSMP, COLOR_BACKGROUND);
  
  if (direction == CAT_LEFT) {
    display.print(F("<<SEARCH B"));
  } else {
    display.print(F("SEARCH>> B"));
  }
  
    // Show just the bank number
  print_formatted_number(bank, 2);
  
  display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
  display.setTextSize(2);
}

FLASHMEM void find_next_matching_voice() {
  if (selected_category_count == 0) return;
  
  uint8_t start_bank = configuration.dexed[selected_instance_id].bank;
  uint8_t start_voice = configuration.dexed[selected_instance_id].voice;
  uint8_t attempts = 0;
  bool found = false;
  
  uint8_t current_bank = start_bank;
  uint8_t current_voice = start_voice;
  
  // Show search indicator
  print_cat_search_text(CAT_RIGHT, current_bank);
  
  do {
    // Move to next voice
    current_voice++;
    if (current_voice >= MAX_VOICES_PER_BANK) {
      current_voice = 0;
      current_bank++;
      if (current_bank >= MAX_BANKS) {
        current_bank = 0;
      }
      
      // Quick check: does this bank have a categories file?
      char filename[CONFIG_FILENAME_LEN];
      snprintf_P(filename, sizeof(filename), PSTR("/%s/%d/%d/categories.json"), 
                 DEXED_CONFIG_PATH, configuration.dexed[selected_instance_id].pool, current_bank);
      
      if (!SD.exists(filename)) {
        // Skip this entire bank - no categories file
        current_voice = MAX_VOICES_PER_BANK - 1; // Will wrap to 0 on next iteration
        continue;
      }
      
      print_cat_search_text(CAT_RIGHT, current_bank);
    }
    
    attempts++;
    
    if (voice_matches_categories(configuration.dexed[selected_instance_id].pool,
                                current_bank, current_voice)) {
      found = true;
      break;
    }
    
  } while (attempts < (MAX_BANKS * MAX_VOICES_PER_BANK) && 
           !(current_bank == start_bank && current_voice == start_voice));
  
  if (found) {
    configuration.dexed[selected_instance_id].bank = current_bank;
    configuration.dexed[selected_instance_id].voice = current_voice;
    load_sd_voice(configuration.dexed[selected_instance_id].pool,
                 current_bank, current_voice,
                 selected_instance_id);
    reset_live_modifiers();
  }
  
  // Clear search text
  clear_search_text();
}

FLASHMEM void find_previous_matching_voice() {
  if (selected_category_count == 0) return;
  
  uint8_t start_bank = configuration.dexed[selected_instance_id].bank;
  uint8_t start_voice = configuration.dexed[selected_instance_id].voice;
  uint8_t attempts = 0;
  bool found = false;
  
  uint8_t current_bank = start_bank;
  uint8_t current_voice = start_voice;
  
  // Show search indicator
  print_cat_search_text(CAT_LEFT, current_bank);
  
  do {
    // Move to previous voice
    if (current_voice > 0) {
      current_voice--;
    } else {
      current_voice = MAX_VOICES_PER_BANK - 1;
      if (current_bank > 0) {
        current_bank--;
      } else {
        current_bank = MAX_BANKS - 1;
      }
      
      // Quick check: does this bank have a categories file?
      char filename[CONFIG_FILENAME_LEN];
      snprintf_P(filename, sizeof(filename), PSTR("/%s/%d/%d/categories.json"), 
                 DEXED_CONFIG_PATH, configuration.dexed[selected_instance_id].pool, current_bank);
      
      if (!SD.exists(filename)) {
        // Skip this entire bank - no categories file
        current_voice = 0; // Will wrap to MAX_VOICES-1 on next iteration
        continue;
      }
      
      print_cat_search_text(CAT_LEFT, current_bank);
    }
    
    attempts++;
    
    if (voice_matches_categories(configuration.dexed[selected_instance_id].pool,
                                current_bank, current_voice)) {
      found = true;
      break;
    }
    
  } while (attempts < (MAX_BANKS * MAX_VOICES_PER_BANK) && 
           !(current_bank == start_bank && current_voice == start_voice));
  
  if (found) {
    configuration.dexed[selected_instance_id].bank = current_bank;
    configuration.dexed[selected_instance_id].voice = current_voice;
    load_sd_voice(configuration.dexed[selected_instance_id].pool,
                 current_bank, current_voice,
                 selected_instance_id);
    reset_live_modifiers();
    
    // Update only the buttons that changed
    if (category_mode_active && category_edit_mode == 0) {
      update_category_buttons_for_new_voice();
    }
  }
  
  // Clear search text
  clear_search_text();
}

// Add a function to update button states without full redraw
FLASHMEM void update_category_buttons_for_new_voice() {
  if (!category_mode_active || category_edit_mode != 0) return;
  
  // Get new state for current voice
  bool new_states[MAX_CATEGORIES] = {false};
  for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
    new_states[cat] = voice_has_category(
      configuration.dexed[selected_instance_id].pool,
      configuration.dexed[selected_instance_id].bank,
      configuration.dexed[selected_instance_id].voice,
      cat
    );
  }
  
  // Update cache and redraw only buttons that changed
  uint8_t button_index = 0;
  for (uint8_t cat = 1; cat < MAX_CATEGORIES && button_index < MAX_CATEGORIES; cat++) {
    if (new_states[cat] != button_state_cache[cat]) {
      // Button state changed - redraw it
      uint8_t row = button_index / 6;
      uint8_t col = button_index % 6;
      
      uint16_t x = GRID.X[col];
      uint16_t y = GRID.Y[2 + row];
      
      char row1[8], row2[8];
      get_category_labels(cat, row1, row2);
      
      TouchButton::Color button_color = new_states[cat] ? 
                                       TouchButton::BUTTON_HIGHLIGHTED : 
                                       TouchButton::BUTTON_NORMAL;
      TouchButton::drawButton(x, y, row1, row2, button_color);
      
      // Update cache
      button_state_cache[cat] = new_states[cat];
    }
    button_index++;
  }
  
  // Update cache tracking variables
  cached_pool = configuration.dexed[selected_instance_id].pool;
  cached_bank = configuration.dexed[selected_instance_id].bank;
  cached_voice = configuration.dexed[selected_instance_id].voice;
}

FLASHMEM void enter_category_mode() {
  category_mode_active = true;
  category_edit_mode = 0; // Start in assign mode
  
  // Save current favorite setting before disabling it
  saved_favorite_setting = configuration.sys.favorites;
  
  // Disable favorite filtering when entering category mode
  configuration.sys.favorites = 0;
  
  // Enable console mode for display operations
  display.console = true;
  
  // Clear everything from row 2 to the bottom of the screen
  // This preserves rows 0 and 1 (where FAV, CAT, and V.KEYB buttons are)
  display.fillRect(0, GRID.Y[2], DISPLAY_WIDTH, DISPLAY_HEIGHT , COLOR_BACKGROUND);
  
  TouchButton::drawButton(GRID.X[5], GRID.Y[0], "MODE:", 
                         "ASSIGN", 
                         TouchButton::BUTTON_ACTIVE);
  
  // Ensure top row buttons are still visible
  TouchButton::drawButton(GRID.X[4], GRID.Y[0], "SET AS", "FAV", TouchButton::BUTTON_ACTIVE);
  
  // CATEGORY button should be RED when active
  TouchButton::drawButton(GRID.X[5], GRID.Y[1], "VOICE", "CAT", TouchButton::BUTTON_RED);
  
  // Initialize cache for current voice
  for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
    button_state_cache[cat] = voice_has_category(
      configuration.dexed[selected_instance_id].pool,
      configuration.dexed[selected_instance_id].bank,
      configuration.dexed[selected_instance_id].voice,
      cat
    );
  }
  
  // Set cache tracking variables to current values
  cached_pool = configuration.dexed[selected_instance_id].pool;
  cached_bank = configuration.dexed[selected_instance_id].bank;
  cached_voice = configuration.dexed[selected_instance_id].voice;
  
  // Draw all category buttons (full redraw)
  char row1[8], row2[8];
  uint8_t button_index = 0;
  for (uint8_t cat = 1; cat < MAX_CATEGORIES && button_index < MAX_CATEGORIES; cat++) {
    uint8_t row = button_index / 6;
    uint8_t col = button_index % 6;
    
    uint16_t x = GRID.X[col];
    uint16_t y = GRID.Y[2 + row];
    
    get_category_labels(cat, row1, row2);
    
    TouchButton::Color button_color = button_state_cache[cat] ? 
                                     TouchButton::BUTTON_HIGHLIGHTED : 
                                     TouchButton::BUTTON_NORMAL;
    TouchButton::drawButton(x, y, row1, row2, button_color);
    
    button_index++;
  }
}

FLASHMEM void exit_category_mode() {
  category_mode_active = false;
  selected_category_count = 0;
   // memset(selected_categories, 0, sizeof(selected_categories));
  // Restore the previous favorite setting
  configuration.sys.favorites = saved_favorite_setting;
  TouchButton::drawButton(GRID.X[5], GRID.Y[1], "VOICE", "CAT", TouchButton::BUTTON_NORMAL);
}