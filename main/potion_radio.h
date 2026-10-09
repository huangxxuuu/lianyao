#pragma once

#include "esp_err.h"
#include "potion_model.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POTION_SCAN_MAX 10

typedef struct {
    uint64_t fingerprint;
    int8_t rssi;
    potion_source_t source;
    bool stable;
    char ssid[33];
} potion_signal_t;

typedef enum {
    POTION_RADIO_SCAN_DONE,
    POTION_RADIO_SCAN_FAILED,
    POTION_RADIO_SHOWCASE_STARTED,
    POTION_RADIO_SHOWCASE_STOPPED,
    POTION_RADIO_SHOWCASE_FAILED,
} potion_radio_event_type_t;

typedef struct {
    potion_radio_event_type_t type;
    uint32_t request_id;
    esp_err_t error;
    size_t count;
    potion_signal_t signals[POTION_SCAN_MAX];
} potion_radio_event_t;

typedef void (*potion_radio_callback_t)(const potion_radio_event_t *event, void *context);

esp_err_t potion_radio_init(potion_radio_callback_t callback, void *context);
bool potion_radio_scan_start(uint32_t request_id, bool wifi_only);
void potion_radio_cancel(void);
bool potion_radio_showcase_start(uint16_t potion_id, uint32_t appearance_seed);
void potion_radio_showcase_stop(void);
bool potion_radio_busy(void);
