/*
 * wireless_mgr.c — Shared scan-result store + WiFi / ESP-NOW / BLE init
 *
 * Initialises the full wireless stack in one call:
 *   1. NVS flash (required by WiFi)
 *   2. Enables the C6 wireless module via BSP IO expander
 *   3. Brings up WiFi in STA mode (via esp_wifi_remote on ESP32-P4)
 *   4. Initialises ESP-NOW
 *   5. Initialises NimBLE (BLE host, guarded by CONFIG_BT_NIMBLE_ENABLED)
 *
 * Thread safety: the shared node list is protected by a FreeRTOS mutex.
 * Callers from WiFi / BLE tasks MUST NOT hold bsp_display_lock when
 * calling these functions.
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "bsp/esp-bsp.h"           /* bsp_feature_enable()  */
#include "wireless_mgr.h"

static const char *TAG = "wireless_mgr";

/* ── Shared result list ──────────────────────────────────────────── */
static wireless_node_t   s_nodes[WIRELESS_MAX_NODES];
static SemaphoreHandle_t s_mutex        = NULL;
static int               s_node_count   = 0;   /* cached count (updated under lock) */

/* Global receive callback — set by the application layer */
wireless_recv_cb_t g_wireless_recv_cb = NULL;

/* ── Internal helper (called by espnow_mgr.c and ble_mgr.c) ─────── */
/*
 * _wireless_add_node() — add or refresh a node in the shared list.
 *
 * Returns:
 *   ESP_OK           → new node added (caller should fire result_cb)
 *   ESP_ERR_NOT_FINISHED → duplicate; RSSI updated but no new node
 *   ESP_ERR_NO_MEM   → list is full
 */
esp_err_t _wireless_add_node(const wireless_node_t *n)
{
    if (!n || !n->valid || !s_mutex) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Duplicate check by MAC */
    for (int i = 0; i < WIRELESS_MAX_NODES; i++) {
        if (s_nodes[i].valid && memcmp(s_nodes[i].mac, n->mac, 6) == 0) {
            /* Refresh signal strength so UI stays current */
            memcpy(s_nodes[i].signal, n->signal, sizeof(n->signal));
            xSemaphoreGive(s_mutex);
            return ESP_ERR_NOT_FINISHED;
        }
    }

    /* Find free slot */
    for (int i = 0; i < WIRELESS_MAX_NODES; i++) {
        if (!s_nodes[i].valid) {
            s_nodes[i] = *n;
            s_node_count++;
            xSemaphoreGive(s_mutex);
            return ESP_OK;   /* new node — caller fires result_cb */
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_ERR_NO_MEM;
}

/* ── Public API ──────────────────────────────────────────────────── */

int wireless_get_results(wireless_node_t *out, int max_count)
{
    if (!out || max_count <= 0 || !s_mutex) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int count = 0;
    for (int i = 0; i < WIRELESS_MAX_NODES && count < max_count; i++) {
        if (s_nodes[i].valid) out[count++] = s_nodes[i];
    }
    xSemaphoreGive(s_mutex);
    return count;
}

int wireless_node_count(void)
{
    /* Quick read under lock */
    if (!s_mutex) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int c = s_node_count;
    xSemaphoreGive(s_mutex);
    return c;
}

void wireless_clear_results(void)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_nodes, 0, sizeof(s_nodes));
    s_node_count = 0;
    xSemaphoreGive(s_mutex);
}

void wireless_register_recv_cb(wireless_recv_cb_t cb)
{
    g_wireless_recv_cb = cb;
}

wireless_status_cb_t g_wireless_status_cb = NULL;

void wireless_register_status_cb(wireless_status_cb_t cb)
{
    g_wireless_status_cb = cb;
}

/* ── Init ────────────────────────────────────────────────────────── */

esp_err_t wireless_init(void)
{
    esp_err_t ret;

    /* Mutex for shared node list */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    memset(s_nodes, 0, sizeof(s_nodes));
    s_node_count = 0;

    /* ── 1. NVS flash (required by WiFi driver) ── */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── 2. Enable C6 wireless module via BSP IO expander ── */
    ret = bsp_feature_enable(BSP_FEATURE_WIFI, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi module enable failed (%s), BSP init may not have run yet",
                 esp_err_to_name(ret));
        /* Non-fatal: display init may come before wireless */
    }
    vTaskDelay(pdMS_TO_TICKS(150));   /* allow C6 to power up */

    /* ── 3. TCP/IP adapter + WiFi STA ── */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    bool wifi_ok = false;
    ret = esp_wifi_init(&wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        /* Don't return — still attempt BLE init below */
    } else {
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_mode(WIFI_MODE_STA);
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        } else {
            wifi_ok = true;
        }
    }

    /* ── 4. ESP-NOW (not supported on ESP32-P4) ── */
#if !CONFIG_IDF_TARGET_ESP32P4
    if (wifi_ok) {
        esp_err_t enow_ret = esp_now_init();
        if (enow_ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_init failed: %s", esp_err_to_name(enow_ret));
        } else {
            ESP_LOGI(TAG, "ESP-NOW initialized successfully");
        }
    }
#else
    ESP_LOGW(TAG, "ESP-NOW not supported on ESP32-P4");
#endif

    /* ── 5. BLE stack (NimBLE host on P4, controller on C6 via HCI) ── */
    bool ble_ok = false;
#if CONFIG_BT_NIMBLE_ENABLED
    extern esp_err_t ble_stack_init(void);
    ret = ble_stack_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE stack init failed: %s — BLE scanning disabled",
                 esp_err_to_name(ret));
    } else {
        ble_ok = true;
    }
#else
    ESP_LOGW(TAG, "CONFIG_BT_NIMBLE_ENABLED not set — BLE scanning disabled");
#endif

    if (!wifi_ok && !ble_ok) {
        ESP_LOGE(TAG, "Both WiFi and BLE failed to start (is C6 powered?)");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Wireless stack ready (WiFi:%s BLE:%s)",
             wifi_ok ? "OK" : "FAIL", ble_ok ? "OK" : "FAIL");
    return ESP_OK;
}
