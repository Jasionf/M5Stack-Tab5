/*
 * espnow_mgr.c — ESP-NOW peer discovery and data exchange
 *
 * Discovery protocol:
 *   This device broadcasts WHO_IS_HERE beacons while scanning. Any ESP-NOW
 *   node that understands the protocol can reply with I_AM_HERE.
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "wireless_mgr.h"

/* ESP-NOW not supported on ESP32-P4 (esp_wifi_remote doesn't implement it) */
#if !CONFIG_IDF_TARGET_ESP32P4

static const char *TAG = "espnow_mgr";

#define ESPNOW_MAGIC          0x454E5701UL
#define ESPNOW_TYPE_WHO       0x01
#define ESPNOW_TYPE_IAM       0x02
#define ESPNOW_TYPE_DATA      0x03

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  type;
    char     device_name[32];
    uint8_t  channel;
    uint8_t  _pad[2];
} espnow_pkt_t;

static wireless_result_cb_t s_scan_cb  = NULL;
static bool                  s_scanning = false;
static TimerHandle_t         s_who_tmr  = NULL;
static uint8_t               s_scan_ch  = 1;
static uint32_t              s_scan_elapsed_ms = 0;

#define ESPNOW_SCAN_WINDOW_MS 5000
#define ESPNOW_SCAN_STEP_MS    300

static uint8_t s_peer_mac[6] = {0};
static bool    s_connected   = false;

static const uint8_t k_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* Known CardKB2 device MAC (88:56:a6:c3:02:54 in little-endian) */
static const uint8_t k_cardkb2_mac[6] = {0x54, 0x02, 0xC3, 0xA6, 0x56, 0x88};

extern esp_err_t _wireless_add_node(const wireless_node_t *n);
extern wireless_recv_cb_t g_wireless_recv_cb;

static uint8_t current_channel(void)
{
    uint8_t ch = 1;
    wifi_second_chan_t sec;
    esp_wifi_get_channel(&ch, &sec);
    return ch;
}

static void set_scan_channel(uint8_t ch)
{
    if (ch < 1 || ch > 13) return;
    esp_err_t ret = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_channel(%u) failed: %s", ch, esp_err_to_name(ret));
    }
}

static void send_beacon(uint8_t type, const uint8_t dest[6])
{
    espnow_pkt_t pkt = {
        .magic   = ESPNOW_MAGIC,
        .type    = type,
        .channel = current_channel(),
    };
    strncpy(pkt.device_name, "TAB5", sizeof(pkt.device_name) - 1);
    esp_now_send(dest, (const uint8_t *)&pkt, sizeof(pkt));
}

static void espnow_recv_cb(const esp_now_recv_info_t *info,
                           const uint8_t *data, int len)
{
    if (!info || !data || len <= 0) {
        return;
    }

    /* KB factory firmware key frame: [0xAA][0x03][key_id][state][checksum] */
    if (len == 5 && data[0] == 0xAA && data[1] == 0x03) {
        /* Always add KB to scan results when we receive key frames */
        if (s_scanning) {
            wireless_node_t n = {0};
            n.is_bt = false;
            n.valid = true;
            snprintf(n.name, sizeof(n.name), "CardKB2-%02X%02X%02X",
                     info->src_addr[3], info->src_addr[4], info->src_addr[5]);
            snprintf(n.type_tag, sizeof(n.type_tag), "ESP-NOW");
            snprintf(n.signal, sizeof(n.signal), "%d dBm", info->rx_ctrl ? info->rx_ctrl->rssi : 0);
            snprintf(n.detail, sizeof(n.detail), "CardKB2");
            memcpy(n.mac, info->src_addr, 6);
            if (_wireless_add_node(&n) == ESP_OK && s_scan_cb) {
                s_scan_cb();
            }
        }
        /* Forward to app if connected or scanning */
        if (g_wireless_recv_cb && (s_connected || s_scanning)) {
            ESP_LOGI(TAG, "ESP-NOW key frame: key_id=%u state=%u checksum=0x%02X from " MACSTR,
                     data[2], data[3], data[4], MAC2STR(info->src_addr));
            g_wireless_recv_cb(false, info->src_addr, data, 5);
        }
        return;
    }

    /* Optional fallback: raw single-byte key payload */
    if (len == 1 && data[0] != 0) {
        if (s_scanning) {
            wireless_node_t n = {0};
            n.is_bt = false;
            n.valid = true;
            snprintf(n.name, sizeof(n.name), "ESP-NOW-%02X%02X%02X",
                     info->src_addr[3], info->src_addr[4], info->src_addr[5]);
            snprintf(n.type_tag, sizeof(n.type_tag), "ESP-NOW");
            snprintf(n.signal, sizeof(n.signal), "%d dBm", info->rx_ctrl ? info->rx_ctrl->rssi : 0);
            snprintf(n.detail, sizeof(n.detail), "RawKey");
            memcpy(n.mac, info->src_addr, 6);
            if (_wireless_add_node(&n) == ESP_OK && s_scan_cb) {
                s_scan_cb();
            }
        }
        if (g_wireless_recv_cb && (s_connected || s_scanning)) {
            ESP_LOGI(TAG, "ESP-NOW raw key: 0x%02X '%c' from " MACSTR,
                     data[0], (data[0] >= 0x20 && data[0] < 0x7F) ? data[0] : '?',
                     MAC2STR(info->src_addr));
            g_wireless_recv_cb(false, info->src_addr, data, 1);
        }
        return;
    }

    /* Short packets from connected peer (non-protocol data) */
    if (len < (int)sizeof(espnow_pkt_t)) {
        if (s_connected && memcmp(info->src_addr, s_peer_mac, 6) == 0 && g_wireless_recv_cb) {
            ESP_LOGI(TAG, "ESP-NOW short data from peer: len=%d", len);
            g_wireless_recv_cb(false, info->src_addr, data, (size_t)len);
        }
        return;
    }

    /* Check if this is a protocol packet */
    const espnow_pkt_t *pkt = (const espnow_pkt_t *)data;
    if (pkt->magic != ESPNOW_MAGIC) {
        /* Non-protocol data from connected peer */
        if (s_connected && memcmp(info->src_addr, s_peer_mac, 6) == 0 && g_wireless_recv_cb) {
            ESP_LOGI(TAG, "ESP-NOW non-protocol data from peer: len=%d", len);
            g_wireless_recv_cb(false, info->src_addr, data, (size_t)len);
        }
        return;
    }

    switch (pkt->type) {
    case ESPNOW_TYPE_IAM:
        if (s_scanning) {
            wireless_node_t n = {0};
            n.is_bt = false;
            n.valid  = true;
            if (pkt->device_name[0] != '\0') {
                snprintf(n.name, sizeof(n.name), "%.36s", pkt->device_name);
            } else {
                snprintf(n.name, sizeof(n.name), "ESP-NOW-%02X%02X%02X",
                         info->src_addr[3], info->src_addr[4], info->src_addr[5]);
            }
            snprintf(n.type_tag, sizeof(n.type_tag), "ESP-NOW");
            snprintf(n.signal, sizeof(n.signal), "%d dBm", info->rx_ctrl ? info->rx_ctrl->rssi : 0);
            snprintf(n.detail, sizeof(n.detail), "Ch.%u", pkt->channel);
            memcpy(n.mac, info->src_addr, 6);
            if (_wireless_add_node(&n) == ESP_OK && s_scan_cb) {
                s_scan_cb();
            }
        }
        break;

    case ESPNOW_TYPE_WHO:
        send_beacon(ESPNOW_TYPE_IAM, info->src_addr);
        if (s_scanning) {
            wireless_node_t n = {0};
            n.is_bt = false;
            n.valid  = true;
            if (pkt->device_name[0] != '\0') {
                snprintf(n.name, sizeof(n.name), "%.36s", pkt->device_name);
            } else {
                snprintf(n.name, sizeof(n.name), "ESP-NOW-%02X%02X%02X",
                         info->src_addr[3], info->src_addr[4], info->src_addr[5]);
            }
            snprintf(n.type_tag, sizeof(n.type_tag), "ESP-NOW");
            snprintf(n.signal, sizeof(n.signal), "%d dBm", info->rx_ctrl ? info->rx_ctrl->rssi : 0);
            snprintf(n.detail, sizeof(n.detail), "Ch.%u", pkt->channel);
            memcpy(n.mac, info->src_addr, 6);
            if (_wireless_add_node(&n) == ESP_OK && s_scan_cb) {
                s_scan_cb();
            }
        }
        break;

    case ESPNOW_TYPE_DATA:
        if (s_connected && memcmp(info->src_addr, s_peer_mac, 6) == 0 && g_wireless_recv_cb) {
            if (len > (int)sizeof(espnow_pkt_t)) {
                ESP_LOGI(TAG, "ESP-NOW protocol data from peer: payload_len=%d",
                         len - (int)sizeof(espnow_pkt_t));
                g_wireless_recv_cb(false, info->src_addr,
                                   data + sizeof(espnow_pkt_t),
                                   (size_t)(len - sizeof(espnow_pkt_t)));
            }
        }
        break;

    default:
        break;
    }
}

static void who_timer_cb(TimerHandle_t tmr)
{
    (void)tmr;
    if (!s_scanning) return;

    /* Cycle through channels to listen for KB broadcasts */
    set_scan_channel(s_scan_ch);

    s_scan_ch++;
    if (s_scan_ch > 13) s_scan_ch = 1;

    s_scan_elapsed_ms += ESPNOW_SCAN_STEP_MS;
    if (s_scan_elapsed_ms >= ESPNOW_SCAN_WINDOW_MS) {
        ESP_LOGI(TAG, "ESP-NOW scan window reached (%u ms), stopping", (unsigned)s_scan_elapsed_ms);
        espnow_scan_stop();
    }
}

esp_err_t espnow_scan_start(wireless_result_cb_t cb)
{
    if (s_scanning) {
        espnow_scan_stop();
    }

    s_scan_cb  = cb;
    s_scanning = true;
    s_scan_ch  = 1;
    s_scan_elapsed_ms = 0;

    esp_now_register_recv_cb(espnow_recv_cb);

    /* Add known CardKB2 device to scan results immediately */
    wireless_node_t kb_node = {0};
    kb_node.is_bt = false;
    kb_node.valid = true;
    snprintf(kb_node.name, sizeof(kb_node.name), "CardKB2-C30254");
    snprintf(kb_node.type_tag, sizeof(kb_node.type_tag), "ESP-NOW");
    snprintf(kb_node.signal, sizeof(kb_node.signal), "Known");
    snprintf(kb_node.detail, sizeof(kb_node.detail), "CardKB2");
    memcpy(kb_node.mac, k_cardkb2_mac, 6);
    
    ESP_LOGI(TAG, "Adding known CardKB2 device: MAC=" MACSTR, MAC2STR(k_cardkb2_mac));
    esp_err_t add_ret = _wireless_add_node(&kb_node);
    ESP_LOGI(TAG, "Add result: %s", esp_err_to_name(add_ret));
    
    if (add_ret == ESP_OK && cb) {
        ESP_LOGI(TAG, "Calling scan result callback");
        cb();
    }

    if (!esp_now_is_peer_exist(k_broadcast)) {
        esp_now_peer_info_t peer = {
            .channel = 0,
            .ifidx   = WIFI_IF_STA,
            .encrypt = false,
        };
        memcpy(peer.peer_addr, k_broadcast, 6);
        esp_err_t r = esp_now_add_peer(&peer);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(r));
        }
    }

    /* KB doesn't respond to WHO_IS_HERE, so we just listen passively.
     * The timer is still used to auto-stop the scan after 5 seconds. */
    set_scan_channel(s_scan_ch);
    s_scan_ch = 2;

    if (!s_who_tmr) {
        s_who_tmr = xTimerCreate("espnow_scan", pdMS_TO_TICKS(ESPNOW_SCAN_STEP_MS),
                                 pdTRUE, NULL, who_timer_cb);
    }
    if (s_who_tmr) xTimerStart(s_who_tmr, 0);

    ESP_LOGI(TAG, "ESP-NOW scan started (Ch.%u)", current_channel());
    return ESP_OK;
}

bool espnow_scan_is_active(void)
{
    return s_scanning;
}

esp_err_t espnow_scan_stop(void)
{
    if (!s_scanning) return ESP_OK;

    s_scanning = false;
    s_scan_elapsed_ms = 0;
    if (s_who_tmr) xTimerStop(s_who_tmr, 0);
    if (!s_connected) {
        esp_now_unregister_recv_cb();
    }

    if (s_scan_cb) {
        s_scan_cb();
    }

    ESP_LOGI(TAG, "ESP-NOW scan stopped");
    return ESP_OK;
}

esp_err_t espnow_connect(const uint8_t peer_mac[6])
{
    espnow_scan_stop();

    if (!esp_now_is_peer_exist(peer_mac)) {
        esp_now_peer_info_t peer = {
            .channel = 0,
            .ifidx   = WIFI_IF_STA,
            .encrypt = false,
        };
        memcpy(peer.peer_addr, peer_mac, 6);
        esp_err_t r = esp_now_add_peer(&peer);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_add_peer: %s", esp_err_to_name(r));
            return r;
        }
    }

    memcpy(s_peer_mac, peer_mac, 6);
    s_connected = true;
    esp_now_register_recv_cb(espnow_recv_cb);

    ESP_LOGI(TAG, "ESP-NOW connected to " MACSTR, MAC2STR(peer_mac));
    return ESP_OK;
}

esp_err_t espnow_send(const uint8_t *data, size_t len)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    size_t total = sizeof(espnow_pkt_t) + len;
    if (total > 250) return ESP_ERR_INVALID_SIZE;

    uint8_t buf[250];
    espnow_pkt_t *hdr = (espnow_pkt_t *)buf;
    hdr->magic   = ESPNOW_MAGIC;
    hdr->type    = ESPNOW_TYPE_DATA;
    hdr->channel = current_channel();
    strncpy(hdr->device_name, "TAB5", sizeof(hdr->device_name) - 1);
    if (data && len > 0) memcpy(buf + sizeof(espnow_pkt_t), data, len);

    return esp_now_send(s_peer_mac, buf, total);
}

void espnow_disconnect(void)
{
    if (!s_connected) return;

    esp_now_del_peer(s_peer_mac);
    esp_now_unregister_recv_cb();
    s_connected = false;
    memset(s_peer_mac, 0, sizeof(s_peer_mac));

    ESP_LOGI(TAG, "ESP-NOW disconnected");
}

#else  /* CONFIG_IDF_TARGET_ESP32P4 */

esp_err_t espnow_scan_start(wireless_result_cb_t cb)           { (void)cb; return ESP_ERR_NOT_SUPPORTED; }
bool      espnow_scan_is_active(void)                          { return false; }
esp_err_t espnow_scan_stop(void)                               { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t espnow_connect(const uint8_t peer_mac[6])            { (void)peer_mac; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t espnow_send(const uint8_t *data, size_t len)         { (void)data; (void)len; return ESP_ERR_NOT_SUPPORTED; }
void      espnow_disconnect(void)                              { }

#endif /* !CONFIG_IDF_TARGET_ESP32P4 */
