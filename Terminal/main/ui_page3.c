/*
 * ui_page3.c — Connect overlay: 12-dot loading spinner → "Successfully Connected"
 * Page 3 of 4
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_common.h"
#include "wireless_mgr.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ui_page3";

/* ── Overlay state (extern-declared in ui_common.h) ─────────────── */
lv_obj_t *g_p2_overlay  = NULL;
lv_obj_t *g_p2_spinner  = NULL;
lv_obj_t *g_p2_result   = NULL;
lv_obj_t *g_connect_btn = NULL;

/* ── Forward declaration for animation completion callback ─────── */
static void overlay_delete_anim_done(lv_anim_t *a);
static void overlay_delete_only_anim_done(lv_anim_t *a);

/* ── Animation callbacks ─────────────────────────────────────────── */
static void overlay_opa_anim_cb(void *obj, int32_t val)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
}

static void obj_opa_anim_cb(void *obj, int32_t val)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
}

static void dot_scale_anim_cb(void *obj, int32_t scale)
{
    lv_obj_t *dot = (lv_obj_t *)obj;
    lv_obj_set_style_transform_scale_x(dot, scale, 0);
    lv_obj_set_style_transform_scale_y(dot, scale, 0);
}

static void spinner_rotate_anim_cb(void *obj, int32_t angle)
{
    lv_obj_set_style_transform_rotation((lv_obj_t *)obj, angle, 0);
}

/* ── Stored connection context for the success timer ────────────── */
static connect_btn_ctx_t s_ctx;   /* copied from heap before freeing */
static bool s_waiting_ble_result = false;
static bool s_result_finalized   = false;
static lv_timer_t *s_ble_timeout_timer = NULL;

typedef struct {
    bool is_disconnected;
} p3_status_async_arg_t;

/* ── Timer callbacks ─────────────────────────────────────────────── */
static void result_show_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    if (!g_p2_result || !g_p2_spinner) return;

    lv_obj_add_flag(g_p2_spinner, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t ra;
    lv_anim_init(&ra);
    lv_anim_set_exec_cb(&ra, obj_opa_anim_cb);
    lv_anim_set_var(&ra, g_p2_result);
    lv_anim_set_values(&ra, 0, 255);
    lv_anim_set_duration(&ra, 400);
    lv_anim_start(&ra);
}

static void show_result_label(const char *text, lv_color_t color)
{
    if (!g_p2_result || !g_p2_spinner) return;

    lv_obj_add_flag(g_p2_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_p2_result, text);
    lv_obj_set_style_text_color(g_p2_result, color, 0);

    lv_anim_t ra;
    lv_anim_init(&ra);
    lv_anim_set_exec_cb(&ra, obj_opa_anim_cb);
    lv_anim_set_var(&ra, g_p2_result);
    lv_anim_set_values(&ra, 0, 255);
    lv_anim_set_duration(&ra, 220);
    lv_anim_start(&ra);
}

static void success_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);

    /* Mark the tapped button as CONNECTED */
    if (g_connect_btn && lv_obj_is_valid(g_connect_btn)) {
        lv_obj_set_style_bg_color(g_connect_btn, C_CONNECTED, 0);
        lv_obj_clear_flag(g_connect_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *lbl = lv_obj_get_child(g_connect_btn, 0);
        if (lbl) {
            lv_label_set_text(lbl, "CONNECTED");
            lv_obj_set_style_text_color(lbl, C_PURE_WHITE, 0);
        }
    }

    /* Fade out and destroy the overlay */
    if (g_p2_overlay && lv_obj_is_valid(g_p2_overlay)) {
        lv_anim_t oa;
        lv_anim_init(&oa);
        lv_anim_set_exec_cb(&oa, overlay_opa_anim_cb);
        lv_anim_set_var(&oa, g_p2_overlay);
        lv_anim_set_values(&oa, lv_obj_get_style_bg_opa(g_p2_overlay, 0), 0);
        lv_anim_set_duration(&oa, 220);
        lv_anim_set_completed_cb(&oa, overlay_delete_anim_done);
        lv_anim_start(&oa);
    } else {
        g_p2_overlay  = NULL;
        g_p2_spinner  = NULL;
        g_p2_result   = NULL;
        g_connect_btn = NULL;
    }
}

static void failure_close_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);

    if (g_p2_overlay && lv_obj_is_valid(g_p2_overlay)) {
        lv_anim_t oa;
        lv_anim_init(&oa);
        lv_anim_set_exec_cb(&oa, overlay_opa_anim_cb);
        lv_anim_set_var(&oa, g_p2_overlay);
        lv_anim_set_values(&oa, lv_obj_get_style_bg_opa(g_p2_overlay, 0), 0);
        lv_anim_set_duration(&oa, 220);
        lv_anim_set_completed_cb(&oa, overlay_delete_only_anim_done);
        lv_anim_start(&oa);
    } else {
        g_p2_overlay  = NULL;
        g_p2_spinner  = NULL;
        g_p2_result   = NULL;
        g_connect_btn = NULL;
    }
}

static void overlay_delete_anim_done(lv_anim_t *a)
{
    lv_obj_t *overlay = (lv_obj_t *)a->var;
    if (overlay && lv_obj_is_valid(overlay)) lv_obj_delete(overlay);
    g_p2_overlay  = NULL;
    g_p2_spinner  = NULL;
    g_p2_result   = NULL;
    g_connect_btn = NULL;

    /* Navigate to the terminal screen */
    build_page5(g_p2_is_bt);
}

static void overlay_delete_only_anim_done(lv_anim_t *a)
{
    lv_obj_t *overlay = (lv_obj_t *)a->var;
    if (overlay && lv_obj_is_valid(overlay)) lv_obj_delete(overlay);
    g_p2_overlay  = NULL;
    g_p2_spinner  = NULL;
    g_p2_result   = NULL;
    g_connect_btn = NULL;
}

static void finalize_ble_connect(bool success)
{
    if (s_result_finalized) return;
    s_result_finalized = true;
    s_waiting_ble_result = false;

    if (s_ble_timeout_timer) {
        lv_timer_delete(s_ble_timeout_timer);
        s_ble_timeout_timer = NULL;
    }

    wireless_register_status_cb(NULL);

    if (success) {
        show_result_label("Successfully Connected", C_PURE_WHITE);
        lv_timer_create(success_timer_cb, 900, NULL);
    } else {
        show_result_label("Connection Failed", lv_color_hex(0xFF6B6B));
        lv_timer_create(failure_close_timer_cb, 1200, NULL);
    }
}

static void p3_status_async_cb(void *arg)
{
    p3_status_async_arg_t *a = (p3_status_async_arg_t *)arg;
    if (!a) return;
    bool is_disconnected = a->is_disconnected;
    free(a);

    if (!s_waiting_ble_result) return;
    finalize_ble_connect(!is_disconnected);
}

static void p3_status_cb(bool is_disconnected)
{
    p3_status_async_arg_t *a = malloc(sizeof(p3_status_async_arg_t));
    if (!a) return;
    a->is_disconnected = is_disconnected;
    lv_async_call(p3_status_async_cb, a);
}

static void ble_timeout_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    s_ble_timeout_timer = NULL;
    if (!s_waiting_ble_result) return;
    finalize_ble_connect(false);
}

/* ── CONNECT button click handler (called from ui_page2.c) ──────── */
void connect_click_cb(lv_event_t *e)
{
    connect_btn_ctx_t *ctx = (connect_btn_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    /* Copy context to static storage and free the heap block */
    s_ctx = *ctx;
    free(ctx);

    lv_obj_t *scr = s_ctx.scr;
    if (!scr) return;
    g_connect_btn = lv_event_get_target(e);

    /* Block additional taps while overlay is active */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Semi-transparent black overlay */
    lv_obj_t *overlay = lv_obj_create(scr);
    g_p2_overlay = overlay;
    lv_obj_set_size(overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, C_BLACK, 0);
    lv_obj_set_style_bg_opa(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_anim_t oa;
    lv_anim_init(&oa);
    lv_anim_set_exec_cb(&oa, overlay_opa_anim_cb);
    lv_anim_set_var(&oa, overlay);
    lv_anim_set_values(&oa, 0, 160);
    lv_anim_set_duration(&oa, 200);
    lv_anim_start(&oa);

    /* 12-dot rotating spinner (svg-spinners/12-dots-scale-rotate style) */
    lv_obj_t *spinner = lv_obj_create(overlay);
    g_p2_spinner = spinner;
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_center(spinner);
    lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spinner, 0, 0);
    lv_obj_set_style_pad_all(spinner, 0, 0);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_transform_pivot_x(spinner, 40, 0);
    lv_obj_set_style_transform_pivot_y(spinner, 40, 0);

    /* Dot positions computed from a 74px-diameter ring (r=37) centred in 80×80 */
    static const int8_t dot_x[12] = {37, 52, 63, 67, 63, 52, 37, 22, 11,  7, 11, 22};
    static const int8_t dot_y[12] = { 7, 11, 22, 37, 52, 63, 67, 63, 52, 37, 22, 11};

    for (int i = 0; i < 12; i++) {
        lv_obj_t *dot = lv_obj_create(spinner);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_pos(dot, dot_x[i], dot_y[i]);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, C_PURE_WHITE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_transform_pivot_x(dot, 3, 0);
        lv_obj_set_style_transform_pivot_y(dot, 3, 0);

        lv_anim_t da;
        lv_anim_init(&da);
        lv_anim_set_exec_cb(&da, dot_scale_anim_cb);
        lv_anim_set_var(&da, dot);
        lv_anim_set_values(&da, 128, 256);
        lv_anim_set_duration(&da, 300);
        lv_anim_set_reverse_duration(&da, 300);
        lv_anim_set_delay(&da, (uint32_t)(i * 100));
        lv_anim_set_repeat_count(&da, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&da);
    }

    /* Whole spinner rotates CCW (3600 → 0 = 360° CCW) over 6 s */
    lv_anim_t sa;
    lv_anim_init(&sa);
    lv_anim_set_exec_cb(&sa, spinner_rotate_anim_cb);
    lv_anim_set_var(&sa, spinner);
    lv_anim_set_values(&sa, 3600, 0);
    lv_anim_set_duration(&sa, 6000);
    lv_anim_set_repeat_count(&sa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&sa);

    /* Success label (starts transparent; fades in at 1 500 ms) */
    lv_obj_t *result_lbl = lv_label_create(overlay);
    g_p2_result = result_lbl;
    lv_label_set_text(result_lbl, "");
    lv_obj_set_style_text_color(result_lbl, C_PURE_WHITE, 0);
    lv_obj_set_style_text_font(result_lbl, &lv_font_inter_bold_32, 0);
    lv_obj_set_style_opa(result_lbl, LV_OPA_TRANSP, 0);
    lv_obj_align(result_lbl, LV_ALIGN_CENTER, 0, 60);

    /* Initiate the real wireless connection after overlay is visible. */
    if (s_ctx.node.is_bt) {
        ESP_LOGI(TAG, "BLE connect requested: %02X:%02X:%02X:%02X:%02X:%02X type=%u name='%s'",
                 s_ctx.node.mac[5], s_ctx.node.mac[4], s_ctx.node.mac[3],
                 s_ctx.node.mac[2], s_ctx.node.mac[1], s_ctx.node.mac[0],
                 s_ctx.node.ble_addr_type, s_ctx.node.name);
        s_waiting_ble_result = true;
        s_result_finalized   = false;
        wireless_register_status_cb(p3_status_cb);

        if (ble_connect(s_ctx.node.mac, s_ctx.node.ble_addr_type) != ESP_OK) {
            finalize_ble_connect(false);
            return;
        }

        /* Fail-safe timeout in case no status callback is received. */
        s_ble_timeout_timer = lv_timer_create(ble_timeout_cb, 12000, NULL);
    } else {
        esp_err_t rc = espnow_connect(s_ctx.node.mac);
        if (rc == ESP_OK) {
            lv_timer_create(result_show_timer_cb, 1500, NULL);
            lv_timer_create(success_timer_cb, 2500, NULL);
        } else {
            show_result_label("Connection Failed", lv_color_hex(0xFF6B6B));
            lv_timer_create(failure_close_timer_cb, 1200, NULL);
        }
    }
}
