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

// Category IDs – completely reordered into logical groups
enum VoiceCategory {
    CAT_NONE = 0,

    // Instrument categories (1–18)
    CAT_PIANO = 1,
    CAT_EPIANO = 2,
    CAT_ORGAN = 3,
    CAT_STRINGS = 4,
    CAT_PADS = 5,
    CAT_LEAD = 6,
    CAT_BASS = 7,
    CAT_SYNTH_BASS = 8,
    CAT_SYNTH_LEAD = 9,
    CAT_BELLS = 10,
    CAT_GUITAR = 11,
    CAT_DRUMS = 12,
    CAT_FX = 13,
    CAT_BRASS = 14,
    CAT_SYNTH_BRASS = 15,
    CAT_WOODWINDS = 16,
    CAT_VOCAL = 17,
    CAT_KEYS = 18,

    // Attack (19–20)
    CAT_SLOW_ATTACK = 19,
    CAT_FAST_ATTACK = 20,

    // Decay (21–22)
    CAT_SHORT_DECAY = 21,
    CAT_LONG_DECAY = 22,

    // Release (23–24)
    CAT_SHORT_RELEASE = 23,
    CAT_LONG_RELEASE = 24,

    // Tone (25–26)
    CAT_HARD = 25,
    CAT_SOFT = 26,

    // Rating (27–29)
    CAT_RATING1 = 27,
    CAT_RATING2 = 28,
    CAT_RATING3 = 29,

    // Feel (30–39)
    CAT_HAPPY = 30,
    CAT_SAD = 31,
    CAT_ANGRY = 32,
    CAT_CALM = 33,
    CAT_MYST = 34,
    CAT_EPIC = 35,
    CAT_PLAYFUL = 36,
    CAT_DREAMY = 37,
    CAT_FUNKY = 38,
    CAT_ETHEREAL = 39
};

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
void find_next_matching_voice();
void find_previous_matching_voice();
void print_cat_search_text(uint8_t direction, uint8_t bank);
void enter_category_mode();
void exit_category_mode();
void update_category_buttons_for_new_voice();

// Global state
extern bool category_mode_active;
extern uint8_t category_edit_mode;
extern uint8_t selected_categories[MAX_CATEGORIES];
extern uint8_t selected_category_count;

#endif