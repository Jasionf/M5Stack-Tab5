/*
 * uart_mgr.c — UART communication manager for CardKB2
 *
 * Receives key frames from CardKB2 via UART:
 *   Frame format: [0xAA][0x03][key_id][state][checksum]
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "uart_mgr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "uart_mgr";

static bool s_initialized = false;
static uart_recv_cb_t s_recv_cb = NULL;
static TaskHandle_t s_recv_task = NULL;

/* UART receive task */
static void uart_recv_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(UART_KB_BUF_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate UART buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UART receive task started, waiting for data...");

    while (1) {
        int len = uart_read_bytes(UART_KB_NUM, data, UART_KB_BUF_SIZE, pdMS_TO_TICKS(20));
        if (len > 0) {
            ESP_LOGI(TAG, "UART RX: %d bytes", len);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);
            
            if (s_recv_cb) {
                ESP_LOGI(TAG, "Calling receive callback");
                s_recv_cb(data, len);
            } else {
                ESP_LOGW(TAG, "No receive callback registered!");
            }
        }
        
        /* Periodic heartbeat to show task is alive (~3s interval) */
        static int heartbeat = 0;
        if (++heartbeat % 150 == 0) {
            ESP_LOGI(TAG, "UART task alive, waiting for data... (callback: %s)", 
                     s_recv_cb ? "registered" : "NULL");
        }
    }

    free(data);
    vTaskDelete(NULL);
}

esp_err_t uart_mgr_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "UART already initialized");
        return ESP_OK;
    }

    uart_config_t uart_config = {
        .baud_rate = UART_KB_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(UART_KB_NUM, UART_KB_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(UART_KB_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_KB_NUM);
        return ret;
    }

    ret = uart_set_pin(UART_KB_NUM, UART_KB_TX_PIN, UART_KB_RX_PIN, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_KB_NUM);
        return ret;
    }

    /* Create receive task */
    BaseType_t task_ret = xTaskCreate(uart_recv_task, "uart_recv", 
                                       4096, NULL, 5, &s_recv_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART receive task");
        uart_driver_delete(UART_KB_NUM);
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "UART initialized (TX:%d, RX:%d, Baud:%d)", 
             UART_KB_TX_PIN, UART_KB_RX_PIN, UART_KB_BAUD_RATE);

    return ESP_OK;
}

esp_err_t uart_mgr_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    /* Stop receive task */
    if (s_recv_task) {
        vTaskDelete(s_recv_task);
        s_recv_task = NULL;
    }

    /* Delete UART driver */
    esp_err_t ret = uart_driver_delete(UART_KB_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_delete failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    s_recv_cb = NULL;
    ESP_LOGI(TAG, "UART deinitialized");

    return ESP_OK;
}

void uart_mgr_register_recv_cb(uart_recv_cb_t cb)
{
    s_recv_cb = cb;
    ESP_LOGI(TAG, "UART receive callback %s", cb ? "registered" : "unregistered");
}

int uart_mgr_send(const uint8_t *data, size_t len)
{
    if (!s_initialized || !data || len == 0) {
        return -1;
    }

    int sent = uart_write_bytes(UART_KB_NUM, data, len);
    if (sent > 0) {
        ESP_LOGD(TAG, "UART TX: %d bytes", sent);
    }
    return sent;
}
