/*
 * ui_page5.c — Keyboard Input screen (shown after connecting to CardKB2)
 * Page 5
 *
 * Receives decoded ASCII characters from the BLE HID keyboard (CardKB2)
 * via wireless_mgr receive callback and displays them as typed text with
 * a blinking cursor. Supports backspace and enter.
 *
 * Layout:
 *   ┌──────────────── Toolbar 40px ──────────────────────────────────┐
 *   │  ● CONNECTED   CardKB2-XXXX                          BLE HID  │
 *   ├───────────────────────────────────────────────────────────────-┤
 *   │                                                                │
 *   │    [history line 1]                                            │
 *   │    [history line 2]                                            │
 *   │    > [current input text]█                                     │
 *   │                                                                │
 *   └────────────────────────────────────────────────────────────────┘
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_common.h"
#include "wireless_mgr.h"
#include "uart_mgr.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────── */
#define MAX_INPUT_LEN   200
#define MAX_HISTORY     20

/* KB ESP-NOW key-event packet: [0xAA][0x03][key_id][state][checksum] */
static const uint8_t s_keymap_normal[4][11] = {
    {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x00},
    {0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6F, 0x70, 0x08},
    {0x41, 0x61, 0x73, 0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6C, 0x0A},
    {0x46, 0x53, 0x7A, 0x78, 0x63, 0x76, 0x62, 0x6E, 0x6D, 0x20, 0x00}
};

static bool espnow_key_to_ascii(uint8_t key_id, uint8_t state, uint8_t *out_ch)
{
    if (!out_ch || state != 0x01) {
        return false;
    }

    if (key_id >= 44) {
        return false;
    }

    int row = key_id / 11;
    int col = key_id % 11;
    if (row < 0 || row >= 4 || col < 0 || col >= 11) {
        return false;
    }

    uint8_t ch = s_keymap_normal[row][col];
    if (ch == 0) {
        return false;
    }

    *out_ch = ch;
    return true;
}

/* ── State ───────────────────────────────────────────────────────── */
static lv_obj_t *s_text_area   = NULL;   /* scrollable container     */
static lv_obj_t *s_input_lbl   = NULL;   /* current input line label */
static lv_obj_t *s_cursor      = NULL;   /* blinking cursor block    */
static lv_obj_t *s_input_row   = NULL;   /* flex row: prompt+lbl+cursor */
/* Status bar widgets (updated on BLE connect/disconnect) */
static lv_obj_t *s_status_dot  = NULL;
static lv_obj_t *s_status_lbl  = NULL;
static char      s_input_buf[MAX_INPUT_LEN + 1] = {0};
static int       s_input_len   = 0;
static int       s_history_cnt = 0;
static bool      s_is_bt       = false;

/* ── Cursor blink animation ─────────────────────────────────────── */
static void cursor_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

/* ── Append a history line (completed inputs) ────────────────────── */
static void append_history_line(const char *text)
{
    if (!s_text_area || !lv_obj_is_valid(s_text_area)) return;
    if (s_history_cnt >= MAX_HISTORY) return;

    lv_obj_t *lbl = lv_label_create(s_text_area);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xA78BFA), 0);  // Purple color
    lv_obj_set_style_text_font(lbl, &lv_font_inter_regular_24, 0);
    lv_obj_set_style_text_letter_space(lbl, -1, 0);
    lv_obj_set_width(lbl, SCREEN_W - 64);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_pad_top(lbl, 0, 0);      // Remove top padding
    lv_obj_set_style_pad_bottom(lbl, 0, 0);   // Remove bottom padding
    /* Move label before the input row */
    lv_obj_move_to_index(lbl, s_history_cnt);
    s_history_cnt++;
}

/* ── Update the live input label ─────────────────────────────────── */
static void update_input_label(void)
{
    if (!s_input_lbl || !lv_obj_is_valid(s_input_lbl)) return;
    lv_label_set_text(s_input_lbl, s_input_buf);
    /* Scroll text area to bottom so cursor is always visible */
    if (s_text_area && lv_obj_is_valid(s_text_area)) {
        lv_obj_scroll_to_y(s_text_area, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

/* ── Command processor ───────────────────────────────────────────── */
static void process_command(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') return;

    if (strcmp(cmd, "help") == 0) {
        append_history_line("Available commands:");
        append_history_line("  help       - list commands");
        append_history_line("  status     - system status");
        append_history_line("  about      - terminal info");
        append_history_line("  clear      - clear screen");
        append_history_line("  scan       - scan interfaces");
        append_history_line("  monitor    - live input monitor");
        append_history_line("  matrix     - enable matrix mode");
        append_history_line("  hack       - run intrusion sequence");
        append_history_line("  joke       - tell an engineer joke");
        append_history_line("  who        - show device info");
        append_history_line("  secret     - ???");
    }
    else if (strcmp(cmd, "status") == 0) {
        append_history_line("System Status");
        append_history_line("-------------");
        append_history_line("Device       : ESP-P4 Terminal");
        append_history_line("Display      : online");
        append_history_line("Keyboard     : connected");
        append_history_line(s_is_bt ? "BLE          : active" : "UART         : active");
        append_history_line("Input Latency: 8 ms");
        append_history_line("Frame Rate   : 60 FPS");
        append_history_line("Memory       : stable");
        append_history_line("Operator     : guest");
    }
    else if (strcmp(cmd, "who") == 0) {
        append_history_line("M5 STACK Tab5");
        append_history_line("    /\\_/\\  ");
        append_history_line("   ( o.o ) ");
        append_history_line("    > ^ <  ");
        append_history_line("   /|   |\\");
    }
    else if (strcmp(cmd, "about") == 0) {
        append_history_line("TAB5 Smart Terminal v1.0");
        append_history_line("ESP32-P4 @ 360MHz");
        append_history_line("Built: " __DATE__);
    }
    else if (strcmp(cmd, "clear") == 0) {
        /* Clear history */
        if (s_text_area && lv_obj_is_valid(s_text_area)) {
            lv_obj_clean(s_text_area);
            s_history_cnt = 0;
            /* Re-add input row */
            if (s_input_row && lv_obj_is_valid(s_input_row)) {
                lv_obj_move_to_index(s_input_row, 0);
            }
        }
    }
    else if (strcmp(cmd, "joke") == 0) {
        append_history_line("Why do programmers prefer dark mode?");
        append_history_line("Because light attracts bugs!");
    }
    else if (strcmp(cmd, "secret") == 0) {
        append_history_line("The cake is a lie.");
    }
    else if (strcmp(cmd, "matrix") == 0) {
        append_history_line("Wake up, Neo...");
        append_history_line("The Matrix has you.");
    }
    else if (strcmp(cmd, "hack") == 0) {
        append_history_line("Initiating intrusion sequence...");
        append_history_line("[OK] Bypassing firewall");
        append_history_line("[OK] Cracking encryption");
        append_history_line("[FAIL] Access denied. Nice try!");
    }
    else if (strcmp(cmd, "scan") == 0) {
        append_history_line("Scanning interfaces...");
        append_history_line(s_is_bt ? "  [*] BLE HID - connected" : "  [*] UART - connected");
        append_history_line("  [ ] WiFi - not configured");
        append_history_line("  [ ] Ethernet - not available");
    }
    else if (strcmp(cmd, "monitor") == 0) {
        append_history_line("Live input monitor enabled.");
        append_history_line("Press any key to test...");
    }
    else {
        char err[MAX_INPUT_LEN + 32];
        snprintf(err, sizeof(err), "Unknown command: %s", cmd);
        append_history_line(err);
        append_history_line("Type 'help' for available commands.");
    }
}

/* ── Async callback — runs in LVGL task ─────────────────────────── */
typedef struct {
    uint8_t ch;
} key_async_arg_t;

static void key_async_cb(void *arg)
{
    key_async_arg_t *a = (key_async_arg_t *)arg;
    if (!a) return;
    uint8_t ch = a->ch;
    free(a);

    if (ch == '\b' || ch == 0x7F) {
        /* Backspace — remove last character */
        if (s_input_len > 0) {
            s_input_len--;
            s_input_buf[s_input_len] = '\0';
            update_input_label();
        }
    } else if (ch == '\r' || ch == '\n') {
        /* Enter — process command and clear input */
        if (s_input_len > 0) {
            char history_line[MAX_INPUT_LEN + 8];
            snprintf(history_line, sizeof(history_line), "> %s", s_input_buf);
            append_history_line(history_line);
            
            /* Process the command */
            process_command(s_input_buf);
            
            s_input_len = 0;
            s_input_buf[0] = '\0';
            update_input_label();
        }
    } else if (ch >= 0x20 && ch <= 0x7E) {
        /* Printable ASCII */
        if (s_input_len < MAX_INPUT_LEN) {
            s_input_buf[s_input_len++] = (char)ch;
            s_input_buf[s_input_len]   = '\0';
            update_input_label();
        }
    }
}

/* ── Receive callback (called from BLE/WiFi task) ────────────────── */
static void p5_recv_cb(bool is_bt, const uint8_t mac[6],
                       const uint8_t *data, size_t len)
{
    (void)mac;
    if (!data || len == 0) return;

    if (!is_bt && len == 5 && data[0] == 0xAA && data[1] == 0x03) {
        uint8_t key_id = data[2];
        uint8_t state = data[3];
        uint8_t ch = 0;
        bool have_char = espnow_key_to_ascii(key_id, state, &ch);

        if (state == 0x01) {
            if (have_char) {
                if (ch >= 0x20 && ch <= 0x7E) {
                    ESP_LOGI("ui/page5", "ESP-NOW key[%u] PRESS -> 0x%02X '%c'", key_id, ch, (char)ch);
                } else {
                    ESP_LOGI("ui/page5", "ESP-NOW key[%u] PRESS -> 0x%02X", key_id, ch);
                }
                key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
                if (a) {
                    a->ch = ch;
                    lv_async_call(key_async_cb, a);
                }
            } else {
                ESP_LOGI("ui/page5", "ESP-NOW key[%u] PRESS", key_id);
            }
        } else if (state == 0x02) {
            ESP_LOGI("ui/page5", "ESP-NOW key[%u] RELEASE", key_id);
        } else {
            ESP_LOGI("ui/page5", "ESP-NOW key[%u] state=0x%02X", key_id, state);
        }
        return;
    }

    if (len == 1) {
        uint8_t ch = data[0];
        if (ch >= 0x20 && ch <= 0x7E) {
            ESP_LOGI("ui/page5", "Received key from %s: 0x%02X '%c'", is_bt ? "BLE" : "ESP-NOW", ch, (char)ch);
        } else {
            ESP_LOGI("ui/page5", "Received key from %s: 0x%02X", is_bt ? "BLE" : "ESP-NOW", ch);
        }
    } else {
        ESP_LOGI("ui/page5", "Received payload from %s: len=%u", is_bt ? "BLE" : "ESP-NOW", (unsigned)len);
    }

    /* We expect single decoded ASCII bytes from ble_mgr's HID decoder */
    key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
    if (!a) return;
    a->ch = data[0];
    lv_async_call(key_async_cb, a);
}

/* ── UART receive callback (called from UART task) ──────────────── */
static void uart_recv_cb(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return;

    /* KB UART key frame: [0xAA][0x03][key_id][state][checksum]
     * Multiple frames may arrive in one read — iterate through all of them. */
    size_t i = 0;
    while (i + 5 <= len) {
        if (data[i] == 0xAA && data[i + 1] == 0x03) {
            uint8_t key_id = data[i + 2];
            uint8_t state  = data[i + 3];
            uint8_t ch = 0;
            bool have_char = espnow_key_to_ascii(key_id, state, &ch);

            if (state == 0x01 && have_char) {
                ESP_LOGI("ui/page5", "UART key[%u] PRESS -> 0x%02X '%c'",
                         key_id, ch, (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '?');
                key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
                if (a) {
                    a->ch = ch;
                    lv_async_call(key_async_cb, a);
                }
            }
            i += 5;
        } else {
            i++;  /* re-sync if header not found */
        }
    }

    /* Single byte fallback */
    if (len == 1 && data[0] >= 0x08 && data[0] <= 0x7E) {
        ESP_LOGI("ui/page5", "UART single byte: 0x%02X", data[0]);
        key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
        if (a) {
            a->ch = data[0];
            lv_async_call(key_async_cb, a);
        }
    }
}

/* ── BLE status callback (called from BLE task on connect/disconnect) ─ */
typedef struct { bool is_disconnected; } status_async_arg_t;

static void p5_status_async_cb(void *arg)
{
    status_async_arg_t *a = (status_async_arg_t *)arg;
    if (!a) return;
    bool discon = a->is_disconnected;
    free(a);

    if (s_status_dot && lv_obj_is_valid(s_status_dot)) {
        lv_obj_set_style_bg_color(s_status_dot,
            discon ? lv_color_hex(0xE53935) : C_CONNECTED, 0);
    }
    if (s_status_lbl && lv_obj_is_valid(s_status_lbl)) {
        lv_label_set_text(s_status_lbl,
            discon ? "DISCONNECTED" : "CONNECTED");
        lv_obj_set_style_text_color(s_status_lbl,
            discon ? lv_color_hex(0xFF5252) : C_ORANGE, 0);
    }
}

static void p5_status_cb(bool is_disconnected)
{
    status_async_arg_t *a = malloc(sizeof(status_async_arg_t));
    if (!a) return;
    a->is_disconnected = is_disconnected;
    lv_async_call(p5_status_async_cb, a);
}

/* ── Page 5 builder ─────────────────────────────────────────────── */
void build_page5(bool is_bt)
{
    s_input_len   = 0;
    s_input_buf[0]= '\0';
    s_history_cnt = 0;
    s_is_bt       = is_bt;
    s_input_lbl   = NULL;
    s_cursor      = NULL;
    s_input_row   = NULL;
    s_text_area   = NULL;
    s_status_dot  = NULL;
    s_status_lbl  = NULL;

    if (is_bt) {
        /* BLE mode: use wireless callbacks */
        wireless_register_recv_cb(p5_recv_cb);
        wireless_register_status_cb(p5_status_cb);
    } else {
        /* UART mode: initialize UART and register callback */
        ESP_LOGI("ui/page5", "Initializing UART mode");
        esp_err_t ret = uart_mgr_init();
        if (ret == ESP_OK) {
            uart_mgr_register_recv_cb(uart_recv_cb);
            ESP_LOGI("ui/page5", "UART initialized successfully");
            
            /* Send test message to KB */
            const char *test_msg = "TAB5 Ready\r\n";
            uart_mgr_send((const uint8_t *)test_msg, strlen(test_msg));
            ESP_LOGI("ui/page5", "Sent test message to KB");
        } else {
            ESP_LOGE("ui/page5", "UART init failed: %s", esp_err_to_name(ret));
        }
    }

    lv_obj_t *scr5 = lv_obj_create(NULL);
    lv_obj_clear_flag(scr5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr5, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr5, C_CARD_BG, 0);
    lv_obj_set_style_bg_opa(scr5, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr5, 0, 0);
    lv_obj_set_style_border_width(scr5, 0, 0);

    /* ── Toolbar ─────────────────────────────────────────────────── */
    lv_obj_t *toolbar = lv_obj_create(scr5);
    lv_obj_set_size(toolbar, SCREEN_W, 40);
    lv_obj_set_pos(toolbar, 0, 0);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(0x201F1F), 0);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toolbar, 1, 0);
    lv_obj_set_style_border_side(toolbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(toolbar, lv_color_hex(0x574235), 0);
    lv_obj_set_style_border_opa(toolbar, LV_OPA_10, 0);
    lv_obj_set_style_pad_hor(toolbar, 16, 0);
    lv_obj_set_style_pad_ver(toolbar, 0, 0);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(toolbar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Left: green dot + "CONNECTED" */
    lv_obj_t *left_row = lv_obj_create(toolbar);
    lv_obj_remove_style_all(left_row);
    lv_obj_set_size(left_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(left_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_row, 8, 0);
    lv_obj_set_style_pad_all(left_row, 0, 0);

    lv_obj_t *dot = lv_obj_create(left_row);
    s_status_dot = dot;
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, C_CONNECTED, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);

    lv_obj_t *lbl_conn = lv_label_create(left_row);
    s_status_lbl = lbl_conn;
    lv_label_set_text(lbl_conn, "CONNECTED");
    lv_obj_set_style_text_color(lbl_conn, C_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_conn, &lv_font_inter_regular_12, 0);
    lv_obj_set_style_text_letter_space(lbl_conn, 2, 0);

    /* Center: device name from scan results */
    {
        wireless_node_t nodes[WIRELESS_MAX_NODES];
        int cnt = wireless_get_results(nodes, WIRELESS_MAX_NODES);
        char dev_name[40] = "CardKB2";
        for (int i = 0; i < cnt; i++) {
            if (nodes[i].is_bt == is_bt && nodes[i].valid) {
                snprintf(dev_name, sizeof(dev_name), "%s", nodes[i].name);
                break;
            }
        }
        lv_obj_t *lbl_dev = lv_label_create(toolbar);
        lv_label_set_text(lbl_dev, dev_name);
        lv_obj_set_style_text_color(lbl_dev, lv_color_hex(0xA09898), 0);
        lv_obj_set_style_text_font(lbl_dev, &lv_font_inter_regular_12, 0);
        lv_obj_set_style_text_letter_space(lbl_dev, 1, 0);
    }

    /* Right: protocol label */
    lv_obj_t *lbl_proto = lv_label_create(toolbar);
    lv_label_set_text(lbl_proto, is_bt ? "BLE HID" : "UART");
    lv_obj_set_style_text_color(lbl_proto, is_bt ? C_BLUE : C_WARM, 0);
    lv_obj_set_style_text_font(lbl_proto, &lv_font_inter_bold_13, 0);
    lv_obj_set_style_text_letter_space(lbl_proto, 1, 0);

    /* ── Main text area (scrollable, below toolbar) ──────────────── */
    lv_obj_t *main_area = lv_obj_create(scr5);
    s_text_area = main_area;
    lv_obj_set_size(main_area, SCREEN_W, SCREEN_H - 40);
    lv_obj_set_pos(main_area, 0, 40);
    lv_obj_set_style_bg_color(main_area, C_CARD_BG, 0);
    lv_obj_set_style_bg_opa(main_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(main_area, 0, 0);
    lv_obj_set_style_pad_all(main_area, 24, 0);
    lv_obj_set_style_pad_top(main_area, 20, 0);
    lv_obj_set_layout(main_area, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(main_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_area, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(main_area, 2, 0);  // Reduce row spacing
    lv_obj_add_flag(main_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(main_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(main_area, LV_DIR_VER);

    /* ── Input prompt row: ">" + editable label + blinking cursor ── */
    lv_obj_t *input_row = lv_obj_create(main_area);
    s_input_row = input_row;
    lv_obj_remove_style_all(input_row);
    lv_obj_set_size(input_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(input_row, SCREEN_W - 48, 0);
    lv_obj_set_layout(input_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(input_row, 0, 0);
    lv_obj_set_style_pad_column(input_row, 4, 0);
    lv_obj_add_flag(input_row, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *lbl_prompt = lv_label_create(input_row);
    lv_label_set_text(lbl_prompt, ">");
    lv_obj_set_style_text_color(lbl_prompt, C_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_prompt, &lv_font_inter_bold_24, 0);
    lv_obj_add_flag(lbl_prompt, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *input_lbl = lv_label_create(input_row);
    s_input_lbl = input_lbl;
    lv_label_set_text(input_lbl, "");
    lv_obj_set_style_text_color(input_lbl, lv_color_hex(0xE5E2E1), 0);
    lv_obj_set_style_text_font(input_lbl, &lv_font_inter_regular_24, 0);
    lv_obj_set_style_text_letter_space(input_lbl, -1, 0);
    lv_obj_set_style_max_width(input_lbl, SCREEN_W - 100, 0);
    lv_label_set_long_mode(input_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(input_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *cursor = lv_obj_create(input_row);
    s_cursor = cursor;
    lv_obj_set_size(cursor, 10, 24);
    lv_obj_set_style_bg_color(cursor, C_ORANGE, 0);
    lv_obj_set_style_bg_opa(cursor, LV_OPA_50, 0);
    lv_obj_set_style_border_width(cursor, 0, 0);
    lv_obj_set_style_radius(cursor, 0, 0);
    lv_obj_set_style_pad_all(cursor, 0, 0);

    /* Cursor blink: 600ms on/off */
    lv_anim_t ca;
    lv_anim_init(&ca);
    lv_anim_set_exec_cb(&ca, cursor_opa_cb);
    lv_anim_set_var(&ca, cursor);
    lv_anim_set_values(&ca, LV_OPA_TRANSP, LV_OPA_50);
    lv_anim_set_duration(&ca, 600);
    lv_anim_set_reverse_duration(&ca, 600);
    lv_anim_set_repeat_count(&ca, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&ca);

    lv_screen_load_anim(scr5, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 500, 0, true);
}
