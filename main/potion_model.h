#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POTION_MATERIAL_COUNT 12
#define POTION_FORMAL_COUNT 6
#define POTION_TYPE_COUNT 7
#define POTION_MUDDY_ID 6
#define POTION_ATTRIBUTE_COUNT 8
#define POTION_CAULDRON_SLOTS 4
#define POTION_HISTORY_PER_TYPE 10
#define POTION_COOLDOWN_CAPACITY 48

typedef enum { POTION_SOURCE_WIFI = 0, POTION_SOURCE_BLE_STABLE, POTION_SOURCE_BLE_TEMP } potion_source_t;
typedef enum { POTION_RARITY_COMMON = 0, POTION_RARITY_RARE, POTION_RARITY_EPIC, POTION_RARITY_LEGENDARY } potion_rarity_t;
typedef enum {
    POTION_ATTR_HERB = 0, POTION_ATTR_MINERAL, POTION_ATTR_LIGHT, POTION_ATTR_DARK,
    POTION_ATTR_FIRE, POTION_ATTR_COLD, POTION_ATTR_THUNDER, POTION_ATTR_DRAGON
} potion_attribute_t;

typedef struct {
    uint64_t fingerprint;
    int64_t next_epoch;
    uint32_t session_id;
    uint8_t source_type;
    uint8_t used;
} potion_cooldown_t;

typedef struct {
    uint16_t material_id[POTION_CAULDRON_SLOTS];
    uint32_t last_used_generation;
    uint8_t used;
} potion_recipe_history_t;

typedef struct {
    uint16_t materials[POTION_MATERIAL_COUNT];
    uint16_t potions[POTION_TYPE_COUNT];
    uint32_t potion_seed[POTION_TYPE_COUNT];
    potion_recipe_history_t history[POTION_TYPE_COUNT][POTION_HISTORY_PER_TYPE];
    potion_cooldown_t cooldowns[POTION_COOLDOWN_CAPACITY];
    uint64_t lab_fingerprint;
    uint32_t generation;
    uint8_t lab_set;
    uint8_t showcase_enabled;
    uint8_t screen_timeout_index;
} potion_player_t;

typedef struct {
    uint16_t id;
    const char *name;
    const char *description;
    potion_rarity_t rarity;
    uint8_t properties[POTION_ATTRIBUTE_COUNT];
    uint32_t cooldown_sec;
} potion_material_def_t;

typedef struct {
    uint16_t id;
    const char *name;
    const char *description;
    potion_rarity_t rarity;
    uint8_t priority;
    uint8_t min_value[POTION_ATTRIBUTE_COUNT];
    uint8_t max_value[POTION_ATTRIBUTE_COUNT];
    uint8_t forbidden_mask;
} potion_def_t;

typedef struct {
    uint64_t fingerprint;
    int8_t rssi;
    uint16_t material_id;
    potion_source_t source;
} potion_material_signal_t;

typedef struct {
    uint64_t fingerprint;
    int8_t rssi;
    uint16_t material_id;
    potion_source_t source;
    int64_t cooldown_remaining;
    bool collectable;
} potion_radar_item_t;

typedef struct {
    uint64_t fingerprint;
    uint32_t started_ms;
    uint32_t elapsed_ms;
    uint16_t variation;
    int8_t last_rssi;
    uint8_t last_strength;
    uint8_t strength_history[5];
    uint8_t strength_count;
    uint8_t sample_count;
    bool initialized;
} potion_explore_progress_t;

typedef enum {
    POTION_DURATION_SECONDS = 0,
    POTION_DURATION_MINUTES,
    POTION_DURATION_HOURS,
    POTION_DURATION_DAYS,
} potion_duration_unit_t;

typedef struct {
    int8_t x;
    int8_t y;
} potion_radar_point_t;

typedef struct {
    int64_t epoch;
    uint32_t uptime_sec;
    uint32_t session_id;
    bool epoch_valid;
} potion_clock_t;

typedef struct {
    uint32_t last_activity_ms;
    bool screen_off;
} potion_idle_t;

void potion_player_defaults(potion_player_t *player);
const potion_material_def_t *potion_material_def(uint16_t id);
const potion_def_t *potion_def(uint16_t id);
uint16_t potion_material_for_signal(potion_source_t source, uint64_t fingerprint);
bool potion_collect(potion_player_t *player, potion_source_t source, uint64_t fingerprint,
                    uint16_t material_id, const potion_clock_t *clock,
                    int64_t *remaining_sec);
int64_t potion_cooldown_remaining(const potion_player_t *player, uint64_t fingerprint,
                                  const potion_clock_t *clock);
bool potion_cooldown_checkpoint(potion_player_t *player, const potion_clock_t *clock);
bool potion_cooldown_any_active(const potion_player_t *player,
                                const potion_clock_t *clock);
size_t potion_radar_aggregate(const potion_player_t *player, const potion_material_signal_t *signals,
                              size_t signal_count, const potion_clock_t *clock,
                              potion_radar_item_t *items, size_t capacity,
                              bool exclude_cooling);
size_t potion_radar_select(const potion_player_t *player, const potion_material_signal_t *signals,
                           size_t signal_count, const potion_clock_t *clock,
                           potion_radar_item_t *items, size_t capacity);
void potion_explore_reset(potion_explore_progress_t *progress);
uint8_t potion_explore_update(potion_explore_progress_t *progress, uint64_t fingerprint,
                              uint8_t strength, int8_t raw_rssi, uint32_t elapsed_ms);
uint8_t potion_explore_score(const potion_explore_progress_t *progress);
bool potion_explore_collectable(const potion_explore_progress_t *progress);
uint8_t potion_explore_score_at(const potion_explore_progress_t *progress, uint8_t threshold);
bool potion_explore_collectable_at(const potion_explore_progress_t *progress, uint8_t threshold);
uint8_t potion_signal_threshold_relax(uint8_t threshold, uint8_t strongest);
uint8_t potion_signal_threshold_recover(uint8_t threshold, uint8_t collected_strength);
uint32_t potion_duration_ceil(int64_t seconds, potion_duration_unit_t *unit);
uint8_t potion_signal_strength_percent(potion_source_t source, int8_t rssi);
void potion_radar_layout(const potion_radar_item_t *items, size_t count,
                         potion_radar_point_t *points);
void potion_property_total(const uint16_t materials[POTION_CAULDRON_SLOTS],
                           uint8_t totals[POTION_ATTRIBUTE_COUNT]);
uint16_t potion_match(const uint16_t materials[POTION_CAULDRON_SLOTS]);
bool potion_brew_candidate(const potion_player_t *player,
                           const uint16_t materials[POTION_CAULDRON_SLOTS],
                           potion_player_t *candidate, uint16_t *potion_id,
                           uint32_t *appearance_seed);
size_t potion_recipe_history_count(const potion_player_t *player, uint16_t potion_id);
const potion_recipe_history_t *potion_recipe_history_at(const potion_player_t *player,
                                                         uint16_t potion_id, size_t recent_index);
size_t potion_owned_material_types(const potion_player_t *player);
size_t potion_owned_potion_types(const potion_player_t *player);
void potion_idle_init(potion_idle_t *idle, uint32_t now_ms);
bool potion_idle_note_input(potion_idle_t *idle, uint32_t now_ms);
bool potion_idle_poll(potion_idle_t *idle, uint32_t now_ms, uint32_t timeout_ms);
