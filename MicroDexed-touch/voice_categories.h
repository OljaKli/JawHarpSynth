/*
 * voice_categories.h - Voice category management for MicroDexed
 * 
 * Part of MicroDexed
 * 
 * MIT License
 */

#ifndef VOICE_CATEGORIES_H
#define VOICE_CATEGORIES_H

#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "config.h"

// Search direction - use CAT_ prefix to avoid conflicts
#define CAT_LEFT 0
#define CAT_RIGHT 1

// Category IDs
enum VoiceCategory {
  // Instrument categories 
  CAT_NONE = 0,
  CAT_PIANO,
  CAT_EPIANO,
  CAT_ORGAN,
  CAT_STRINGS,
  CAT_PADS,
  CAT_LEAD,
  CAT_BASS,
  CAT_SYNTH_BASS,
  CAT_SYNTH_LEAD,
  CAT_BELLS,
  CAT_GUITAR,
  CAT_DRUMS,
  CAT_FX,
  CAT_BRASS,
  CAT_SYNTH_BRASS,
  CAT_WOODWINDS,
  CAT_VOCAL,
  CAT_KEYS,
  // Sound characteristics 
  CAT_SLOW_ATTACK,     // Slow attack pads/swells
  CAT_FAST_ATTACK,     // Percussive, immediate sounds
  CAT_LONG_RELEASE,    // Long sustaining sounds
  CAT_SHORT_DECAY,     // Plucked, struck, dampened sounds
  CAT_HARD,            // Aggressive, bright, hard sounds
  CAT_SOFT             // Mellow, soft, gentle sounds
};;

// Structure to hold category data for all 32 voices in a bank
struct BankCategoryData {
  uint8_t voice_categories[MAX_VOICES_PER_BANK][MAX_CATEGORIES_PER_VOICE];
  uint8_t voice_category_counts[MAX_VOICES_PER_BANK];
  char custom_categories[MAX_VOICES_PER_BANK][MAX_CATEGORY_NAME_LEN];
  bool has_data;
};

// Function prototypes
void get_category_name(uint8_t category, char *name);
bool load_bank_categories(uint8_t pool, uint8_t bank, BankCategoryData *data);
bool save_bank_categories(uint8_t pool, uint8_t bank, BankCategoryData *data);
void toggle_category_for_voice(uint8_t pool, uint8_t bank, uint8_t voice, uint8_t category);
bool voice_has_category(uint8_t pool, uint8_t bank, uint8_t voice, uint8_t category);
void draw_category_buttons(bool filter_mode);
void handle_category_touch();
bool voice_matches_categories(uint8_t pool, uint8_t bank, uint8_t voice);
void find_first_matching_voice();
void find_next_matching_voice();
void find_previous_matching_voice();
void print_cat_search_text(uint8_t direction, uint8_t bank);
void enter_category_mode();
void exit_category_mode();
void update_category_buttons_for_new_voice();  // Add this line

// Global state
extern bool category_mode_active;
extern uint8_t category_edit_mode;
extern uint8_t selected_categories[MAX_CATEGORIES];
extern uint8_t selected_category_count;

#endif