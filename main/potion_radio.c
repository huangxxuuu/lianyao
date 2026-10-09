#include "potion_radio.h"
#include "potion_hash.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t SERVICE_UUID[16] = {0x8a,0x77,0x12,0xb4,0x61,0x49,0xa0,0x91,0x7d,0x44,0x50,0x50,0x01,0xe7,0xc3,0x5a};

typedef enum { RADIO_IDLE, RADIO_SCANNING, RADIO_ADVERTISING } radio_state_t;
static SemaphoreHandle_t s_lock;
static potion_radio_callback_t s_callback;
static void *s_context;
static radio_state_t s_state;
static volatile bool s_cancel;
static volatile bool s_adv_stop;
static volatile bool s_wifi_scan_active;
static volatile bool s_ble_scan_active;
static bool s_netif_ready;
static bool s_event_ready;

static SemaphoreHandle_t s_ble_done;
static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static potion_radio_event_t *s_scan_event;
static uint8_t s_addr_type;
static uint8_t s_adv_payload[25];

static void emit(const potion_radio_event_t *event) { if (s_callback) s_callback(event, s_context); }

static void set_idle(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = RADIO_IDLE;
    xSemaphoreGive(s_lock);
}

static bool reserve(radio_state_t next)
{
    bool ok = false;
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == RADIO_IDLE) { s_state = next; ok = true; }
    xSemaphoreGive(s_lock);
    return ok;
}

static esp_err_t network_prepare(void)
{
    esp_err_t err;
    if (!s_netif_ready) {
        err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        s_netif_ready = true;
    }
    if (!s_event_ready) {
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        s_event_ready = true;
    }
    return ESP_OK;
}

static void add_signal(potion_radio_event_t *event, potion_source_t source,
                       uint64_t fingerprint, int8_t rssi, bool stable, const char *ssid)
{
    if (!event || !fingerprint) return;
    for (size_t i = 0; i < event->count; ++i) {
        if (event->signals[i].fingerprint == fingerprint) {
            if (rssi > event->signals[i].rssi) event->signals[i].rssi = rssi;
            return;
        }
    }
    size_t at = event->count;
    if (at >= POTION_SCAN_MAX) {
        at = 0;
        for (size_t i = 1; i < event->count; ++i)
            if (potion_signal_strength_percent(event->signals[i].source, event->signals[i].rssi) <
                potion_signal_strength_percent(event->signals[at].source, event->signals[at].rssi)) at = i;
        if (potion_signal_strength_percent(source, rssi) <=
            potion_signal_strength_percent(event->signals[at].source, event->signals[at].rssi)) return;
    } else {
        event->count++;
    }
    event->signals[at] = (potion_signal_t){
        .fingerprint = fingerprint, .rssi = rssi, .source = source, .stable = stable
    };
    if (ssid) {
        strncpy(event->signals[at].ssid, ssid, sizeof(event->signals[at].ssid) - 1);
        event->signals[at].ssid[sizeof(event->signals[at].ssid) - 1] = '\0';
    }
}

static esp_err_t scan_wifi(potion_radio_event_t *event)
{
    esp_err_t err = network_prepare();
    if (err != ESP_OK) return err;
    esp_netif_config_t config = ESP_NETIF_DEFAULT_WIFI_STA();
    esp_netif_t *netif = esp_netif_new(&config);
    bool initialized = false, started = false, attached = false;
    if (!netif) return ESP_ERR_NO_MEM;
    err = esp_netif_attach_wifi_station(netif);
    if (err != ESP_OK) goto out;
    attached = true;
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) goto out;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) goto out;
    initialized = true;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) goto out;
    started = true;
    s_wifi_scan_active = true;
    err = esp_wifi_scan_start(NULL, true);
    s_wifi_scan_active = false;
    if (err == ESP_OK && !s_cancel) {
        uint16_t count = 16;
        wifi_ap_record_t records[16] = {0};
        err = esp_wifi_scan_get_ap_records(&count, records);
        for (uint16_t i = 0; err == ESP_OK && i < count; ++i) {
            uint64_t fp = potion_fingerprint("PP/WIFI/V1", records[i].bssid, sizeof(records[i].bssid));
            add_signal(event, POTION_SOURCE_WIFI, fp, records[i].rssi, true,
                       (const char *)records[i].ssid);
        }
    }
out:
    if (started) esp_wifi_stop();
    if (initialized) esp_wifi_deinit();
    if (attached) esp_netif_destroy_default_wifi(netif); else esp_netif_destroy(netif);
    return err;
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    xSemaphoreGive(s_host_stopped);
    vTaskSuspend(NULL);
}

static int scan_gap_event(struct ble_gap_event *gap, void *arg)
{
    (void)arg;
    if (gap->type == BLE_GAP_EVENT_DISC && s_scan_event && !s_cancel) {
        const struct ble_gap_disc_desc *d = &gap->disc;
        uint8_t key[31];
        size_t key_len;
        potion_source_t source;
        bool stable;
        if (d->length_data >= 8) {
            key_len = d->length_data > sizeof(key) ? sizeof(key) : d->length_data;
            memcpy(key, d->data, key_len);
            source = POTION_SOURCE_BLE_STABLE;
            stable = true;
        } else {
            key[0] = d->addr.type;
            memcpy(key + 1, d->addr.val, 6);
            key_len = 7;
            source = POTION_SOURCE_BLE_TEMP;
            stable = false;
        }
        uint64_t fp = potion_fingerprint(stable ? "PP/BLE/PAYLOAD/V1" : "PP/BLE/ADDR/V1", key, key_len);
        add_signal(s_scan_event, source, fp, d->rssi, stable, NULL);
    } else if (gap->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        xSemaphoreGive(s_ble_done);
    }
    return 0;
}

static void scan_sync(void)
{
    struct ble_gap_disc_params params = {0};
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    params.passive = 1;
    params.filter_duplicates = 1;
    if (rc == 0) rc = ble_gap_disc(s_addr_type, 3000, &params, scan_gap_event, NULL);
    if (rc != 0) xSemaphoreGive(s_ble_done);
}

static esp_err_t stop_nimble(void)
{
    esp_err_t err = ESP_OK;
    int rc = nimble_port_stop();
    if (rc != 0) err = ESP_FAIL;
    if (s_host_task && xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(2000)) != pdTRUE) err = ESP_ERR_TIMEOUT;
    if (s_host_task) { vTaskDelete(s_host_task); s_host_task = NULL; }
    if (nimble_port_deinit() != ESP_OK && err == ESP_OK) err = ESP_FAIL;
    vSemaphoreDelete(s_ble_done); s_ble_done = NULL;
    vSemaphoreDelete(s_host_stopped); s_host_stopped = NULL;
    return err;
}

static esp_err_t scan_ble(potion_radio_event_t *event)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_ble_done = xSemaphoreCreateBinary();
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_ble_done || !s_host_stopped) { err = ESP_ERR_NO_MEM; goto init_fail; }
    s_scan_event = event;
    ble_hs_cfg.sync_cb = scan_sync;
    if (xTaskCreatePinnedToCore(host_task, "potion_ble", NIMBLE_HS_STACK_SIZE, NULL,
                               configMAX_PRIORITIES - 4, &s_host_task, NIMBLE_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM; goto init_fail;
    }
    s_ble_scan_active = true;
    if (xSemaphoreTake(s_ble_done, pdMS_TO_TICKS(4500)) != pdTRUE) err = ESP_ERR_TIMEOUT;
    if (s_cancel) (void)ble_gap_disc_cancel();
    s_ble_scan_active = false;
    s_scan_event = NULL;
    {
        esp_err_t cleanup = stop_nimble();
        if (err == ESP_OK) err = cleanup;
    }
    return err;
init_fail:
    s_scan_event = NULL;
    if (s_host_task) stop_nimble();
    else {
        nimble_port_deinit();
        if (s_ble_done) vSemaphoreDelete(s_ble_done);
        if (s_host_stopped) vSemaphoreDelete(s_host_stopped);
        s_ble_done = s_host_stopped = NULL;
    }
    return err;
}

typedef struct {
    uint32_t request_id;
    bool wifi_only;
} scan_request_t;

static void scan_worker(void *arg)
{
    scan_request_t request = *(scan_request_t *)arg;
    free(arg);
    potion_radio_event_t event = {.type = POTION_RADIO_SCAN_DONE, .request_id = request.request_id};
    esp_err_t err = scan_wifi(&event);
    if (err == ESP_OK && !s_cancel && !request.wifi_only) err = scan_ble(&event);
    set_idle();
    if (err != ESP_OK && !s_cancel) { event.type = POTION_RADIO_SCAN_FAILED; event.error = err; }
    if (!s_cancel) emit(&event);
    vTaskDelete(NULL);
}

static uint16_t checksum16(const uint8_t *data, size_t length)
{
    uint16_t value = 0x6d31;
    for (size_t i = 0; i < length; ++i) value = (uint16_t)((value << 5) ^ (value >> 11) ^ data[i]);
    return value;
}

static int adv_gap_event(struct ble_gap_event *event, void *arg);
static int advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.svc_data_uuid128 = s_adv_payload;
    fields.svc_data_uuid128_len = sizeof(s_adv_payload);
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, adv_gap_event, NULL);
}

static int adv_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_ADV_COMPLETE && !s_adv_stop) (void)advertise();
    return 0;
}

static void adv_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) rc = advertise();
    potion_radio_event_t event = {.type = rc == 0 ? POTION_RADIO_SHOWCASE_STARTED : POTION_RADIO_SHOWCASE_FAILED,
                                  .error = rc == 0 ? ESP_OK : ESP_FAIL};
    emit(&event);
    if (rc != 0) s_adv_stop = true;
}

static void adv_worker(void *arg)
{
    (void)arg;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        potion_radio_event_t event = {.type=POTION_RADIO_SHOWCASE_FAILED,.error=err}; emit(&event);
        set_idle(); vTaskDelete(NULL); return;
    }
    s_host_stopped = xSemaphoreCreateBinary();
    ble_hs_cfg.sync_cb = adv_sync;
    if (!s_host_stopped || xTaskCreatePinnedToCore(host_task, "potion_adv", NIMBLE_HS_STACK_SIZE,
                                                   NULL, configMAX_PRIORITIES - 4,
                                                   &s_host_task, NIMBLE_CORE) != pdPASS) {
        potion_radio_event_t event = {.type=POTION_RADIO_SHOWCASE_FAILED,.error=ESP_ERR_NO_MEM}; emit(&event);
        if (s_host_stopped) vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL; nimble_port_deinit(); set_idle(); vTaskDelete(NULL); return;
    }
    while (!s_adv_stop) vTaskDelay(pdMS_TO_TICKS(50));
    (void)ble_gap_adv_stop();
    int rc = nimble_port_stop();
    if (rc == 0) (void)xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(2000));
    vTaskDelete(s_host_task); s_host_task = NULL;
    nimble_port_deinit();
    vSemaphoreDelete(s_host_stopped); s_host_stopped = NULL;
    set_idle();
    potion_radio_event_t event = {.type=POTION_RADIO_SHOWCASE_STOPPED}; emit(&event);
    vTaskDelete(NULL);
}

esp_err_t potion_radio_init(potion_radio_callback_t callback, void *context)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_callback = callback; s_context = context;
    return ESP_OK;
}

bool potion_radio_scan_start(uint32_t request_id, bool wifi_only)
{
    if (!reserve(RADIO_SCANNING)) return false;
    s_cancel = false;
    scan_request_t *request = malloc(sizeof(*request));
    if (!request) { set_idle(); return false; }
    *request = (scan_request_t){.request_id = request_id, .wifi_only = wifi_only};
    if (xTaskCreate(scan_worker, "potion_scan", 6144, request, 4, NULL) != pdPASS) {
        free(request);
        set_idle(); return false;
    }
    return true;
}

void potion_radio_cancel(void)
{
    s_cancel = true;
    if (s_wifi_scan_active) (void)esp_wifi_scan_stop();
    if (s_ble_scan_active) (void)ble_gap_disc_cancel();
}

bool potion_radio_showcase_start(uint16_t potion_id, uint32_t appearance_seed)
{
    if (!reserve(RADIO_ADVERTISING)) return false;
    memcpy(s_adv_payload, SERVICE_UUID, sizeof(SERVICE_UUID));
    s_adv_payload[16] = 1;
    s_adv_payload[17] = (uint8_t)potion_id;
    s_adv_payload[18] = (uint8_t)(potion_id >> 8);
    for (int i = 0; i < 4; ++i) s_adv_payload[19 + i] = (uint8_t)(appearance_seed >> (24 - i * 8));
    uint16_t sum = checksum16(s_adv_payload, 23);
    s_adv_payload[23] = (uint8_t)sum; s_adv_payload[24] = (uint8_t)(sum >> 8);
    s_adv_stop = false;
    if (xTaskCreate(adv_worker, "potion_show", 5120, NULL, 4, NULL) != pdPASS) {
        set_idle(); return false;
    }
    return true;
}

void potion_radio_showcase_stop(void) { s_adv_stop = true; }
bool potion_radio_busy(void) { return s_state != RADIO_IDLE; }
