#include "ui_common.h"
#include "wireless_mgr.h"
#include "uart_mgr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#define TERM_MAX_LINES       500
#define TERM_LINE_LEN        256
#define TERM_MAX_INPUT       220
#define TERM_HISTORY_MAX     20

#define TOPBAR_H             64
#define INPUT_H              68
#define LINE_NO_W            48
#define CONTENT_LEFT_PAD     24
#define OUTPUT_SIDE_PAD      20

#define TOP_TASK_LIMIT       10

static const char *s_cmd_catalog[] = {
    "help", "clear", "neofetch", "uname -a", "uptime", "date", "pwd", "whoami",
    "ls", "ls -la", "cat /etc/motd", "ifconfig", "ping", "ping 8.8.8.8",
    "ble status", "espnow scan", "top", "ps", "fortune", "sudo", "sudo rm -rf /", "reboot"
};

static const uint8_t s_keymap_normal[4][11] = {
    {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x00},
    {0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6F, 0x70, 0x08},
    {0x41, 0x61, 0x73, 0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6C, 0x0A},
    {0x46, 0x53, 0x7A, 0x78, 0x63, 0x76, 0x62, 0x6E, 0x6D, 0x20, 0x00}
};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *topbar;
    lv_obj_t *output_area;
    lv_obj_t *input_row;
    lv_obj_t *input_prompt;
    lv_obj_t *input_label;
    lv_obj_t *cursor;
    lv_obj_t *ble_dot;
    lv_obj_t *ble_label;
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

typedef struct {
    int seq;
} ping_ctx_t;

static ui_terminal_t s_term;
static bool          s_is_bt = false;
static char          s_input_buf[TERM_MAX_INPUT + 1];
static int           s_input_len = 0;
static char          s_cmd_history[TERM_HISTORY_MAX][TERM_MAX_INPUT + 1];
static int           s_cmd_hist_count = 0;
static int           s_cmd_hist_index = -1;
static lv_timer_t   *s_ping_timer = NULL;
static uint8_t       s_esc_state = 0;

static bool espnow_key_to_ascii(uint8_t key_id, uint8_t state, uint8_t *out_ch)
{
    if (!out_ch || state != 0x01 || key_id >= 44) {
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

static void term_set_ble_status(bool connected)
{
    if (s_term.ble_dot && lv_obj_is_valid(s_term.ble_dot)) {
        lv_obj_set_style_bg_color(s_term.ble_dot, connected ? T_SUCCESS : T_ERROR, 0);
    }
    if (s_term.ble_label && lv_obj_is_valid(s_term.ble_label)) {
        lv_label_set_text(s_term.ble_label, connected ? "CONNECTED" : "DISCONNECTED");
        lv_obj_set_style_text_color(s_term.ble_label, connected ? T_SUCCESS : T_ERROR, 0);
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
    term_append_line(T_DEFAULT,   "                              BLE     CardKB2-A1B2 [CONNECTED]");
    term_append_line(T_DEFAULT,   "                              ESPNOW  [ACTIVE] ch.6");
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
    term_append_line(T_DEFAULT, "  ifconfig ping ble status espnow scan");
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
            term_append_line(T_SUCCESS, "BLE keyboard ready. Have fun.");
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
    } else if (strcmp(start, "ble status") == 0) {
        term_append_line(T_DEFAULT, "BLE: %s", s_is_bt ? "connected" : "inactive");
        term_append_line(T_INFO, "Paired device: CardKB2-A1B2");
    } else if (strcmp(start, "espnow scan") == 0) {
        term_append_line(T_INFO, "Scanning nearby ESP-NOW nodes...");
        term_append_line(T_DEFAULT, "1) node-kb2  ch.6  -56 dBm");
        term_append_line(T_DEFAULT, "2) sensor-c6 ch.6  -63 dBm");
        term_append_line(T_SUCCESS, "scan complete");
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

    
    static bool fn_held = false;
    if (ch == 0x10) { 
        fn_held = true;
        return;
    }
    if (ch == 0x11) { 
        fn_held = false;
        return;
    }
    if ((ch == 'q' || ch == 'Q') && fn_held) {
        
        lv_obj_t *scr = lv_disp_get_scr_act(NULL);
        example_lvgl_demo_ui(scr);
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
    term_handle_char(ch);
}

static void p5_recv_cb(bool is_bt, const uint8_t mac[6], const uint8_t *data, size_t len)
{
    (void)mac;

    if (!data || len == 0) {
        return;
    }

    if (!is_bt && len == 5 && data[0] == 0xAA && data[1] == 0x03) {
        uint8_t ch = 0;
        if (espnow_key_to_ascii(data[2], data[3], &ch)) {
            key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
            if (a) {
                a->ch = ch;
                lv_async_call(key_async_cb, a);
            }
        }
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

static void uart_recv_cb(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }

    bool parsed_frame = false;
    size_t i = 0;
    while (i + 5 <= len) {
        if (data[i] == 0xAA && data[i + 1] == 0x03) {
            parsed_frame = true;
            uint8_t ch = 0;
            if (espnow_key_to_ascii(data[i + 2], data[i + 3], &ch)) {
                key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
                if (a) {
                    a->ch = ch;
                    lv_async_call(key_async_cb, a);
                }
            }
            i += 5;
        } else {
            i++;
        }
    }

    if (!parsed_frame) {
        for (size_t j = 0; j < len; j++) {
            if (data[j] >= 0x03 && data[j] <= 0x7F) {
                key_async_arg_t *a = malloc(sizeof(key_async_arg_t));
                if (!a) {
                    return;
                }
                a->ch = data[j];
                lv_async_call(key_async_cb, a);
            }
        }
    }
}

static void p5_status_async_cb(void *arg)
{
    status_async_arg_t *a = (status_async_arg_t *)arg;
    if (!a) {
        return;
    }
    term_set_ble_status(!a->is_disconnected);
    if (!a->is_disconnected) {
        term_append_line(T_SUCCESS, "[BLE] CardKB2 connected");
    } else {
        term_append_line(T_ERROR, "[BLE] disconnected");
    }
    free(a);
}

static void p5_status_cb(bool is_disconnected)
{
    status_async_arg_t *a = malloc(sizeof(status_async_arg_t));
    if (!a) {
        return;
    }
    a->is_disconnected = is_disconnected;
    lv_async_call(p5_status_async_cb, a);
}

static void build_topbar(lv_obj_t *root, bool is_bt)
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

    lv_obj_t *sdot = lv_obj_create(status);
    s_term.ble_dot = sdot;
    lv_obj_set_size(sdot, 10, 10);
    lv_obj_set_style_radius(sdot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sdot, is_bt ? T_SUCCESS : T_INFO, 0);
    lv_obj_set_style_bg_opa(sdot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sdot, 0, 0);
    lv_obj_set_style_pad_all(sdot, 0, 0);

    lv_obj_t *slbl = lv_label_create(status);
    s_term.ble_label = slbl;
    lv_label_set_text(slbl, is_bt ? "CONNECTED" : "UART ACTIVE");
    lv_obj_set_style_text_color(slbl, is_bt ? T_SUCCESS : T_INFO, 0);
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
    lv_label_set_text(prompt, "Tab5@terminal : ~ $");
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
    memset(&s_term, 0, sizeof(s_term));
    memset(s_input_buf, 0, sizeof(s_input_buf));
    s_input_len = 0;
    s_cmd_hist_count = 0;
    s_cmd_hist_index = -1;
    s_is_bt = is_bt;

    if (s_ping_timer) {
        lv_timer_del(s_ping_timer);
        s_ping_timer = NULL;
    }

    if (is_bt) {
        wireless_register_recv_cb(p5_recv_cb);
        wireless_register_status_cb(p5_status_cb);
    } else {
        esp_err_t ret = uart_mgr_init();
        if (ret == ESP_OK) {
            uart_mgr_register_recv_cb(uart_recv_cb);
            const char *test_msg = "TAB5 terminal ready\r\n";
            uart_mgr_send((const uint8_t *)test_msg, strlen(test_msg));
        } else {
            ESP_LOGE("ui/page5", "UART init failed: %s", esp_err_to_name(ret));
        }
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    s_term.root = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    build_topbar(scr, is_bt);
    build_output_area(scr);
    build_input_row(scr);

    term_reset_lines();
    term_output_neofetch();
    term_output_motd();

    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 500, 0, true);
}