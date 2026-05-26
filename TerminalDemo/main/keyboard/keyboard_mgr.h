#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*keyboard_recv_cb_t)(const uint8_t *data, size_t len);
typedef void (*keyboard_status_cb_t)(bool connected);

esp_err_t keyboard_mgr_init(void);
esp_err_t keyboard_mgr_deinit(void);
void keyboard_mgr_register_recv_cb(keyboard_recv_cb_t cb);
void keyboard_mgr_register_status_cb(keyboard_status_cb_t cb);
bool keyboard_mgr_is_active(void);

#ifdef __cplusplus
}
#endif
