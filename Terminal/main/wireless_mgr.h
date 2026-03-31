/*
 * wireless_mgr.h — Shared interface for ESP-NOW and BLE subsystems
 *
 * Architecture (M5Stack Tab5):
 *   ESP32-P4 (main CPU)  ─── UART/SPI ───  ESP32-C6 (wireless coprocessor)
 *       esp_wifi_remote                       esp_hosted_slave firmware
 *       NimBLE host (BLE)                     BLE controller (HCI)
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ─────────────────────────────────────────────────────── */
#define WIRELESS_MAX_NODES   32   /* max devices in scan result list  */

/* ── Discovered / connected node descriptor ─────────────────────── */
typedef struct {
    char    name[40];         /* device name or MAC-based fallback      */
    char    type_tag[12];     /* "ESP-NOW"  "BLE HID"  "BLE"           */
    bool    is_bt;            /* true → BLE,  false → ESP-NOW          */
    char    signal[12];       /* RSSI string: "-58 dBm"                 */
    char    detail[24];       /* "Ch.6" (ESP-NOW) │ "HID" / UUID (BLE) */
    uint8_t mac[6];           /* raw MAC / BLE BD_ADDR (little-endian)  */
    uint8_t ble_addr_type;    /* BLE only: 0=public 1=random            */
    bool    valid;            /* slot occupied                           */
} wireless_node_t;

/* ── Callback types ──────────────────────────────────────────────── */
/**
 * @brief Called (from any task) each time a new device is discovered.
 *        Do NOT call LVGL APIs directly here — use lv_async_call().
 */
typedef void (*wireless_result_cb_t)(void);

/**
 * @brief Called when data arrives from the connected peer.
 * @param is_bt   true = BLE peer,  false = ESP-NOW peer
 * @param mac     MAC address of sender
 * @param data    raw payload bytes
 * @param len     payload length
 */
typedef void (*wireless_recv_cb_t)(bool is_bt,
                                   const uint8_t mac[6],
                                   const uint8_t *data,
                                   size_t len);

/* ────────────────────────────────────────────────────────────────── */
/*  Shared result store                                               */
/* ────────────────────────────────────────────────────────────────── */

/**
 * @brief One-time init: NVS flash + WiFi + ESP-NOW + BLE stack.
 *        Call from app_main() BEFORE any scan.
 */
esp_err_t wireless_init(void);

/**
 * @brief Copy current scan results into caller-supplied array.
 * @param out        Destination buffer (WIRELESS_MAX_NODES entries recommended)
 * @param max_count  Size of destination buffer
 * @return Number of valid entries copied (0 … max_count)
 */
int  wireless_get_results(wireless_node_t *out, int max_count);

/** Return the current count of valid discovered nodes (lock-free read). */
int  wireless_node_count(void);

/** Erase all scan results (e.g., when starting a new scan session). */
void wireless_clear_results(void);

/**
 * @brief Register callback invoked when the connected peer sends data.
 *        Pass NULL to deregister.
 */
void wireless_register_recv_cb(wireless_recv_cb_t cb);

/**
 * @brief Called when the BLE connection state changes.
 * @param is_disconnected  true = disconnected/connecting,  false = HID ready
 *        Do NOT call LVGL APIs directly — use lv_async_call().
 */
typedef void (*wireless_status_cb_t)(bool is_disconnected);
void wireless_register_status_cb(wireless_status_cb_t cb);

/* ────────────────────────────────────────────────────────────────── */
/*  ESP-NOW                                                           */
/* ────────────────────────────────────────────────────────────────── */

/**
 * @brief Start ESP-NOW peer discovery.
 *        Broadcasts WHO_IS_HERE beacons periodically and collects
 *        I_AM_HERE responses from nearby ESP-NOW peers.
 *        Also responds to incoming WHO_IS_HERE requests.
 * @param cb  Notified for each newly discovered peer (may be NULL)
 */
esp_err_t espnow_scan_start(wireless_result_cb_t cb);

/** Stop broadcasting / receiving discovery beacons. */
esp_err_t espnow_scan_stop(void);

/** Returns true while an ESP-NOW scan is in progress. */
bool espnow_scan_is_active(void);

/**
 * @brief Add peer and enable bidirectional data exchange.
 *        After this call, incoming packets trigger the recv_cb.
 */
esp_err_t espnow_connect(const uint8_t peer_mac[6]);

/**
 * @brief Send raw bytes to the currently connected ESP-NOW peer.
 * @return ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t espnow_send(const uint8_t *data, size_t len);

/** Remove peer and stop data exchange. */
void espnow_disconnect(void);

/* ────────────────────────────────────────────────────────────────── */
/*  BLE (NimBLE)                                                      */
/* ────────────────────────────────────────────────────────────────── */

/**
 * @brief Start BLE GAP active scan.
 *        Collects advertising reports and populates the shared result list.
 * @param cb  Notified each time a new or updated device is found (may be NULL)
 */
esp_err_t ble_scan_start(wireless_result_cb_t cb);

/** Cancel the ongoing BLE scan. */
esp_err_t ble_scan_stop(void);

/** Returns true while a BLE scan is in progress. */
bool ble_scan_is_active(void);

/**
 * @brief Initiate a BLE connection.
 * @param addr       6-byte BD_ADDR (little-endian NimBLE convention)
 * @param addr_type  BLE_ADDR_PUBLIC(0) or BLE_ADDR_RANDOM(1)
 */
esp_err_t ble_connect(const uint8_t addr[6], uint8_t addr_type);

/** Terminate the current BLE connection. */
void ble_disconnect(void);

#ifdef __cplusplus
}
#endif
