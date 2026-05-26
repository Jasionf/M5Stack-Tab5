#include "ui_common.h"
#include "keyboard_mgr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "bsp/esp-bsp.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "esp_wolfssh_client.h"
#include "vterm.h"
#include "wolfssh/ssh.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#define TERM_MAX_LINES       500
#define TERM_LINE_LEN        256
#define TERM_MAX_INPUT       220
#define TERM_HISTORY_MAX     20
#define TERM_VT_ROWS         24
#define TERM_VT_COLS         112
#define TERM_CONNECT_STACK   8192
#define TERM_WIFI_TIMEOUT_MS 30000

#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID "M5Stack"
#endif
#ifndef CONFIG_WIFI_PASS
#define CONFIG_WIFI_PASS "m5office888"
#endif
#ifndef CONFIG_DEFAULT_SSH_HOST
#define CONFIG_DEFAULT_SSH_HOST "192.168.51.54"
#endif
#ifndef CONFIG_DEFAULT_SSH_USER
#define CONFIG_DEFAULT_SSH_USER "m5-vincent"
#endif
#ifndef CONFIG_DEFAULT_SSH_PORT
#define CONFIG_DEFAULT_SSH_PORT 22
#endif

#define TOPBAR_H             64
#define INPUT_H              68
#define LINE_NO_W            48
#define CONTENT_LEFT_PAD     24
#define OUTPUT_SIDE_PAD      20

#define TOP_TASK_LIMIT       10

static const char *s_cmd_catalog[] = {
    "help", "clear", "neofetch", "uname -a", "uptime", "date", "pwd", "whoami",
    "ls", "ls -la", "cat /etc/motd", "ifconfig", "ping", "ping 8.8.8.8",
    "keyboard status", "gamepage", "top", "ps", "fortune", "sudo", "sudo rm -rf /", "reboot"
};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *topbar;
    lv_obj_t *output_area;
    lv_obj_t *input_row;
    lv_obj_t *input_prompt;
    lv_obj_t *input_label;
    lv_obj_t *cursor;
    lv_obj_t *kb_dot;
    lv_obj_t *kb_label;
    lv_obj_t *ssh_dot;
    lv_obj_t *ssh_label;
    lv_obj_t *vt_labels[TERM_VT_ROWS];
    lv_obj_t *line_rows[TERM_MAX_LINES];
    uint16_t  line_count;
    uint16_t  row_count;
} ui_terminal_t;

typedef struct {
    uint8_t ch;
} key_async_arg_t;

typedef struct {
    bool is_disconnected;
} status_async_arg_t;

typedef enum {
    TERM_SSH_IDLE = 0,
    TERM_SSH_WIFI,
    TERM_SSH_CONNECTING,
    TERM_SSH_CONNECTED,
    TERM_SSH_FAILED,
} term_ssh_state_t;

typedef struct {
    term_ssh_state_t state;
    int reason;
} ssh_status_async_arg_t;

typedef struct {
    int seq;
} ping_ctx_t;

static ui_terminal_t s_term;
static bool          s_keyboard_connected = false;
static char          s_input_buf[TERM_MAX_INPUT + 1];
static int           s_input_len = 0;
static char          s_cmd_history[TERM_HISTORY_MAX][TERM_MAX_INPUT + 1];
static int           s_cmd_hist_count = 0;
static int           s_cmd_hist_index = -1;
static lv_timer_t   *s_ping_timer = NULL;
static uint8_t       s_esc_state = 0;

static const char *TAG = "ui_terminal";

extern const uint8_t ssh_key_start[]    asm("_binary_id_ed25519_start");
extern const uint8_t ssh_key_end[]      asm("_binary_id_ed25519_end");
extern const uint8_t ssh_pubkey_start[] asm("_binary_id_ed25519_pub_start");
extern const uint8_t ssh_pubkey_end[]   asm("_binary_id_ed25519_pub_end");

static VTerm             *s_vt = NULL;
static VTermScreen       *s_vt_screen = NULL;
static SemaphoreHandle_t  s_vterm_lock = NULL;
static uint64_t           s_vt_dirty_rows = 0;
static lv_timer_t        *s_vterm_timer = NULL;
static bool               s_remote_mode = false;

static EventGroupHandle_t s_wifi_events = NULL;
static TaskHandle_t       s_connect_task = NULL;
static bool               s_wifi_initialized = false;
static bool               s_wifi_handlers_registered = false;
static bool               s_wolfssh_initialized = false;
static volatile bool      s_ssh_connected = false;
static volatile bool      s_ssh_connecting = false;

#define TERM_WIFI_CONNECTED_BIT BIT0

static void cursor_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void term_scroll_bottom(bool anim)
{
    if (!s_term.output_area || !lv_obj_is_valid(s_term.output_area)) {
        return;
    }
    lv_obj_scroll_to_y(s_term.output_area, LV_COORD_MAX, anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void term_prune_if_needed(void)
{
    if (s_term.row_count < TERM_MAX_LINES) {
        return;
    }

    if (s_term.line_rows[0] && lv_obj_is_valid(s_term.line_rows[0])) {
        lv_obj_del(s_term.line_rows[0]);
    }

    for (int i = 1; i < s_term.row_count; i++) {
        s_term.line_rows[i - 1] = s_term.line_rows[i];
    }
    s_term.row_count--;
}

static lv_obj_t *term_make_row(void)
{
    term_prune_if_needed();

    lv_obj_t *row = lv_obj_create(s_term.output_area);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 0, 0);

    s_term.line_rows[s_term.row_count++] = row;
    return row;
}

static void term_line_prefix(lv_obj_t *row)
{
    char ln_buf[8];
    snprintf(ln_buf, sizeof(ln_buf), "%02u", (unsigned)(s_term.line_count % 100));
    s_term.line_count++;

    lv_obj_t *ln = lv_label_create(row);
    lv_label_set_text(ln, ln_buf);
    lv_obj_set_width(ln, LINE_NO_W);
    lv_obj_set_style_text_align(ln, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(ln, T_DIM, 0);
    lv_obj_set_style_text_font(ln, &lv_font_inter_regular_20, 0);

    lv_obj_t *pad = lv_obj_create(row);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, CONTENT_LEFT_PAD, 1);
}

static void term_add_text(lv_obj_t *row, lv_color_t color, const char *txt, bool mono_small)
{
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, txt ? txt : "");
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl,
        mono_small ? &lv_font_inter_regular_20 : &lv_font_inter_regular_24, 0);
    lv_obj_set_style_text_letter_space(lbl, 0, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, SCREEN_W - OUTPUT_SIDE_PAD * 2 - LINE_NO_W - CONTENT_LEFT_PAD);
}

static void term_append_line(lv_color_t color, const char *fmt, ...)
{
    char buf[TERM_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    lv_obj_t *row = term_make_row();
    term_line_prefix(row);
    term_add_text(row, color, buf, true);
    term_scroll_bottom(false);
}

static void term_append_prompt_line(const char *cmd)
{
    lv_obj_t *row = term_make_row();
    term_line_prefix(row);

    lv_obj_t *ps = lv_label_create(row);
    lv_label_set_text(ps, "Tab5@terminal : ~ $ ");
    lv_obj_set_style_text_color(ps, T_PROMPT, 0);
    lv_obj_set_style_text_font(ps, &lv_font_inter_regular_20, 0);

    lv_obj_t *cm = lv_label_create(row);
    lv_label_set_text(cm, cmd ? cmd : "");
    lv_obj_set_style_text_color(cm, T_CMD, 0);
    lv_obj_set_style_text_font(cm, &lv_font_inter_regular_20, 0);

    term_scroll_bottom(false);
}

static void term_reset_lines(void)
{
    if (s_term.output_area && lv_obj_is_valid(s_term.output_area)) {
        lv_obj_clean(s_term.output_area);
    }
    s_term.line_count = 1;
    s_term.row_count = 0;
    memset(s_term.line_rows, 0, sizeof(s_term.line_rows));
}

static void term_update_input_label(void)
{
    if (!s_term.input_label || !lv_obj_is_valid(s_term.input_label)) {
        return;
    }
    lv_label_set_text(s_term.input_label, s_input_buf);
}

static void term_set_keyboard_status(bool connected)
{
    if (s_term.kb_dot && lv_obj_is_valid(s_term.kb_dot)) {
        lv_obj_set_style_bg_color(s_term.kb_dot, connected ? T_SUCCESS : T_ERROR, 0);
    }
    if (s_term.kb_label && lv_obj_is_valid(s_term.kb_label)) {
        lv_label_set_text(s_term.kb_label, connected ? "UNIT KB READY" : "NO KEYBOARD");
        lv_obj_set_style_text_color(s_term.kb_label, connected ? T_SUCCESS : T_ERROR, 0);
    }
}


static void term_set_ssh_status(term_ssh_state_t state)
{
    const char *txt = "SSH IDLE";
    lv_color_t color = T_DIM;

    switch (state) {
    case TERM_SSH_WIFI:
        txt = "WIFI...";
        color = T_WARN;
        break;
    case TERM_SSH_CONNECTING:
        txt = "SSH...";
        color = T_WARN;
        break;
    case TERM_SSH_CONNECTED:
        txt = "SSH LIVE";
        color = T_SUCCESS;
        break;
    case TERM_SSH_FAILED:
        txt = "SSH FAIL";
        color = T_ERROR;
        break;
    case TERM_SSH_IDLE:
    default:
        break;
    }

    if (s_term.ssh_dot && lv_obj_is_valid(s_term.ssh_dot)) {
        lv_obj_set_style_bg_color(s_term.ssh_dot, color, 0);
    }
    if (s_term.ssh_label && lv_obj_is_valid(s_term.ssh_label)) {
        lv_label_set_text(s_term.ssh_label, txt);
        lv_obj_set_style_text_color(s_term.ssh_label, color, 0);
    }
}

static void term_set_input_hint(const char *prompt, const char *hint, lv_color_t color)
{
    if (s_term.input_prompt && lv_obj_is_valid(s_term.input_prompt)) {
        lv_label_set_text(s_term.input_prompt, prompt ? prompt : "SSH");
    }
    if (s_term.input_label && lv_obj_is_valid(s_term.input_label)) {
        lv_label_set_text(s_term.input_label, hint ? hint : "");
        lv_obj_set_style_text_color(s_term.input_label, color, 0);
    }
}

static int terminal_vt_on_damage(VTermRect rect, void *user)
{
    (void)user;
    for (int r = rect.start_row; r < rect.end_row && r < TERM_VT_ROWS && r < 64; r++) {
        if (r >= 0) {
            s_vt_dirty_rows |= (1ULL << r);
        }
    }
    return 0;
}

static int terminal_vt_on_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    (void)visible;
    (void)user;
    if (oldpos.row >= 0 && oldpos.row < TERM_VT_ROWS && oldpos.row < 64) {
        s_vt_dirty_rows |= (1ULL << oldpos.row);
    }
    if (pos.row >= 0 && pos.row < TERM_VT_ROWS && pos.row < 64) {
        s_vt_dirty_rows |= (1ULL << pos.row);
    }
    return 0;
}

static const VTermScreenCallbacks s_vterm_cbs = {
    .damage = terminal_vt_on_damage,
    .movecursor = terminal_vt_on_movecursor,
};

static void terminal_vterm_destroy(void)
{
    if (s_vterm_timer) {
        lv_timer_del(s_vterm_timer);
        s_vterm_timer = NULL;
    }
    if (s_vt) {
        vterm_free(s_vt);
        s_vt = NULL;
        s_vt_screen = NULL;
    }
    s_vt_dirty_rows = 0;
}

static esp_err_t terminal_vterm_init(void)
{
    if (!s_vterm_lock) {
        s_vterm_lock = xSemaphoreCreateMutex();
        if (!s_vterm_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    terminal_vterm_destroy();
    s_vt = vterm_new(TERM_VT_ROWS, TERM_VT_COLS);
    if (!s_vt) {
        return ESP_ERR_NO_MEM;
    }

    vterm_set_utf8(s_vt, 1);
    s_vt_screen = vterm_obtain_screen(s_vt);
    vterm_screen_set_callbacks(s_vt_screen, &s_vterm_cbs, NULL);
    vterm_screen_reset(s_vt_screen, 1);
    s_vt_dirty_rows = (TERM_VT_ROWS >= 64) ? UINT64_MAX : ((1ULL << TERM_VT_ROWS) - 1ULL);
    return ESP_OK;
}

static void terminal_vterm_write(const char *text)
{
    if (!text || !s_vt || !s_vterm_lock) {
        return;
    }
    if (xSemaphoreTake(s_vterm_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        vterm_input_write(s_vt, text, (int)strlen(text));
        xSemaphoreGive(s_vterm_lock);
    }
}

static void terminal_vterm_write_line(const char *line)
{
    char buf[192];
    snprintf(buf, sizeof(buf), "%s\r\n", line ? line : "");
    terminal_vterm_write(buf);
}

static void terminal_vterm_writef(const char *fmt, ...)
{
    char line[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    terminal_vterm_write_line(line);
}

static void terminal_vterm_create_rows(void)
{
    if (s_term.output_area && lv_obj_is_valid(s_term.output_area)) {
        lv_obj_set_style_pad_row(s_term.output_area, 2, 0);
    }
    memset(s_term.vt_labels, 0, sizeof(s_term.vt_labels));
    for (int r = 0; r < TERM_VT_ROWS; r++) {
        lv_obj_t *row = term_make_row();
        term_line_prefix(row);

        lv_obj_t *lbl = lv_label_create(row);
        s_term.vt_labels[r] = lbl;
        lv_label_set_text(lbl, "");
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl, SCREEN_W - OUTPUT_SIDE_PAD * 2 - LINE_NO_W - CONTENT_LEFT_PAD);
        lv_obj_set_height(lbl, 20);
        lv_obj_set_style_text_color(lbl, T_DEFAULT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_inter_regular_20, 0);
        lv_obj_set_style_text_letter_space(lbl, 0, 0);
    }
}

static void terminal_vterm_render_timer(lv_timer_t *timer)
{
    (void)timer;
    if (!s_vt || !s_vt_screen || !s_vterm_lock) {
        return;
    }
    if (xSemaphoreTake(s_vterm_lock, pdMS_TO_TICKS(2)) != pdTRUE) {
        return;
    }

    VTermState *state = vterm_obtain_state(s_vt);
    VTermPos cursor = { .row = -1, .col = -1 };
    vterm_state_get_cursorpos(state, &cursor);

    uint64_t dirty = s_vt_dirty_rows;
    if (cursor.row >= 0 && cursor.row < TERM_VT_ROWS && cursor.row < 64) {
        dirty |= (1ULL << cursor.row);
    }
    s_vt_dirty_rows = 0;

    for (int row = 0; row < TERM_VT_ROWS; row++) {
        if (!(dirty & (1ULL << row))) {
            continue;
        }
        if (!s_term.vt_labels[row] || !lv_obj_is_valid(s_term.vt_labels[row])) {
            continue;
        }

        char line[TERM_VT_COLS + 1];
        for (int col = 0; col < TERM_VT_COLS; col++) {
            VTermScreenCell cell;
            VTermPos pos = { .row = row, .col = col };
            vterm_screen_get_cell(s_vt_screen, pos, &cell);
            uint32_t cp = cell.chars[0];
            line[col] = (cp >= 32 && cp < 127) ? (char)cp : ' ';
        }

        int last = TERM_VT_COLS - 1;
        while (last > 0 && line[last] == ' ') {
            last--;
        }

        if (cursor.row == row) {
            int ccol = cursor.col;
            if (ccol < 0) {
                ccol = 0;
            } else if (ccol >= TERM_VT_COLS) {
                ccol = TERM_VT_COLS - 1;
            }
            while (last < ccol) {
                line[++last] = ' ';
            }
            line[ccol] = (line[ccol] == ' ') ? '_' : line[ccol];
        }

        line[last + 1] = '\0';
        lv_label_set_text(s_term.vt_labels[row], line);
    }

    xSemaphoreGive(s_vterm_lock);
}

static void terminal_vterm_banner(void)
{
    terminal_vterm_write_line("Tab5 Terminal UI");
    terminal_vterm_writef("WiFi: %s", CONFIG_WIFI_SSID);
    terminal_vterm_writef("SSH : %s@%s:%d", CONFIG_DEFAULT_SSH_USER,
                          CONFIG_DEFAULT_SSH_HOST, CONFIG_DEFAULT_SSH_PORT);
    terminal_vterm_write_line("Press Enter to reconnect if the session drops.");
    terminal_vterm_write_line("");
}

static void terminal_ssh_status_async_cb(void *arg)
{
    ssh_status_async_arg_t *a = (ssh_status_async_arg_t *)arg;
    if (!a) {
        return;
    }

    term_set_ssh_status(a->state);
    switch (a->state) {
    case TERM_SSH_WIFI:
        term_set_input_hint("WiFi", "connecting...", T_WARN);
        break;
    case TERM_SSH_CONNECTING:
        term_set_input_hint("SSH", "handshake...", T_WARN);
        break;
    case TERM_SSH_CONNECTED:
        term_set_input_hint("SSH LIVE", "remote echo enabled", T_SUCCESS);
        break;
    case TERM_SSH_FAILED:
        term_set_input_hint("SSH", "press Enter to reconnect", T_ERROR);
        break;
    case TERM_SSH_IDLE:
    default:
        term_set_input_hint("SSH", "press Enter to connect", T_DIM);
        break;
    }
    free(a);
}

static void terminal_post_ssh_status(term_ssh_state_t state, int reason)
{
    ssh_status_async_arg_t *a = malloc(sizeof(ssh_status_async_arg_t));
    if (!a) {
        return;
    }
    a->state = state;
    a->reason = reason;
    lv_async_call(terminal_ssh_status_async_cb, a);
}

static void terminal_wifi_event_handler(void *arg, esp_event_base_t base,
                                        int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_events) {
            xEventGroupClearBits(s_wifi_events, TERM_WIFI_CONNECTED_BIT);
        }
        if (s_ssh_connected || s_ssh_connecting) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, TERM_WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t terminal_wifi_start(void)
{
    if (!s_wifi_events) {
        s_wifi_events = xEventGroupCreate();
        if (!s_wifi_events) {
            return ESP_ERR_NO_MEM;
        }
    }
    xEventGroupClearBits(s_wifi_events, TERM_WIFI_CONNECTED_BIT);

    if (!s_wifi_initialized) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS needs erase: %s", esp_err_to_name(ret));
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) {
            return ret;
        }

        ret = bsp_feature_enable(BSP_FEATURE_WIFI, true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "bsp_feature_enable(WIFI) failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(150));

        ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }

        (void)esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            return ret;
        }

        if (!s_wifi_handlers_registered) {
            ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             terminal_wifi_event_handler, NULL);
            if (ret != ESP_OK) {
                return ret;
            }
            ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             terminal_wifi_event_handler, NULL);
            if (ret != ESP_OK) {
                return ret;
            }
            s_wifi_handlers_registered = true;
        }

        ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            return ret;
        }
        s_wifi_initialized = true;
    }

    wifi_config_t wifi_cfg = {0};
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", CONFIG_WIFI_SSID);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", CONFIG_WIFI_PASS);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

static bool terminal_ssh_host_key_accept(const uint8_t *key, size_t len, void *ctx)
{
    (void)key;
    (void)len;
    (void)ctx;
    return true;
}

static void terminal_ssh_on_data(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    if (!data || len == 0 || !s_vt || !s_vterm_lock) {
        return;
    }
    if (xSemaphoreTake(s_vterm_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        vterm_input_write(s_vt, (const char *)data, (int)len);
        xSemaphoreGive(s_vterm_lock);
    }
}

static void terminal_ssh_on_connected(void *ctx)
{
    (void)ctx;
    s_ssh_connected = true;
    s_ssh_connecting = false;
    terminal_vterm_write_line("[SSH] connected");
    terminal_post_ssh_status(TERM_SSH_CONNECTED, 0);
}

static void terminal_ssh_on_disconnected(int reason, void *ctx)
{
    (void)ctx;
    s_ssh_connected = false;
    s_ssh_connecting = false;
    if (reason == 0) {
        terminal_vterm_write_line("[SSH] disconnected");
    } else {
        terminal_vterm_writef("[SSH] failed/disconnected: reason %d", reason);
    }
    terminal_vterm_write_line("Press Enter to reconnect.");
    terminal_post_ssh_status(reason == 0 ? TERM_SSH_IDLE : TERM_SSH_FAILED, reason);
}

static void terminal_connect_task(void *arg)
{
    (void)arg;

    terminal_post_ssh_status(TERM_SSH_WIFI, 0);
    terminal_vterm_writef("[WiFi] connecting to %s ...", CONFIG_WIFI_SSID);
    esp_err_t ret = terminal_wifi_start();
    if (ret != ESP_OK) {
        terminal_vterm_writef("[WiFi] start failed: %s", esp_err_to_name(ret));
        s_ssh_connecting = false;
        s_connect_task = NULL;
        terminal_post_ssh_status(TERM_SSH_FAILED, (int)ret);
        vTaskDelete(NULL);
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, TERM_WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(TERM_WIFI_TIMEOUT_MS));
    if (!(bits & TERM_WIFI_CONNECTED_BIT)) {
        terminal_vterm_write_line("[WiFi] timeout waiting for IP");
        s_ssh_connecting = false;
        s_connect_task = NULL;
        terminal_post_ssh_status(TERM_SSH_FAILED, -1);
        vTaskDelete(NULL);
        return;
    }

    terminal_post_ssh_status(TERM_SSH_CONNECTING, 0);
    terminal_vterm_writef("[SSH] connecting %s@%s:%d ...", CONFIG_DEFAULT_SSH_USER,
                          CONFIG_DEFAULT_SSH_HOST, CONFIG_DEFAULT_SSH_PORT);

    if (!s_wolfssh_initialized) {
        wolfSSH_Init();
        s_wolfssh_initialized = true;
    }

    ssh_client_config_t cfg = {
        .host = CONFIG_DEFAULT_SSH_HOST,
        .port = CONFIG_DEFAULT_SSH_PORT,
        .user = CONFIG_DEFAULT_SSH_USER,
        .privkey_pem = ssh_key_start,
        .privkey_pem_len = (size_t)(ssh_key_end - ssh_key_start),
        .pubkey_pem = ssh_pubkey_start,
        .pubkey_pem_len = (size_t)(ssh_pubkey_end - ssh_pubkey_start),
        .password = NULL,
        .term_cols = TERM_VT_COLS,
        .term_rows = TERM_VT_ROWS,
        .connect_timeout_ms = 10000,
        .callbacks = {
            .on_data = terminal_ssh_on_data,
            .on_connected = terminal_ssh_on_connected,
            .on_disconnected = terminal_ssh_on_disconnected,
            .on_host_key = terminal_ssh_host_key_accept,
            .ctx = NULL,
        },
    };

    ret = ssh_client_connect(&cfg);
    if (ret != ESP_OK) {
        terminal_vterm_writef("[SSH] start failed: %s", esp_err_to_name(ret));
        s_ssh_connecting = false;
        terminal_post_ssh_status(TERM_SSH_FAILED, (int)ret);
    }

    s_connect_task = NULL;
    vTaskDelete(NULL);
}

static void terminal_start_ssh(void)
{
    if (s_connect_task || s_ssh_connecting || s_ssh_connected) {
        return;
    }

    s_ssh_connecting = true;
    BaseType_t ok = xTaskCreate(terminal_connect_task, "term_ssh_connect",
                                TERM_CONNECT_STACK, NULL, 5, &s_connect_task);
    if (ok != pdPASS) {
        s_connect_task = NULL;
        s_ssh_connecting = false;
        terminal_vterm_write_line("[SSH] connect task create failed");
        terminal_post_ssh_status(TERM_SSH_FAILED, ESP_ERR_NO_MEM);
    }
}

static void terminal_send_to_ssh(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }

    uint8_t out[32];
    size_t out_len = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = data[i];
        if (ch == '\n') {
            ch = '\r';
        } else if (ch == '\b') {
            ch = 0x7F;
        }
        out[out_len++] = ch;
        if (out_len == sizeof(out)) {
            esp_err_t ret = ssh_client_send(out, out_len);
            if (ret != ESP_OK) {
                terminal_vterm_writef("[SSH] send failed: %s", esp_err_to_name(ret));
            }
            out_len = 0;
        }
    }
    if (out_len > 0) {
        esp_err_t ret = ssh_client_send(out, out_len);
        if (ret != ESP_OK) {
            terminal_vterm_writef("[SSH] send failed: %s", esp_err_to_name(ret));
        }
    }
}

static void terminal_handle_offline_char(uint8_t ch)
{
    if (ch == '\r' || ch == '\n') {
        terminal_start_ssh();
    } else if (ch == 0x0C) {
        if (s_vt_screen && xSemaphoreTake(s_vterm_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            vterm_screen_reset(s_vt_screen, 1);
            s_vt_dirty_rows = (TERM_VT_ROWS >= 64) ? UINT64_MAX : ((1ULL << TERM_VT_ROWS) - 1ULL);
            xSemaphoreGive(s_vterm_lock);
        }
        terminal_vterm_banner();
    } else if (ch == 0x03) {
        if (s_ssh_connecting || s_ssh_connected) {
            ssh_client_disconnect();
            terminal_vterm_write_line("[SSH] disconnect requested");
        }
    }
}

static void term_push_history(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') {
        return;
    }

    if (s_cmd_hist_count < TERM_HISTORY_MAX) {
        snprintf(s_cmd_history[s_cmd_hist_count], sizeof(s_cmd_history[s_cmd_hist_count]), "%s", cmd);
        s_cmd_hist_count++;
    } else {
        memmove(s_cmd_history[0], s_cmd_history[1], sizeof(s_cmd_history[0]) * (TERM_HISTORY_MAX - 1));
        snprintf(s_cmd_history[TERM_HISTORY_MAX - 1], sizeof(s_cmd_history[0]), "%s", cmd);
    }
    s_cmd_hist_index = s_cmd_hist_count;
}

static void term_history_prev(void)
{
    if (s_cmd_hist_count <= 0) {
        return;
    }

    if (s_cmd_hist_index <= 0) {
        s_cmd_hist_index = 0;
    } else {
        s_cmd_hist_index--;
    }

    snprintf(s_input_buf, sizeof(s_input_buf), "%s", s_cmd_history[s_cmd_hist_index]);
    s_input_len = (int)strlen(s_input_buf);
    term_update_input_label();
}

static void term_history_next(void)
{
    if (s_cmd_hist_count <= 0) {
        return;
    }

    if (s_cmd_hist_index >= s_cmd_hist_count - 1) {
        s_cmd_hist_index = s_cmd_hist_count;
        s_input_len = 0;
        s_input_buf[0] = '\0';
        term_update_input_label();
        return;
    }

    s_cmd_hist_index++;
    snprintf(s_input_buf, sizeof(s_input_buf), "%s", s_cmd_history[s_cmd_hist_index]);
    s_input_len = (int)strlen(s_input_buf);
    term_update_input_label();
}

static void term_try_autocomplete(void)
{
    size_t prefix_len = strlen(s_input_buf);
    if (prefix_len == 0) {
        return;
    }

    const char *first_match = NULL;
    int matches = 0;
    for (size_t i = 0; i < sizeof(s_cmd_catalog) / sizeof(s_cmd_catalog[0]); i++) {
        if (strncmp(s_cmd_catalog[i], s_input_buf, prefix_len) == 0) {
            if (!first_match) {
                first_match = s_cmd_catalog[i];
            }
            matches++;
        }
    }

    if (matches == 1 && first_match) {
        snprintf(s_input_buf, sizeof(s_input_buf), "%s", first_match);
        s_input_len = (int)strlen(s_input_buf);
        term_update_input_label();
        return;
    }

    if (matches > 1) {
        term_append_line(T_INFO, "matches:");
        for (size_t i = 0; i < sizeof(s_cmd_catalog) / sizeof(s_cmd_catalog[0]); i++) {
            if (strncmp(s_cmd_catalog[i], s_input_buf, prefix_len) == 0) {
                term_append_line(T_DIM, "  %s", s_cmd_catalog[i]);
            }
        }
    }
}

static void term_append_color_tags(void)
{
    const lv_color_t colors[7] = {
        lv_color_hex(0xEF4444), lv_color_hex(0xFBBF24), lv_color_hex(0x4ADE80),
        lv_color_hex(0x60A5FA), lv_color_hex(0xE879F9), lv_color_hex(0xE07B39),
        lv_color_hex(0x6B7280)
    };

    lv_obj_t *row = term_make_row();
    term_line_prefix(row);

    lv_obj_t *wrap = lv_obj_create(row);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(wrap, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(wrap, 8, 0);

    for (int i = 0; i < 7; i++) {
        lv_obj_t *tag = lv_obj_create(wrap);
        lv_obj_set_size(tag, 28, 12);
        lv_obj_set_style_bg_color(tag, colors[i], 0);
        lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tag, 0, 0);
        lv_obj_set_style_radius(tag, 4, 0);
        lv_obj_set_style_pad_all(tag, 0, 0);
    }

    term_scroll_bottom(false);
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
typedef struct {
    TaskStatus_t item;
    float pct;
} term_task_view_t;

static int task_pct_desc_cmp(const void *a, const void *b)
{
    const term_task_view_t *ta = (const term_task_view_t *)a;
    const term_task_view_t *tb = (const term_task_view_t *)b;
    if (tb->pct > ta->pct) {
        return 1;
    }
    if (tb->pct < ta->pct) {
        return -1;
    }
    return 0;
}

static char task_state_char(eTaskState s)
{
    switch (s) {
    case eRunning:   return 'R';
    case eReady:     return 'D';
    case eBlocked:   return 'S';
    case eSuspended: return 'T';
    case eDeleted:   return 'X';
    default:         return '?';
    }
}
#endif

static void term_output_top_realtime(void)
{
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0) {
        term_append_line(T_WARN, "top: no task data");
        return;
    }

    TaskStatus_t *tasks = calloc((size_t)n + 4, sizeof(TaskStatus_t));
    term_task_view_t *view = calloc((size_t)n + 4, sizeof(term_task_view_t));
    if (!tasks || !view) {
        free(tasks);
        free(view);
        term_append_line(T_ERROR, "top: out of memory");
        return;
    }

    uint32_t total_runtime = 0;
    UBaseType_t got = uxTaskGetSystemState(tasks, n + 4, &total_runtime);
    if (got == 0 || total_runtime == 0) {
        free(tasks);
        free(view);
        term_append_line(T_WARN, "top: runtime stats unavailable");
        return;
    }

    UBaseType_t running = 0;
    UBaseType_t sleeping = 0;
    for (UBaseType_t i = 0; i < got; i++) {
        view[i].item = tasks[i];
        view[i].pct = (100.0f * (float)tasks[i].ulRunTimeCounter) / (float)total_runtime;
        if (tasks[i].eCurrentState == eRunning) {
            running++;
        } else {
            sleeping++;
        }
    }

    qsort(view, got, sizeof(term_task_view_t), task_pct_desc_cmp);

    uint32_t heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    uint32_t heap_used = heap_total - heap_free;

    term_append_line(T_DIM, "Tasks: %3u total, %3u running, %3u sleeping", (unsigned)got, (unsigned)running, (unsigned)sleeping);
    term_append_line(T_DIM, "Cpu(s): stats from uxTaskGetSystemState()");
    term_append_line(T_DIM, "Mem: %4uK total %4uK used %4uK free", heap_total / 1024, heap_used / 1024, heap_free / 1024);
    term_append_line(T_DIM, "");
    term_append_line(T_DIM, "  PID  USER      %%CPU  STACK  COMMAND");

    UBaseType_t show_n = got > TOP_TASK_LIMIT ? TOP_TASK_LIMIT : got;
    for (UBaseType_t i = 0; i < show_n; i++) {
        lv_color_t cpu_c = view[i].pct > 1.5f ? T_WARN : T_SUCCESS;
        term_append_line(cpu_c, "%5lu  tab5    %5.1f  %5u  %s",
                         (unsigned long)view[i].item.xTaskNumber,
                         (double)view[i].pct,
                         (unsigned)view[i].item.usStackHighWaterMark,
                         view[i].item.pcTaskName);
    }

    free(tasks);
    free(view);
#else
    term_append_line(T_WARN, "top: enable CONFIG_FREERTOS_USE_TRACE_FACILITY for runtime data");
#endif
}

static void term_output_ps_realtime(void)
{
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0) {
        term_append_line(T_WARN, "ps: no task data");
        return;
    }

    TaskStatus_t *tasks = calloc((size_t)n + 4, sizeof(TaskStatus_t));
    if (!tasks) {
        term_append_line(T_ERROR, "ps: out of memory");
        return;
    }

    uint32_t total_runtime = 0;
    UBaseType_t got = uxTaskGetSystemState(tasks, n + 4, &total_runtime);
    if (got == 0) {
        free(tasks);
        term_append_line(T_WARN, "ps: task list unavailable");
        return;
    }

    term_append_line(T_DIM, "  PID TTY      STAT COMMAND");
    for (UBaseType_t i = 0; i < got; i++) {
        term_append_line(T_DEFAULT, "%5lu tty0     %c    %s",
                         (unsigned long)tasks[i].xTaskNumber,
                         task_state_char(tasks[i].eCurrentState),
                         tasks[i].pcTaskName);
    }
    free(tasks);
#else
    term_append_line(T_WARN, "ps: enable CONFIG_FREERTOS_USE_TRACE_FACILITY for runtime data");
#endif
}

static void term_output_motd(void)
{
    term_append_line(T_INFO, "Welcome to Tab5 terminal.");
    term_append_line(T_DIM, "Type help to see available commands.");
}

static void term_output_neofetch(void)
{
    term_append_line(T_HIGHLIGHT, "  ######## ######## ######    tab5@terminal");
    term_append_line(T_HIGHLIGHT, "  ##    ## ##    ## ##   ##  -------------------------");
    term_append_line(T_HIGHLIGHT, "     ##    #######  ######   OS      CardKB-Linux 1.0");
    term_append_line(T_HIGHLIGHT, "     ##    ##       ##       Host    ESP32-P4 + C6");
    term_append_line(T_HIGHLIGHT, "     ##    ##       ##       Kernel  5.15.0-esp32p4");
    term_append_line(T_HIGHLIGHT, "     ##    ##       ##       Shell   bash 5.1.16");
    term_append_line(T_DEFAULT,   "                              KEYBOARD M5Unit-KEYBOARD [I2C]");
    term_append_line(T_DEFAULT,   "                              RAM     288M / 512M");
    term_append_line(T_DEFAULT,   "                              Display 1280x720 IPS");
    term_append_line(T_DIM,       "");
    term_append_color_tags();
}

static void term_output_help(void)
{
    term_append_line(T_INFO, "System:");
    term_append_line(T_DEFAULT, "  help clear neofetch uname -a uptime date pwd whoami");
    term_append_line(T_INFO, "File:");
    term_append_line(T_DEFAULT, "  ls ls -la cat /etc/motd");
    term_append_line(T_INFO, "Network:");
    term_append_line(T_DEFAULT, "  ifconfig ping keyboard status gamepage");
    term_append_line(T_INFO, "Process:");
    term_append_line(T_DEFAULT, "  top ps");
    term_append_line(T_INFO, "Fun:");
    term_append_line(T_DEFAULT, "  fortune sudo reboot");
}

static void ping_timer_cb(lv_timer_t *t)
{
    ping_ctx_t *ctx = (ping_ctx_t *)lv_timer_get_user_data(t);
    if (!ctx) {
        return;
    }

    int ms = 11 + (esp_random() % 6);
    term_append_line(T_DEFAULT, "64 bytes from 8.8.8.8: icmp_seq=%d ttl=118 time=%d ms", ctx->seq, ms);
    ctx->seq++;

    if (ctx->seq >= 5) {
        term_append_line(T_SUCCESS, "5 packets transmitted, 5 received, 0%% packet loss");
        term_append_line(T_DEFAULT, "round-trip min/avg/max = 11/13/16 ms");
        lv_timer_del(s_ping_timer);
        s_ping_timer = NULL;
        free(ctx);
    }
}

static void reboot_timer_cb(lv_timer_t *t)
{
    (void)t;
    term_append_line(T_DIM, "booting...");
    term_output_neofetch();
}

static void term_unknown_cmd(const char *cmd)
{
    term_append_line(T_ERROR, "bash: %s: command not found", cmd ? cmd : "");
    term_append_line(T_DIM, "Type help to see available commands.");
}

static void term_process_command(const char *raw_cmd)
{
    char cmd[TERM_MAX_INPUT + 1];
    char work[TERM_MAX_INPUT + 1];
    char *first = NULL;

    if (!raw_cmd) {
        return;
    }

    snprintf(cmd, sizeof(cmd), "%s", raw_cmd);

    
    char *start = cmd;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (*start == '\0') {
        return;
    }

    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t')) {
        *end-- = '\0';
    }

    term_append_prompt_line(start);
    term_push_history(start);

    snprintf(work, sizeof(work), "%s", start);
    first = strtok(work, " ");
    if (!first) {
        return;
    }

    if (strcmp(start, "help") == 0) {
        term_output_help();
    } else if (strcmp(start, "clear") == 0) {
        term_reset_lines();
    } else if (strcmp(start, "neofetch") == 0) {
        term_output_neofetch();
    } else if (strcmp(start, "uname -a") == 0) {
        term_append_line(T_DEFAULT, "Linux tab5 5.15.0-esp32p4 #1 SMP PREEMPT");
    } else if (strcmp(start, "uptime") == 0) {
        int64_t us = esp_timer_get_time();
        int64_t sec = us / 1000000;
        int h = (int)(sec / 3600);
        int m = (int)((sec % 3600) / 60);
        int s = (int)(sec % 60);
        term_append_line(T_DEFAULT, "up %02d:%02d:%02d", h, m, s);
    } else if (strcmp(start, "date") == 0) {
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        if (tm_now) {
            char out[64];
            strftime(out, sizeof(out), "%Y-%m-%d %H:%M:%S", tm_now);
            term_append_line(T_INFO, "%s", out);
        } else {
            term_append_line(T_WARN, "time source not ready");
        }
    } else if (strcmp(start, "pwd") == 0) {
        term_append_line(T_INFO, "/home/tab5");
    } else if (strcmp(start, "whoami") == 0) {
        term_append_line(T_DEFAULT, "tab5");
    } else if (strcmp(start, "ls") == 0) {
        term_append_line(T_INFO, "bin  etc  home  proc  tmp  usr");
    } else if (strcmp(start, "ls -la") == 0) {
        term_append_line(T_DIM,   "drwxr-xr-x  root root   4096 Mar 31 00:00 .");
        term_append_line(T_DIM,   "drwxr-xr-x  root root   4096 Mar 31 00:00 ..");
        term_append_line(T_INFO,  "drwxr-xr-x  tab5 tab5   4096 Mar 31 00:00 home");
        term_append_line(T_INFO,  "drwxr-xr-x  root root   4096 Mar 31 00:00 etc");
        term_append_line(T_DEFAULT, "-rw-r--r--  root root    128 Mar 31 00:00 motd");
    } else if (strncmp(start, "cat ", 4) == 0) {
        const char *path = start + 4;
        if (strcmp(path, "/etc/motd") == 0) {
            term_append_line(T_DEFAULT, "Welcome to Tab5 Terminal UI.");
            term_append_line(T_SUCCESS, "M5Unit keyboard ready. Have fun.");
        } else {
            term_append_line(T_ERROR, "cat: %s: No such file or directory", path);
        }
    } else if (strcmp(start, "ifconfig") == 0) {
        term_append_line(T_INFO, "wlan0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>");
        term_append_line(T_DEFAULT, "    inet 192.168.4.1  netmask 255.255.255.0");
        term_append_line(T_DEFAULT, "    ether 24:6f:28:aa:bb:cc  txqueuelen 1000");
        term_append_line(T_DIM, "    RX packets 512  TX packets 490");
    } else if (strcmp(start, "ping") == 0 || strcmp(start, "ping 8.8.8.8") == 0) {
        if (!s_ping_timer) {
            ping_ctx_t *ctx = calloc(1, sizeof(ping_ctx_t));
            if (!ctx) {
                term_append_line(T_ERROR, "ping: out of memory");
            } else {
                term_append_line(T_DEFAULT, "PING 8.8.8.8 (8.8.8.8): 56 data bytes");
                s_ping_timer = lv_timer_create(ping_timer_cb, 300, ctx);
            }
        } else {
            term_append_line(T_WARN, "ping already running");
        }
    } else if (strcmp(start, "keyboard status") == 0) {
        term_append_line(T_DEFAULT, "M5Unit-KEYBOARD: %s",
                         s_keyboard_connected ? "connected" : "not detected");
    } else if (strcmp(start, "gamepage") == 0) {
        build_gamepage();
    } else if (strcmp(start, "top") == 0) {
        term_output_top_realtime();
    } else if (strcmp(start, "ps") == 0) {
        term_output_ps_realtime();
    } else if (strcmp(start, "fortune") == 0) {
        term_append_line(T_HIGHLIGHT, "Any sufficiently advanced bug is indistinguishable from a feature.");
    } else if (strncmp(start, "sudo ", 5) == 0) {
        if (strcmp(start, "sudo rm -rf /") == 0) {
            term_append_line(T_WARN, "[sudo] password for tab5: ********");
            term_append_line(T_ERROR, "Permission denied, phew.");
        } else {
            term_append_line(T_WARN, "[sudo] password for tab5:");
            term_append_line(T_ERROR, "Sorry, try again.");
            term_append_line(T_ERROR, "Sorry, try again.");
            term_append_line(T_ERROR, "Sorry, try again.");
            term_append_line(T_HIGHLIGHT, "Nice try.");
        }
    } else if (strcmp(start, "reboot") == 0) {
        term_append_line(T_WARN, "Broadcast message from root@tab5");
        term_append_line(T_DIM,  "The system is going down for reboot NOW!");
        lv_timer_t *tm = lv_timer_create(reboot_timer_cb, 1500, NULL);
        if (tm) {
            lv_timer_set_repeat_count(tm, 1);
        }
    } else {
        term_unknown_cmd(start);
    }
}

static void term_submit_input(void)
{
    if (s_input_len <= 0) {
        s_input_buf[0] = '\0';
        term_update_input_label();
        return;
    }

    term_process_command(s_input_buf);
    s_input_len = 0;
    s_input_buf[0] = '\0';
    term_update_input_label();
}

static void term_handle_char(uint8_t ch)
{
    if (s_esc_state == 1) {
        s_esc_state = (ch == '[') ? 2 : 0;
        return;
    }
    if (s_esc_state == 2) {
        if (ch == 'A') {
            term_history_prev();
        } else if (ch == 'B') {
            term_history_next();
        }
        s_esc_state = 0;
        return;
    }

    if (ch == 0x1B) {
        s_esc_state = 1;
        return;
    }

    if (ch == '\r' || ch == '\n') {
        term_submit_input();
        return;
    }

    if (ch == '\t') {
        term_try_autocomplete();
        return;
    }

    if (ch == '\b' || ch == 0x7F) {
        if (s_input_len > 0) {
            s_input_len--;
            s_input_buf[s_input_len] = '\0';
            term_update_input_label();
        }
        return;
    }

    if (ch == 0x0C) {
        
        term_reset_lines();
        return;
    }

    if (ch == 0x03) {
        
        s_input_len = 0;
        s_input_buf[0] = '\0';
        s_cmd_hist_index = s_cmd_hist_count;
        term_update_input_label();
        return;
    }

    
    if (ch >= 0x20 && ch <= 0x7E) {
        if (s_input_len < TERM_MAX_INPUT) {
            s_input_buf[s_input_len++] = (char)ch;
            s_input_buf[s_input_len] = '\0';
            s_cmd_hist_index = s_cmd_hist_count;
            term_update_input_label();
        }
    }
}

static void key_async_cb(void *arg)
{
    key_async_arg_t *a = (key_async_arg_t *)arg;
    if (!a) {
        return;
    }
    uint8_t ch = a->ch;
    free(a);
    if (s_remote_mode) {
        terminal_handle_offline_char(ch);
    } else {
        term_handle_char(ch);
    }
}

static void keyboard_recv_cb(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }

    if (s_remote_mode && s_ssh_connected) {
        terminal_send_to_ssh(data, len);
        return;
    }

    for (size_t i = 0; i < len; i++) {
        key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
        if (!a) {
            return;
        }
        a->ch = data[i];
        lv_async_call(key_async_cb, a);
    }
}

static void keyboard_status_async_cb(void *arg)
{
    status_async_arg_t *a = (status_async_arg_t *)arg;
    if (!a) {
        return;
    }

    s_keyboard_connected = !a->is_disconnected;
    term_set_keyboard_status(s_keyboard_connected);
    if (s_remote_mode) {
        terminal_vterm_write_line(s_keyboard_connected ?
                                  "[KEYBOARD] M5Unit-KEYBOARD connected" :
                                  "[KEYBOARD] disconnected");
    } else if (s_keyboard_connected) {
        term_append_line(T_SUCCESS, "[KEYBOARD] M5Unit-KEYBOARD connected");
    } else {
        term_append_line(T_ERROR, "[KEYBOARD] disconnected");
    }
    free(a);
}

static void keyboard_status_cb(bool connected)
{
    status_async_arg_t *a = malloc(sizeof(status_async_arg_t));
    if (!a) {
        return;
    }
    a->is_disconnected = !connected;
    lv_async_call(keyboard_status_async_cb, a);
}

static void build_topbar(lv_obj_t *root)
{
    lv_obj_t *bar = lv_obj_create(root);
    s_term.topbar = bar;
    lv_obj_set_size(bar, SCREEN_W, TOPBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, C_BORDER, 0);
    lv_obj_set_style_pad_left(bar, 16, 0);
    lv_obj_set_style_pad_right(bar, 16, 0);
    lv_obj_set_style_pad_top(bar, 0, 0);
    lv_obj_set_style_pad_bottom(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = lv_obj_create(bar);
    lv_obj_remove_style_all(left);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 10, 0);

    const lv_color_t dots[3] = {
        lv_color_hex(0xFF5F57),
        lv_color_hex(0xFEBC2E),
        lv_color_hex(0x28C840)
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *d = lv_obj_create(left);
        lv_obj_set_size(d, 13, 13);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, dots[i], 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
    }

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Tab5@terminal - SYSTEM STATUS");
    lv_obj_set_style_text_color(title, T_DIM, 0);
    lv_obj_set_style_text_font(title, &lv_font_inter_regular_20, 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);

    lv_obj_t *status = lv_obj_create(bar);
    lv_obj_remove_style_all(status);
    lv_obj_set_layout(status, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(status, 10, 0);

    lv_obj_t *ssh_dot = lv_obj_create(status);
    s_term.ssh_dot = ssh_dot;
    lv_obj_set_size(ssh_dot, 10, 10);
    lv_obj_set_style_radius(ssh_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ssh_dot, T_DIM, 0);
    lv_obj_set_style_bg_opa(ssh_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ssh_dot, 0, 0);
    lv_obj_set_style_pad_all(ssh_dot, 0, 0);

    lv_obj_t *ssh_lbl = lv_label_create(status);
    s_term.ssh_label = ssh_lbl;
    lv_label_set_text(ssh_lbl, "SSH IDLE");
    lv_obj_set_style_text_color(ssh_lbl, T_DIM, 0);
    lv_obj_set_style_text_font(ssh_lbl, &lv_font_inter_bold_24, 0);

    lv_obj_t *sdot = lv_obj_create(status);
    s_term.kb_dot = sdot;
    lv_obj_set_size(sdot, 10, 10);
    lv_obj_set_style_radius(sdot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sdot, s_keyboard_connected ? T_SUCCESS : T_ERROR, 0);
    lv_obj_set_style_bg_opa(sdot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sdot, 0, 0);
    lv_obj_set_style_pad_all(sdot, 0, 0);

    lv_obj_t *slbl = lv_label_create(status);
    s_term.kb_label = slbl;
    lv_label_set_text(slbl, s_keyboard_connected ? "UNIT KB READY" : "NO KEYBOARD");
    lv_obj_set_style_text_color(slbl, s_keyboard_connected ? T_SUCCESS : T_ERROR, 0);
    lv_obj_set_style_text_font(slbl, &lv_font_inter_bold_24, 0);
}

static void build_output_area(lv_obj_t *root)
{
    lv_obj_t *area = lv_obj_create(root);
    s_term.output_area = area;
    lv_obj_set_pos(area, 0, TOPBAR_H);
    lv_obj_set_size(area, SCREEN_W, SCREEN_H - TOPBAR_H - INPUT_H);
    lv_obj_set_style_bg_color(area, C_BG, 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_pad_left(area, OUTPUT_SIDE_PAD, 0);
    lv_obj_set_style_pad_right(area, OUTPUT_SIDE_PAD, 0);
    lv_obj_set_style_pad_top(area, 14, 0);
    lv_obj_set_style_pad_bottom(area, 14, 0);
    lv_obj_set_layout(area, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(area, 8, 0);
    lv_obj_add_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(area, LV_SCROLLBAR_MODE_OFF);
}

static void build_input_row(lv_obj_t *root)
{
    lv_obj_t *row = lv_obj_create(root);
    s_term.input_row = row;
    lv_obj_set_pos(row, 0, SCREEN_H - INPUT_H);
    lv_obj_set_size(row, SCREEN_W, INPUT_H);
    lv_obj_set_style_bg_color(row, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(row, C_BORDER, 0);
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 16, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t *prompt = lv_label_create(row);
    s_term.input_prompt = prompt;
    lv_label_set_text(prompt, "SSH");
    lv_obj_set_style_text_color(prompt, T_PROMPT, 0);
    lv_obj_set_style_text_font(prompt, &lv_font_inter_regular_20, 0);

    lv_obj_t *in = lv_label_create(row);
    s_term.input_label = in;
    lv_label_set_text(in, "");
    lv_obj_set_style_text_color(in, T_CMD, 0);
    lv_obj_set_style_text_font(in, &lv_font_inter_regular_20, 0);
    lv_obj_set_flex_grow(in, 1);
    lv_label_set_long_mode(in, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *cursor = lv_obj_create(row);
    s_term.cursor = cursor;
    lv_obj_set_size(cursor, 11, 22);
    lv_obj_set_style_radius(cursor, 0, 0);
    lv_obj_set_style_border_width(cursor, 0, 0);
    lv_obj_set_style_pad_all(cursor, 0, 0);
    lv_obj_set_style_bg_color(cursor, T_PROMPT, 0);
    lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, cursor);
    lv_anim_set_exec_cb(&a, cursor_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 450);
    lv_anim_set_reverse_duration(&a, 450);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void build_page5(bool is_bt)
{
    (void)is_bt;
    lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_90);

    if (s_ssh_connected || s_ssh_connecting) {
        ssh_client_disconnect();
    }
    terminal_vterm_destroy();

    memset(&s_term, 0, sizeof(s_term));
    memset(s_input_buf, 0, sizeof(s_input_buf));
    s_input_len = 0;
    s_cmd_hist_count = 0;
    s_cmd_hist_index = -1;
    s_keyboard_connected = false;
    s_ssh_connected = false;
    s_ssh_connecting = false;
    s_remote_mode = true;

    if (s_ping_timer) {
        lv_timer_del(s_ping_timer);
        s_ping_timer = NULL;
    }

    keyboard_mgr_register_recv_cb(keyboard_recv_cb);
    keyboard_mgr_register_status_cb(keyboard_status_cb);
    esp_err_t ret = keyboard_mgr_init();
    if (ret == ESP_OK) {
        s_keyboard_connected = keyboard_mgr_is_active();
    } else {
        ESP_LOGE(TAG, "M5Unit-KEYBOARD init failed: %s", esp_err_to_name(ret));
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    s_term.root = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    build_topbar(scr);
    build_output_area(scr);
    build_input_row(scr);

    term_reset_lines();
    ret = terminal_vterm_init();
    if (ret == ESP_OK) {
        terminal_vterm_create_rows();
        s_vterm_timer = lv_timer_create(terminal_vterm_render_timer, 33, NULL);
        terminal_vterm_banner();
        terminal_vterm_render_timer(NULL);
        term_set_ssh_status(TERM_SSH_IDLE);
        term_set_input_hint("SSH", "starting...", T_WARN);
    } else {
        s_remote_mode = false;
        term_output_neofetch();
        term_append_line(T_ERROR, "terminal parser init failed: %s", esp_err_to_name(ret));
    }

    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 500, 0, true);

    if (s_remote_mode) {
        terminal_start_ssh();
    }
}
