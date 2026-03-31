#include "input_uart.h"
#include "uart_mgr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "input_uart";

static input_action_cb_t s_action_cb = NULL;
static input_status_cb_t s_status_cb = NULL;
static void *s_user = NULL;
static bool s_connected = false;

static bool key_id_to_action(uint8_t key_id, input_action_t *out_action)
{
    if (out_action == NULL) {
        return false;
    }

    switch (key_id) {
        case 25: /* D */
            *out_action = INPUT_ACTION_ROTATE;
            return true;
        case 35: /* Z */
            *out_action = INPUT_ACTION_LEFT;
            return true;
        case 36: /* X */
            *out_action = INPUT_ACTION_DOWN;
            return true;
        case 37: /* C */
            *out_action = INPUT_ACTION_RIGHT;
            return true;
        case 32: /* Enter */
            *out_action = INPUT_ACTION_ACCEL;
            return true;
        case 42: /* Space */
            *out_action = INPUT_ACTION_PAUSE;
            return true;
        case 21: /* Del */
            *out_action = INPUT_ACTION_EXIT;
            return true;
        case 39: /* B */
            *out_action = INPUT_ACTION_B;
            return true;
        default:
            return false;
    }
}

static void handle_frame(const uint8_t *data, int len)
{
    if (len < 5 || data == NULL) {
        return;
    }
    for (int i = 0; i <= len - 5; i++) {
        if (data[i] != 0xAA || data[i + 1] != 0x03) {
            continue;
        }
        uint8_t checksum = (uint8_t)((data[i + 1] + data[i + 2] + data[i + 3]) & 0xFF);
        if (checksum != data[i + 4]) {
            continue;
        }

        if (!s_connected) {
            s_connected = true;
            if (s_status_cb) {
                s_status_cb(true, s_user);
            }
        }

        input_action_t action;
        if (!key_id_to_action(data[i + 2], &action)) {
            continue;
        }

        if (s_action_cb) {
            input_action_event_t evt = {
                .action = action,
                .pressed = (data[i + 3] == 0x01),
            };
            s_action_cb(&evt, s_user);
        }
    }
}

static void uart_recv_cb(const uint8_t *data, size_t len)
{
    handle_frame(data, (int)len);
}

esp_err_t input_uart_start(input_action_cb_t action_cb, input_status_cb_t status_cb, void *user)
{
    s_action_cb = action_cb;
    s_status_cb = status_cb;
    s_user = user;
    s_connected = false;

    esp_err_t ret = uart_mgr_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_mgr_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uart_mgr_register_recv_cb(uart_recv_cb);
    ESP_LOGI(TAG, "UART input ready");
    return ESP_OK;
}

void input_uart_stop(void)
{
    uart_mgr_register_recv_cb(NULL);
    uart_mgr_deinit();

    s_connected = false;
    s_action_cb = NULL;
    s_status_cb = NULL;
    s_user = NULL;
}
