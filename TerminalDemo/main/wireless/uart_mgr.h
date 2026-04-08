#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_KB_NUM         UART_NUM_1
#define UART_KB_TX_PIN      53  
#define UART_KB_RX_PIN      54  
#define UART_KB_BAUD_RATE   115200
#define UART_KB_BUF_SIZE    256

typedef void (*uart_recv_cb_t)(const uint8_t *data, size_t len);

esp_err_t uart_mgr_init(void);

esp_err_t uart_mgr_deinit(void);

void uart_mgr_register_recv_cb(uart_recv_cb_t cb);

int uart_mgr_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
