/*
 * uart_mgr.h — UART communication manager for CardKB2
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

/* UART configuration for Tab5 */
#define UART_KB_NUM         UART_NUM_1
#define UART_KB_TX_PIN      53  // Tab5 TX (G53) -> KB RX (G26)
#define UART_KB_RX_PIN      54  // Tab5 RX (G54) -> KB TX (G25)
#define UART_KB_BAUD_RATE   115200
#define UART_KB_BUF_SIZE    256

/**
 * @brief Callback for receiving UART data
 * @param data  Received data buffer
 * @param len   Data length
 */
typedef void (*uart_recv_cb_t)(const uint8_t *data, size_t len);

/**
 * @brief Initialize UART for CardKB2 communication
 * @return ESP_OK on success
 */
esp_err_t uart_mgr_init(void);

/**
 * @brief Deinitialize UART
 * @return ESP_OK on success
 */
esp_err_t uart_mgr_deinit(void);

/**
 * @brief Register callback for receiving UART data
 * @param cb  Callback function (NULL to unregister)
 */
void uart_mgr_register_recv_cb(uart_recv_cb_t cb);

/**
 * @brief Send data via UART
 * @param data  Data buffer
 * @param len   Data length
 * @return Number of bytes sent, or -1 on error
 */
int uart_mgr_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
