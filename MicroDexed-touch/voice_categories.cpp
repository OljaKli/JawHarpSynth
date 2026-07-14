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
#ifdef ESP_DISPLAY
#include "esp32.h"
#elif defined RA_DISPLAY
#include "RA8875.h"
#else
#include "ILI9341_t3n.h"
#endif

 // External references

#if defined ESP_DISPLAY
extern espdisplay display;
#elif defined RA_DISPLAY
extern RA8875 display;
#else
extern ILI9341_t3n display;
#endif

extern uint8_t selected_instance_id;
extern void print_voice_settings_in_dexed_voice_select(bool, bool);
extern void print_voice_select_default_help();
extern void printLiveModButtons();
extern void drawVoiceNameButton();
extern void UI_update_instance_icons();
//extern void draw_favorite_icon(uint8_t p, uint8_t b, uint8_t v, uint8_t instance_id);
extern void draw_algo(bool force);
extern void reset_live_modifiers();
extern void print_formatted_number(uint16_t number, uint8_t length);
extern void clear_search_text();

// Global state
bool category_mode_active = false;
uint8_t category_edit_mode = 0; // 0 = assign, 1 = filter
uint8_t selected_categories[MAX_CATEGORIES] = { 0 };
uint8_t selected_category_count = 0;

// Cache for button states (minimises redraws)
static bool button_state_cache[MAX_CATEGORIES] = { false };
static uint8_t cached_pool = 0;
static uint8_t cached_bank = 0;
static uint8_t cached_voice = 0;

// Cache for group states (which state is active, -1 = none)
static int8_t group_state_cache[6] = { -1, -1, -1, -1, -1, -1 };

static uint8_t saved_favorite_setting = 0;

// ------------------------------------------------------------------
// Helper: blink a button red twice
// ------------------------------------------------------------------
//     // We need the current label to redraw. For instrument buttons we can get from cache,
//     // but for groups we need the group name and state. Since this is called from contexts
//     // where we know the category/group, we'll recompute the label.
//     // For simplicity, we'll assume the caller knows how to get the label.
//     // In practice, we'll pass the label strings.
// }

// But better: we can store the button's current label when we call blink. 
// We'll modify the call sites to provide the label.

// However, to keep the code clean, we'll create a version that takes the two label strings.
static void blink_button_with_labels(uint16_t x, uint16_t y,
    const char* row1, const char* row2,
    TouchButton::Color original_color) {
    // Blink red twice
    for (int i = 0; i < 2; i++) {
        TouchButton::drawButton(x, y, row1, row2, TouchButton::BUTTON_RED);
        delay(200);
        TouchButton::drawButton(x, y, row1, row2, original_color);
        delay(200);
    }
}

// ------------------------------------------------------------------
// Multi‑state category groups – using new IDs
// ------------------------------------------------------------------
static const uint8_t attack_group[] = { CAT_SLOW_ATTACK, CAT_FAST_ATTACK };
static const uint8_t decay_group[] = { CAT_SHORT_DECAY, CAT_LONG_DECAY };
static const uint8_t release_group[] = { CAT_SHORT_RELEASE, CAT_LONG_RELEASE };
static const uint8_t tone_group[] = { CAT_HARD, CAT_SOFT };
static const uint8_t rating_group[] = { CAT_RATING1, CAT_RATING2, CAT_RATING3 };  // 3 states
static const uint8_t feel_group[] = { CAT_HAPPY, CAT_SAD, CAT_ANGRY, CAT_CALM,
                                         CAT_MYST, CAT_EPIC, CAT_PLAYFUL,
                                         CAT_DREAMY, CAT_FUNKY, CAT_ETHEREAL };

static const char* group_names[] = { "ATTACK", "DECAY", "RELEASE", "TONE", "RATING", "FEEL" };
static const uint8_t* group_arrays[] = { attack_group, decay_group, release_group,
                                         tone_group, rating_group, feel_group };
static const uint8_t group_sizes[] = { 2,2,2,2,3,10 };

// Helper: short label for group states (ASCII only)
static const char* get_group_state_label(uint8_t group_idx, int8_t state_idx) {
    if (state_idx < 0) return "-";
    switch (group_idx) {
    case 0: // ATTACK
        return state_idx == 0 ? "SLOW" : "FAST";
    case 1: // DECAY
        return state_idx == 0 ? "SHORT" : "LONG";
    case 2: // RELEASE
        return state_idx == 0 ? "SHORT" : "LONG";
    case 3: // TONE
        return state_idx == 0 ? "HARD" : "SOFT";
    case 4: // RATING – ASCII asterisks, 3 states
        switch (state_idx) {
        case 0: return "*";
        case 1: return "**";
        case 2: return "***";
        default: return "-";
        }
    case 5: // FEEL
        switch (state_idx) {
        case 0: return "HAPPY";
        case 1: return "SAD ";
        case 2: return "ANGRY";
        case 3: return "CALM";
        case 4: return "MYST";
        case 5: return "EPIC";
        case 6: return "PLAYFL";
        case 7: return "DREAMY";
        case 8: return "FUNKY";
        case 9: return "ETHRL";
        default: return "-";
        }
    default: return "-";
    }
}

// Helper: find which state of a group is active from button_state_cache
static int8_t get_active_group_state_from_cache(uint8_t group_idx) {
    const uint8_t* cats = group_arrays[group_idx];
    uint8_t size = group_sizes[group_idx];
    for (uint8_t i = 0; i < size; i++) {
        if (button_state_cache[cats[i]])
            return i;
    }
    return -1;
}

// Update group cache from button_state_cache
static void update_group_state_cache() {
    for (uint8_t g = 0; g < 6; g++) {
        group_state_cache[g] = get_active_group_state_from_cache(g);
    }
}

// ------------------------------------------------------------------
// Category name helpers – logical order matching new IDs
// ------------------------------------------------------------------
static const char* category_names[] = {
    "NONE",                                   // 0
    // Instruments (1‑18)
    "PIANO", "E.PIANO", "ORGAN", "STRINGS", "PAD", "LEAD",
    "BASS", "SYNTH.BASS", "SYNTH.LEAD", "BELLS", "GUITAR", "DRUMS.PERC",
    "FX", "BRASS", "SYNTH.BRASS", "WOOD.WIND", "VOCAL", "KEYS",
    // Attack (19‑20)
    "SLOW", "FAST",
    // Decay (21‑22)
    "SHORT", "LONG",
    // Release (23‑24)
    "SHORT", "LONG",
    // Tone (25‑26)
    "HARD", "SOFT",
    // Rating (27‑29)
    "*", "**", "***",
    // Feel (30‑39)
    "HAPPY", "SAD", "ANGRY", "CALM", "MYST", "EPIC",
    "PLAYFUL", "DREAMY", "FUNKY", "ETHEREAL"
};

FLASHMEM void get_category_name(uint8_t category, char* name) {
    if (category < sizeof(category_names) / sizeof(category_names[0]))
        strcpy(name, category_names[category]);
    else
        sprintf(name, "Cat%d", category);
}

FLASHMEM void get_category_labels(uint8_t category, char* row1, char* row2) {
    const char* name = (category < sizeof(category_names) / sizeof(category_names[0])) ?
        category_names[category] : "";
    uint8_t len = strlen(name);
    if (len <= 7) {
        strcpy(row1, name);
        row2[0] = '\0';
    }
    else if (strstr(name, ".") != NULL) {
        char* dot = strstr(name, ".");
        uint8_t split = dot - name;
        strncpy(row1, name, split);
        row1[split] = '\0';
        strcpy(row2, dot + 1);
    }
    else {
        strncpy(row1, name, 7);
        row1[7] = '\0';
        strcpy(row2, name + 7);
    }
}

// ------------------------------------------------------------------
// SD card load/save (unchanged)
// ------------------------------------------------------------------
FLASHMEM bool load_bank_categories(uint8_t pool, uint8_t bank, BankCategoryData* data) {
    char filename[CONFIG_FILENAME_LEN];
    File catFile;

    pool = constrain(pool, 0, DEXED_POOLS - 1);
    bank = constrain(bank, 0, MAX_BANKS - 1);

    data->has_data = false;
    for (uint8_t v = 0; v < MAX_VOICES_PER_BANK; v++) {
        data->voice_category_counts[v] = 0;
        data->custom_categories[v][0] = '\0';
    }

    snprintf(filename, sizeof(filename), "/%s/%d/%d/categories.json",
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

FLASHMEM bool save_bank_categories(uint8_t pool, uint8_t bank, BankCategoryData* data) {
    char filename[100], folder[100];
    File catFile;

    pool = constrain(pool, 0, DEXED_POOLS - 1);
    bank = constrain(bank, 0, MAX_BANKS - 1);

    snprintf(folder, sizeof(folder), "/%s/%d/%d", DEXED_CONFIG_PATH, pool, bank);
    // snprintf(filename, sizeof(filename), "%s/categories.json", folder);
    strlcpy(filename, folder, sizeof(filename));
    strlcat(filename, "/categories.json", sizeof(filename));

    if (sd_card_internal > 0) {
        if (!SD.exists(folder)) {
            SD.mkdir(folder);
        }

        bool has_any_data = false;
        for (uint8_t v = 0; v < MAX_VOICES_PER_BANK; v++) {
            if (data->voice_category_counts[v] > 0 || data->custom_categories[v][0] != '\0') {
                has_any_data = true;
                break;
            }
        }

        if (!has_any_data) {
            if (SD.exists(filename)) {
                SD.remove(filename);
#ifdef DEBUG
                LOG.print(F("Removed categories file: "));
                LOG.println(filename);
#endif
            }
            return true;
        }

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

// ------------------------------------------------------------------
// Basic category query/toggle (single category)
// ------------------------------------------------------------------
FLASHMEM bool voice_has_category(uint8_t pool, uint8_t bank, uint8_t voice, uint8_t category) {
    BankCategoryData data;
    if (!load_bank_categories(pool, bank, &data)) return false;
    voice = constrain(voice, 0, MAX_VOICES_PER_BANK - 1);
    for (uint8_t i = 0; i < data.voice_category_counts[voice]; i++) {
        if (data.voice_categories[voice][i] == category) return true;
    }
    return false;
}

// Batch version: set categories for a voice (replaces any existing)
static void set_voice_categories(uint8_t pool, uint8_t bank, uint8_t voice,
    const uint8_t* cats, uint8_t count) {
    BankCategoryData data;
    if (!load_bank_categories(pool, bank, &data)) {
        memset(&data, 0, sizeof(BankCategoryData));
    }
    voice = constrain(voice, 0, MAX_VOICES_PER_BANK - 1);

    // Replace all categories for this voice
    data.voice_category_counts[voice] = (count <= MAX_CATEGORIES_PER_VOICE) ? count : MAX_CATEGORIES_PER_VOICE;
    for (uint8_t i = 0; i < data.voice_category_counts[voice]; i++) {
        data.voice_categories[voice][i] = cats[i];
    }

    save_bank_categories(pool, bank, &data);

    // Efficiently update caches without reading SD again
    // Reset button state cache for all categories
    for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
        button_state_cache[cat] = false;
    }
    // Mark active categories
    for (uint8_t i = 0; i < count; i++) {
        uint8_t cat = cats[i];
        if (cat < MAX_CATEGORIES) {
            button_state_cache[cat] = true;
        }
    }
    // Update group cache
    update_group_state_cache();
}

FLASHMEM void toggle_category_for_voice(uint8_t pool, uint8_t bank, uint8_t voice, uint8_t category) {
    BankCategoryData data;
    if (!load_bank_categories(pool, bank, &data)) {
        memset(&data, 0, sizeof(BankCategoryData));
    }
    voice = constrain(voice, 0, MAX_VOICES_PER_BANK - 1);

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
    }
    else {
        // Try to add category
        if (data.voice_category_counts[voice] >= MAX_CATEGORIES_PER_VOICE) {
            // Limit reached – blink the button and abort
            if (category_mode_active && category_edit_mode == 0 && category <= 18) {
                uint8_t button_index = category - 1;
                uint8_t row = button_index / 6;
                uint8_t col = button_index % 6;
                uint16_t x = GRID.X[col];
                uint16_t y = GRID.Y[2 + row];
                char row1[8], row2[8];
                get_category_labels(category, row1, row2);
                TouchButton::Color original = button_state_cache[category] ?
                    TouchButton::BUTTON_HIGHLIGHTED :
                    TouchButton::BUTTON_NORMAL;
                blink_button_with_labels(x, y, row1, row2, original);
            }
            return;
        }
        data.voice_categories[voice][data.voice_category_counts[voice]++] = category;
    }

    save_bank_categories(pool, bank, &data);

    // Update button state cache
    button_state_cache[category] = !found;

    // Update group cache from current voice data (in memory, no SD read)
    for (uint8_t g = 0; g < 6; g++) {
        group_state_cache[g] = -1;
        for (uint8_t i = 0; i < group_sizes[g]; i++) {
            uint8_t cat = group_arrays[g][i];
            for (uint8_t j = 0; j < data.voice_category_counts[voice]; j++) {
                if (data.voice_categories[voice][j] == cat) {
                    group_state_cache[g] = i;
                    break;
                }
            }
            if (group_state_cache[g] != -1) break;
        }
    }

    // Redraw only this button if in assign mode and it's an instrument button
    if (category_mode_active && category_edit_mode == 0 && category <= 18) {
        uint8_t button_index = category - 1;
        uint8_t row = button_index / 6;
        uint8_t col = button_index % 6;
        uint16_t x = GRID.X[col];
        uint16_t y = GRID.Y[2 + row];
        char row1[8], row2[8];
        get_category_labels(category, row1, row2);
        TouchButton::Color btn_color = button_state_cache[category] ?
            TouchButton::BUTTON_HIGHLIGHTED :
            TouchButton::BUTTON_NORMAL;
        TouchButton::drawButton(x, y, row1, row2, btn_color);
    }
}

// ------------------------------------------------------------------
// Cache management (single SD read for full refresh)
// ------------------------------------------------------------------
FLASHMEM void update_button_state_cache() {
    BankCategoryData data;
    if (load_bank_categories(configuration.dexed[selected_instance_id].pool,
        configuration.dexed[selected_instance_id].bank,
        &data)) {
        uint8_t voice = configuration.dexed[selected_instance_id].voice;
        // Reset all to false
        for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
            button_state_cache[cat] = false;
        }
        // Mark active categories
        for (uint8_t i = 0; i < data.voice_category_counts[voice]; i++) {
            uint8_t cat = data.voice_categories[voice][i];
            if (cat < MAX_CATEGORIES) {
                button_state_cache[cat] = true;
            }
        }
        // Update group cache
        update_group_state_cache();
    }
    else {
        // No categories file, all false
        for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
            button_state_cache[cat] = false;
        }
        for (uint8_t g = 0; g < 6; g++) {
            group_state_cache[g] = -1;
        }
    }
    cached_pool = configuration.dexed[selected_instance_id].pool;
    cached_bank = configuration.dexed[selected_instance_id].bank;
    cached_voice = configuration.dexed[selected_instance_id].voice;
}

// ------------------------------------------------------------------
// Drawing the category buttons (6x4 grid) – same layout for both modes
// ------------------------------------------------------------------
FLASHMEM void draw_category_buttons(bool filter_mode) {
    static bool last_mode = false;          // remembers last drawn mode

    char row1[8], row2[8];

    display.setTextSize(1);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);

    bool full_redraw_needed = false;

    // 1. Cache invalidation detection (normal case)
    if (cached_pool != configuration.dexed[selected_instance_id].pool ||
        cached_bank != configuration.dexed[selected_instance_id].bank ||
        cached_voice != configuration.dexed[selected_instance_id].voice) {
        full_redraw_needed = true;
        update_button_state_cache();
    }

    // 2. Force redraw if we just entered category mode (cache was set to 255)
    if (cached_pool == 255) {
        full_redraw_needed = true;
        if (!full_redraw_needed) {   // cache was valid but we still need a fresh load
            update_button_state_cache();
            full_redraw_needed = true;
        }
    }

    // 3. Mode change detection – forces full redraw and updates last_mode
    if (filter_mode != last_mode) {
        full_redraw_needed = true;
        last_mode = filter_mode;
    }

    // Draw all 24 buttons
    for (uint8_t bi = 0; bi < 24; bi++) {
        uint8_t row = bi / 6;
        uint8_t col = bi % 6;
        uint16_t x = GRID.X[col];
        uint16_t y = GRID.Y[2 + row];

        if (filter_mode) {
            // Filter mode: always draw every button
            if (bi < 18) {
                uint8_t cat = bi + 1;
                bool is_selected = false;
                for (uint8_t i = 0; i < selected_category_count; i++) {
                    if (selected_categories[i] == cat) {
                        is_selected = true;
                        break;
                    }
                }
                get_category_labels(cat, row1, row2);
                TouchButton::Color btn_color = is_selected ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL;
                TouchButton::drawButton(x, y, row1, row2, btn_color);
            }
            else {
                uint8_t group_idx = bi - 18;
                const uint8_t* cats = group_arrays[group_idx];
                uint8_t size = group_sizes[group_idx];
                int8_t selected_state = -1;
                for (uint8_t i = 0; i < size; i++) {
                    for (uint8_t j = 0; j < selected_category_count; j++) {
                        if (selected_categories[j] == cats[i]) {
                            selected_state = i;
                            break;
                        }
                    }
                    if (selected_state != -1) break;
                }
                strcpy(row1, group_names[group_idx]);
                strcpy(row2, get_group_state_label(group_idx, selected_state));
                TouchButton::Color btn_color = (selected_state >= 0) ? TouchButton::BUTTON_ACTIVE : TouchButton::BUTTON_NORMAL;
                TouchButton::drawButton(x, y, row1, row2, btn_color);
            }
        }
        else {
            // Assign mode: draw only when full_redraw_needed is true
            if (full_redraw_needed) {
                if (bi < 18) {
                    uint8_t cat = bi + 1;
                    bool active = button_state_cache[cat];
                    get_category_labels(cat, row1, row2);
                    TouchButton::Color btn_color = active ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL;
                    TouchButton::drawButton(x, y, row1, row2, btn_color);
                }
                else {
                    uint8_t group_idx = bi - 18;
                    int8_t state = group_state_cache[group_idx];
                    strcpy(row1, group_names[group_idx]);
                    strcpy(row2, get_group_state_label(group_idx, state));
                    TouchButton::Color btn_color = (state >= 0) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL;
                    TouchButton::drawButton(x, y, row1, row2, btn_color);
                }
            }
        }
    }

    // Mode toggle button (always redrawn)
    if (filter_mode == 1)
        TouchButton::drawButton(GRID.X[5], GRID.Y[0], "MODE:", "FILTER", TouchButton::BUTTON_ACTIVE);
    else
        TouchButton::drawButton(GRID.X[5], GRID.Y[0], "MODE:", "ASSIGN", TouchButton::BUTTON_HIGHLIGHTED);
}

// ------------------------------------------------------------------
// Handle touches – instrument and group buttons
// ------------------------------------------------------------------
FLASHMEM void handle_category_touch() {
    for (uint8_t bi = 0; bi < 24; bi++) {
        uint8_t row = bi / 6;
        uint8_t col = bi % 6;
        uint16_t x = GRID.X[col];
        uint16_t y = GRID.Y[2 + row];

        if (TouchButton::isPressed(x, y)) {
            if (category_edit_mode == 0) {  // Assign mode
                if (bi < 18) {
                    // Instrument button: toggle single category
                    uint8_t cat = bi + 1;
                    toggle_category_for_voice(
                        configuration.dexed[selected_instance_id].pool,
                        configuration.dexed[selected_instance_id].bank,
                        configuration.dexed[selected_instance_id].voice,
                        cat);
                    // Redraw this button only (done in toggle_category_for_voice)
                }
                else {
                    // Group button: cycle through states
                    uint8_t group_idx = bi - 18;
                    const uint8_t* cats = group_arrays[group_idx];
                    uint8_t size = group_sizes[group_idx];

                    // Determine next state (cycle including none)
                    int8_t current = group_state_cache[group_idx];
                    int8_t next = (current + 1) % (size + 1); // size+1 includes "none"

                    // Build new category list for this voice
                    uint8_t new_cats[MAX_CATEGORIES_PER_VOICE];
                    uint8_t new_count = 0;

                    // First, copy all categories that are NOT from this group
                    BankCategoryData data;
                    if (load_bank_categories(
                        configuration.dexed[selected_instance_id].pool,
                        configuration.dexed[selected_instance_id].bank,
                        &data)) {
                        uint8_t voice = configuration.dexed[selected_instance_id].voice;
                        for (uint8_t i = 0; i < data.voice_category_counts[voice]; i++) {
                            uint8_t c = data.voice_categories[voice][i];
                            bool is_in_group = false;
                            for (uint8_t g = 0; g < size; g++) {
                                if (c == cats[g]) {
                                    is_in_group = true;
                                    break;
                                }
                            }
                            if (!is_in_group && new_count < MAX_CATEGORIES_PER_VOICE) {
                                new_cats[new_count++] = c;
                            }
                        }
                    }

                    // Check if adding the new state would exceed the limit
                    bool would_exceed = false;
                    if (next < size) { // adding a state (not "none")
                        if (new_count >= MAX_CATEGORIES_PER_VOICE) {
                            would_exceed = true;
                        }
                    }

                    if (would_exceed) {
                        // Blink the group button
                        const char* grp_name = group_names[group_idx];
                        char row1[8], row2[8];
                        strcpy(row1, grp_name);
                        strcpy(row2, get_group_state_label(group_idx, current));
                        TouchButton::Color original = (current >= 0) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL;
                        blink_button_with_labels(x, y, row1, row2, original);
                        return;
                    }

                    // Add the new state if not "none"
                    if (next < size && new_count < MAX_CATEGORIES_PER_VOICE) {
                        new_cats[new_count++] = cats[next];
                    }

                    // Batch update
                    set_voice_categories(
                        configuration.dexed[selected_instance_id].pool,
                        configuration.dexed[selected_instance_id].bank,
                        configuration.dexed[selected_instance_id].voice,
                        new_cats, new_count);

                    // Redraw this group button with new label
                    const char* grp_name = group_names[group_idx];
                    char row1[8], row2[8];
                    strcpy(row1, grp_name);
                    strcpy(row2, get_group_state_label(group_idx, next < size ? next : -1));
                    TouchButton::Color btn_color = (next < size) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL;
                    TouchButton::drawButton(x, y, row1, row2, btn_color);
                }
            }
            else {  // Filter mode: toggle selected category (including group states)
                if (bi < 18) {
                    // Instrument button: toggle single category
                    uint8_t cat = bi + 1;
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
                        for (uint8_t i = found_index; i < selected_category_count - 1; i++) {
                            selected_categories[i] = selected_categories[i + 1];
                        }
                        selected_category_count--;
                    }
                    else if (selected_category_count < MAX_CATEGORIES) {
                        selected_categories[selected_category_count++] = cat;
                    }
                }
                else {
                    // Group button: cycle through states in filter
                    uint8_t group_idx = bi - 18;
                    const uint8_t* cats = group_arrays[group_idx];
                    uint8_t size = group_sizes[group_idx];

                    // Find which state (if any) is currently selected
                    int8_t current_state = -1;
                    for (uint8_t i = 0; i < size; i++) {
                        for (uint8_t j = 0; j < selected_category_count; j++) {
                            if (selected_categories[j] == cats[i]) {
                                current_state = i;
                                break;
                            }
                        }
                        if (current_state != -1) break;
                    }

                    // Determine next state (cycle including none)
                    int8_t next_state = (current_state + 1) % (size + 1);

                    // Remove all categories of this group from filter
                    for (uint8_t i = 0; i < size; i++) {
                        for (uint8_t j = 0; j < selected_category_count; j++) {
                            if (selected_categories[j] == cats[i]) {
                                // Remove by shifting
                                for (uint8_t k = j; k < selected_category_count - 1; k++) {
                                    selected_categories[k] = selected_categories[k + 1];
                                }
                                selected_category_count--;
                                break; // each category appears only once
                            }
                        }
                    }

                    // Add the new state if not "none"
                    if (next_state < size && selected_category_count < MAX_CATEGORIES) {
                        selected_categories[selected_category_count++] = cats[next_state];
                    }
                }
                // Redraw all buttons in filter mode (full redraw to reflect changes)
                draw_category_buttons(true);
            }
            return; // only one button per touch
        }
    }
}

// ------------------------------------------------------------------
// Filter matching (unchanged – works on selected categories list)
// ------------------------------------------------------------------
FLASHMEM bool voice_matches_categories(uint8_t pool, uint8_t bank, uint8_t voice) {
    if (selected_category_count == 0) return true;
    BankCategoryData data;
    if (!load_bank_categories(pool, bank, &data)) return false;
    voice = constrain(voice, 0, MAX_VOICES_PER_BANK - 1);
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

// ------------------------------------------------------------------
// Navigation in filter mode (with search messages)
// ------------------------------------------------------------------
FLASHMEM void find_next_matching_voice() {
    if (selected_category_count == 0) return;
    uint8_t start_bank = configuration.dexed[selected_instance_id].bank;
    uint8_t start_voice = configuration.dexed[selected_instance_id].voice;
    uint8_t attempts = 0;
    uint8_t current_bank = start_bank;
    uint8_t current_voice = start_voice;

    // Show search indicator
    print_cat_search_text(CAT_RIGHT, current_bank);

    do {
        current_voice++;
        if (current_voice >= MAX_VOICES_PER_BANK) {
            current_voice = 0;
            current_bank++;
            if (current_bank >= MAX_BANKS) current_bank = 0;
            print_cat_search_text(CAT_RIGHT, current_bank);
        }
        attempts++;
        if (voice_matches_categories(configuration.dexed[selected_instance_id].pool,
            current_bank, current_voice)) {
            configuration.dexed[selected_instance_id].bank = current_bank;
            configuration.dexed[selected_instance_id].voice = current_voice;
            load_sd_voice(configuration.dexed[selected_instance_id].pool,
                current_bank, current_voice, selected_instance_id);
            reset_live_modifiers();

            // Do NOT update cache here – let it stay stale so that when returning to assign mode,
            // the mismatch triggers a full redraw.

            // Clear search text
            clear_search_text();
            return;
        }
    } while (attempts < (MAX_BANKS * MAX_VOICES_PER_BANK) &&
        !(current_bank == start_bank && current_voice == start_voice));

    clear_search_text();
}

FLASHMEM void find_previous_matching_voice() {
    if (selected_category_count == 0) return;
    uint8_t start_bank = configuration.dexed[selected_instance_id].bank;
    uint8_t start_voice = configuration.dexed[selected_instance_id].voice;
    uint8_t attempts = 0;
    uint8_t current_bank = start_bank;
    uint8_t current_voice = start_voice;

    // Show search indicator
    print_cat_search_text(CAT_LEFT, current_bank);

    do {
        if (current_voice > 0) current_voice--;
        else {
            current_voice = MAX_VOICES_PER_BANK - 1;
            if (current_bank > 0) current_bank--;
            else current_bank = MAX_BANKS - 1;
            print_cat_search_text(CAT_LEFT, current_bank);
        }
        attempts++;
        if (voice_matches_categories(configuration.dexed[selected_instance_id].pool,
            current_bank, current_voice)) {
            configuration.dexed[selected_instance_id].bank = current_bank;
            configuration.dexed[selected_instance_id].voice = current_voice;
            load_sd_voice(configuration.dexed[selected_instance_id].pool,
                current_bank, current_voice, selected_instance_id);
            reset_live_modifiers();

            // Do NOT update cache here – let it stay stale so that when returning to assign mode,
            // the mismatch triggers a full redraw.

            // Clear search text
            clear_search_text();
            return;
        }
    } while (attempts < (MAX_BANKS * MAX_VOICES_PER_BANK) &&
        !(current_bank == start_bank && current_voice == start_voice));

    clear_search_text();
}

FLASHMEM void print_cat_search_text(uint8_t direction, uint8_t bank) {
    display.setCursor(11 * CHAR_width_small, 2 * CHAR_height_small + 3);
    display.setTextSize(2);
    display.setTextColor(COLOR_PITCHSMP, COLOR_BACKGROUND);
    if (direction == CAT_LEFT) {
        display.print(F("<<SEARCH B"));
    }
    else {
        display.print(F("SEARCH>> B"));
    }
    print_formatted_number(bank, 2);
    display.setTextColor(COLOR_SYSTEXT, COLOR_BACKGROUND);
}

FLASHMEM void clear_search_text() {
    display.setCursor(11 * CHAR_width_small, 2 * CHAR_height_small + 3);
    display.setTextSize(2);
    display.print(F("            ")); // Clear 12 spaces
}

// ------------------------------------------------------------------
// Enter/exit category mode
// ------------------------------------------------------------------
FLASHMEM void enter_category_mode() {
    // Invalidate cache to force full redraw of all buttons
    cached_pool = 255;
    cached_bank = 255;
    cached_voice = 255;

    category_mode_active = true;
    category_edit_mode = 1;                     // 0 = assign, 1 = filter
    saved_favorite_setting = configuration.sys.favorites;
    configuration.sys.favorites = 0;
    display.console = true;

    display.fillRect(0, GRID.Y[2], DISPLAY_WIDTH, DISPLAY_HEIGHT - GRID.Y[2], COLOR_BACKGROUND);

    // Draw top row buttons
    TouchButton::drawButton(GRID.X[5], GRID.Y[0], "MODE:", "FILTER", TouchButton::BUTTON_ACTIVE);
    TouchButton::drawButton(GRID.X[4], GRID.Y[0], "SET AS", "FAV", TouchButton::BUTTON_ACTIVE);
    TouchButton::drawButton(GRID.X[5], GRID.Y[1], "VOICE", "CAT", TouchButton::BUTTON_RED);

    // Draw the category grid in filter mode (filter_mode = true)
    draw_category_buttons(true);
}

FLASHMEM void exit_category_mode() {
    category_mode_active = false;
    // selected_category_count = 0;
    configuration.sys.favorites = saved_favorite_setting;
    TouchButton::drawButton(GRID.X[5], GRID.Y[1], "VOICE", "CAT", TouchButton::BUTTON_NORMAL);
}

// ------------------------------------------------------------------
// Update buttons after voice change (assign mode only)
// ------------------------------------------------------------------
FLASHMEM void update_category_buttons_for_new_voice() {
    if (!category_mode_active || category_edit_mode != 0) return;

    bool new_states[MAX_CATEGORIES] = { false };
    // Instead of 41 SD reads, we load the bank categories once and update caches
    BankCategoryData data;
    if (load_bank_categories(configuration.dexed[selected_instance_id].pool,
        configuration.dexed[selected_instance_id].bank,
        &data)) {
        uint8_t voice = configuration.dexed[selected_instance_id].voice;
        for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
            new_states[cat] = false;
        }
        for (uint8_t i = 0; i < data.voice_category_counts[voice]; i++) {
            uint8_t cat = data.voice_categories[voice][i];
            if (cat < MAX_CATEGORIES) {
                new_states[cat] = true;
            }
        }
        // Update group cache from this data
        for (uint8_t g = 0; g < 6; g++) {
            group_state_cache[g] = -1;
            for (uint8_t i = 0; i < group_sizes[g]; i++) {
                if (new_states[group_arrays[g][i]]) {
                    group_state_cache[g] = i;
                    break;
                }
            }
        }
    }
    else {
        // No data, all false
        for (uint8_t cat = 1; cat < MAX_CATEGORIES; cat++) {
            new_states[cat] = false;
        }
        for (uint8_t g = 0; g < 6; g++) {
            group_state_cache[g] = -1;
        }
    }

    // Redraw only instrument buttons that changed (first 18)
    for (uint8_t bi = 0; bi < 18; bi++) {
        uint8_t cat = bi + 1;
        if (new_states[cat] != button_state_cache[cat]) {
            uint8_t row = bi / 6;
            uint8_t col = bi % 6;
            uint16_t x = GRID.X[col];
            uint16_t y = GRID.Y[2 + row];
            char row1[8], row2[8];
            get_category_labels(cat, row1, row2);
            TouchButton::Color btn_color = new_states[cat] ?
                TouchButton::BUTTON_HIGHLIGHTED :
                TouchButton::BUTTON_NORMAL;
            TouchButton::drawButton(x, y, row1, row2, btn_color);
            button_state_cache[cat] = new_states[cat];
        }
    }

    // Redraw all group buttons (6 buttons, fine)
    for (uint8_t bi = 18; bi < 24; bi++) {
        uint8_t group_idx = bi - 18;
        uint8_t row = bi / 6;
        uint8_t col = bi % 6;
        uint16_t x = GRID.X[col];
        uint16_t y = GRID.Y[2 + row];
        const char* grp_name = group_names[group_idx];
        int8_t state = group_state_cache[group_idx];
        char row1[8], row2[8];
        strcpy(row1, grp_name);
        strcpy(row2, get_group_state_label(group_idx, state));
        TouchButton::Color btn_color = (state >= 0) ? TouchButton::BUTTON_HIGHLIGHTED : TouchButton::BUTTON_NORMAL;
        TouchButton::drawButton(x, y, row1, row2, btn_color);
    }

    cached_pool = configuration.dexed[selected_instance_id].pool;
    cached_bank = configuration.dexed[selected_instance_id].bank;
    cached_voice = configuration.dexed[selected_instance_id].voice;
}
