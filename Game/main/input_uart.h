#pragma once

#include "esp_err.h"
#include "input_events.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t input_uart_start(input_action_cb_t action_cb, input_status_cb_t status_cb, void *user);
void input_uart_stop(void);

#ifdef __cplusplus
}
#endif
