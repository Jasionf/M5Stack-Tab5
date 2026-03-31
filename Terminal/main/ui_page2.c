/*
 * ui_page2.c — Scan screen: live wireless scan + discovered node list
 * Page 2 of 4
 *
 * - Starts a real ESP-NOW or BLE scan in the background.
 * - Each discovered peer is rendered as a node row with real RSSI,
 *   device name, protocol tag, and channel / appearance detail.
 * - Tapping CONNECT passes the real MAC address to ui_page3.c via
 *   a heap-allocated connect_btn_ctx_t (freed in page3 after use).
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_common.h"
#include "wireless_mgr.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

/* ── Page-2 global state ────────────────────────────────────────── */
bool g_p2_is_bt = false;   /* set in build_page2, read by page3 for navigation */

/* ── Module-private scan state ──────────────────────────────────── */
static lv_obj_t   *s_panel    = NULL;   /* scrollable node list           */
static lv_color_t  s_accent;            /* blue (BLE) or deep-orange (EN) */
static bool        s_page_is_bt;        /* mirror of g_p2_is_bt           */
static lv_obj_t   *s_scr2     = NULL;   /* screen object for btn ctx      */

/* Scan state widgets (need to show/hide when scan stops) */
static lv_obj_t   *s_scan_row     = NULL;   /* "SCANNING…" text + dots     */
static lv_obj_t   *s_prog_bg      = NULL;   /* flying-chunk progress bar   */
static lv_obj_t   *s_heading_lbl  = NULL;   /* "DISCOVERED BLE DEVICES"    */
static lv_obj_t   *s_refresh_btn  = NULL;   /* Refresh button (hideen initially) */

/* "empty state" label shown while no nodes discovered yet */
static lv_obj_t   *s_empty_lbl = NULL;
static lv_timer_t *s_refresh_tmr = NULL;
static bool        s_refresh_dirty = false;

/* Forward declaration because add_bt_direct_connect_item() uses it. */
static void make_node_item(lv_obj_t *parent, const wireless_node_t *n,
                           lv_color_t accent, bool page_is_bt, lv_obj_t *scr2);

/* Test fallback: fixed CardKB2 MAC for direct-connect debugging.
 * Display order is big-endian 88:56:A6:C3:02:56, NimBLE uses little-endian bytes. */
static const uint8_t s_kb2_direct_mac[6] = {0x56, 0x02, 0xC3, 0xA6, 0x56, 0x88};

static void add_bt_direct_connect_item(void)
{
    if (!s_panel || !lv_obj_is_valid(s_panel)) return;

    wireless_node_t n = {0};
    n.is_bt         = true;
    n.valid         = true;
    n.ble_addr_type = 0; /* BLE_ADDR_PUBLIC */
    memcpy(n.mac, s_kb2_direct_mac, sizeof(s_kb2_direct_mac));
    snprintf(n.name, sizeof(n.name), "CardKB2 (Direct MAC)");
    snprintf(n.type_tag, sizeof(n.type_tag), "BLE HID");
    snprintf(n.detail, sizeof(n.detail), "Direct connect");
    snprintf(n.signal, sizeof(n.signal), "manual");

    make_node_item(s_panel, &n, s_accent, true, s_scr2);
}

/* ── Animation callbacks ─────────────────────────────────────────── */
static void dot_opa_anim_cb(void *obj, int32_t val)
{
    /* val 0→100; last 6% snaps to dim — mimics svg-spinners 3-dots-fade */
    lv_obj_set_style_opa((lv_obj_t *)obj,
                         (val >= 94) ? LV_OPA_20 : LV_OPA_COVER, 0);
}

static void prog_anim_cb(void *obj, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)obj, x);
}

/* ── Node row builder (real wireless_node_t) ─────────────────────── */
static void make_node_item(lv_obj_t *parent, const wireless_node_t *n,
                           lv_color_t accent, bool page_is_bt, lv_obj_t *scr2)
{
    /* Row card */
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, P2_NODE_W, 102);
    lv_obj_set_style_bg_color(row, C_CARD_BG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 0, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Left flex row: icon + text column */
    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 23, 0);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 27, 0);

    /* Icon bg 66×66 */
    lv_obj_t *icn_bg = lv_obj_create(left);
    lv_obj_set_size(icn_bg, 66, 66);
    lv_obj_set_style_bg_color(icn_bg, C_NODE_BG, 0);
    lv_obj_set_style_bg_opa(icn_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(icn_bg, 8, 0);
    lv_obj_set_style_border_width(icn_bg, 0, 0);
    lv_obj_set_style_pad_all(icn_bg, 0, 0);
    lv_obj_clear_flag(icn_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *node_img = lv_img_create(icn_bg);
    lv_img_set_src(node_img, &node);
    lv_img_set_zoom(node_img, 244);
    lv_obj_set_style_blend_mode(node_img, LV_BLEND_MODE_ADDITIVE, 0);
    lv_obj_center(node_img);

    /* Text column */
    lv_obj_t *txt_col = lv_obj_create(left);
    lv_obj_remove_style_all(txt_col);
    lv_obj_set_size(txt_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(txt_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(txt_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(txt_col, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(txt_col, 3, 0);

    /* Name + badge */
    lv_obj_t *name_row = lv_obj_create(txt_col);
    lv_obj_remove_style_all(name_row);
    lv_obj_set_size(name_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(name_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(name_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(name_row, 8, 0);

    lv_obj_t *lbl_name = lv_label_create(name_row);
    lv_label_set_text(lbl_name, n->name);
    lv_obj_set_style_text_color(lbl_name, C_WHITE, 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_inter_bold_24, 0);

    lv_obj_t *badge = lv_obj_create(name_row);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(badge, 4, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_hor(badge, 8, 0);
    lv_obj_set_style_pad_ver(badge, 2, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    if (n->is_bt) {
        lv_obj_set_style_bg_color(badge, C_BADGE_BT_BG, 0);
        lv_obj_set_style_bg_opa(badge, 26, 0);
    } else {
        lv_obj_set_style_bg_color(badge, C_WARM, 0);
        lv_obj_set_style_bg_opa(badge, 51, 0);
    }
    lv_obj_t *lbl_tag = lv_label_create(badge);
    lv_label_set_text(lbl_tag, n->type_tag);
    lv_obj_set_style_text_color(lbl_tag, n->is_bt ? C_BLUE : C_WARM, 0);
    lv_obj_set_style_text_font(lbl_tag, &lv_font_inter_bold_10, 0);

    /* Signal + detail (channel for ESP-NOW, appearance/UUID for BLE) */
    lv_obj_t *meta_row = lv_obj_create(txt_col);
    lv_obj_remove_style_all(meta_row);
    lv_obj_set_size(meta_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(meta_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(meta_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(meta_row, 12, 0);

    lv_obj_t *lbl_sig = lv_label_create(meta_row);
    lv_label_set_text(lbl_sig, n->signal);
    lv_obj_set_style_text_color(lbl_sig, C_WARM, 0);
    lv_obj_set_style_text_font(lbl_sig, &lv_font_inter_regular_14, 0);

    if (n->detail[0] != '\0') {
        lv_obj_t *lbl_detail = lv_label_create(meta_row);
        lv_label_set_text(lbl_detail, n->detail);
        lv_obj_set_style_text_color(lbl_detail, C_WARM, 0);
        lv_obj_set_style_text_font(lbl_detail, &lv_font_inter_regular_14, 0);
    }

    /* MAC address in small text */
    lv_obj_t *lbl_mac = lv_label_create(txt_col);
    char mac_str[20];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             n->mac[5], n->mac[4], n->mac[3],
             n->mac[2], n->mac[1], n->mac[0]);
    lv_label_set_text(lbl_mac, mac_str);
    lv_obj_set_style_text_color(lbl_mac, C_GRAY, 0);
    lv_obj_set_style_text_font(lbl_mac, &lv_font_inter_regular_12, 0);
    lv_obj_set_style_opa(lbl_mac, LV_OPA_70, 0);

    /* ── CONNECT button (right-aligned) ─────────────────────────── */
    lv_obj_t *btn = lv_obj_create(row);
    lv_obj_set_size(btn, 108, 51);
    lv_obj_set_style_bg_color(btn, accent, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 16, 0);
    lv_obj_set_style_pad_ver(btn, 8, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -23, 0);

    /* Allocate a context struct so page3 gets the real node info */
    connect_btn_ctx_t *ctx = malloc(sizeof(connect_btn_ctx_t));
    if (ctx) {
        ctx->node = *n;
        ctx->scr  = scr2;
        lv_obj_add_event_cb(btn, connect_click_cb, LV_EVENT_CLICKED, ctx);
    }

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "CONNECT");
    lv_obj_set_style_text_color(btn_lbl, page_is_bt ? C_PURE_WHITE : C_DARK_ORANGE, 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_inter_bold_13, 0);
    lv_obj_set_style_text_letter_space(btn_lbl, 1, 0);
    lv_obj_center(btn_lbl);
}

/* ── Refresh button: clear results and restart 5s scan ──────────── */
static void refresh_node_list(void *arg);   /* forward declaration */
static void on_scan_result(void);           /* forward declaration */

static void refresh_timer_cb(lv_timer_t *t)
{
    (void)t;

    if (!s_refresh_dirty) {
        bool done = s_page_is_bt ? !ble_scan_is_active() : false;
        if (done && s_refresh_tmr) {
            lv_timer_delete(s_refresh_tmr);
            s_refresh_tmr = NULL;
        }
        return;
    }

    s_refresh_dirty = false;
    refresh_node_list(NULL);

    if (s_page_is_bt && !ble_scan_is_active() && !s_refresh_dirty && s_refresh_tmr) {
        lv_timer_delete(s_refresh_tmr);
        s_refresh_tmr = NULL;
    }
}

static void refresh_btn_click_cb(lv_event_t *e)
{
    (void)e;
    /* Show scan row / progress bar, hide Refresh button */
    if (s_refresh_btn && lv_obj_is_valid(s_refresh_btn))
        lv_obj_add_flag(s_refresh_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_scan_row && lv_obj_is_valid(s_scan_row))
        lv_obj_clear_flag(s_scan_row, LV_OBJ_FLAG_HIDDEN);
    if (s_prog_bg && lv_obj_is_valid(s_prog_bg))
        lv_obj_clear_flag(s_prog_bg, LV_OBJ_FLAG_HIDDEN);
    if (s_heading_lbl && lv_obj_is_valid(s_heading_lbl))
        lv_label_set_text(s_heading_lbl,
                          s_page_is_bt ? "DISCOVERED BLE DEVICES"
                                       : "DISCOVERED ESP-NOW NODES");

    /* Clear old results and start a fresh 5s scan */
    wireless_clear_results();
    lv_async_call(refresh_node_list, NULL);   /* shows "Scanning…" label */

    if (s_page_is_bt) {
        ble_scan_start(on_scan_result);
    } else {
        espnow_scan_start(on_scan_result);
    }
}

/* ── lv_async_call target: rebuild node list from wireless results ─ */
static void refresh_node_list(void *arg)
{
    (void)arg;
    if (!s_panel || !lv_obj_is_valid(s_panel)) return;

    wireless_node_t nodes[WIRELESS_MAX_NODES];
    int n = wireless_get_results(nodes, WIRELESS_MAX_NODES);
    
    ESP_LOGI("ui/page2", "refresh_node_list: got %d nodes, is_bt=%d", n, s_page_is_bt);

    /* When scan finishes: hide scanning animation, show Refresh button */
    bool still_scanning = s_page_is_bt ? ble_scan_is_active() : espnow_scan_is_active();
    if (!still_scanning) {
        if (s_scan_row && lv_obj_is_valid(s_scan_row))
            lv_obj_add_flag(s_scan_row, LV_OBJ_FLAG_HIDDEN);
        if (s_prog_bg && lv_obj_is_valid(s_prog_bg))
            lv_obj_add_flag(s_prog_bg, LV_OBJ_FLAG_HIDDEN);
        if (s_heading_lbl && lv_obj_is_valid(s_heading_lbl))
            lv_label_set_text(s_heading_lbl,
                              s_page_is_bt ? "BLE DEVICES FOUND"
                                           : "ESP-NOW NODES FOUND");
        if (s_refresh_btn && lv_obj_is_valid(s_refresh_btn))
            lv_obj_clear_flag(s_refresh_btn, LV_OBJ_FLAG_HIDDEN);
    }

    /* Remove all existing children of the panel */
    lv_obj_clean(s_panel);

    /* BLE debug path: always provide one fixed-MAC direct connect row. */
    if (s_page_is_bt) {
        add_bt_direct_connect_item();
    }

    if (n == 0) {
        if (s_page_is_bt) {
            s_empty_lbl = lv_label_create(s_panel);
            lv_label_set_text(s_empty_lbl,
                              still_scanning
                              ? "Scanning BLE devices... You can test with Direct MAC meanwhile."
                              : "No BLE devices found. You can still test with Direct MAC.");
            lv_obj_set_style_text_color(s_empty_lbl, C_WARM, 0);
            lv_obj_set_style_text_font(s_empty_lbl, &lv_font_inter_regular_16, 0);
            lv_obj_set_style_opa(s_empty_lbl, LV_OPA_70, 0);
        } else {
            /* Restore "waiting..." / "no devices" label */
            s_empty_lbl = lv_label_create(s_panel);
            lv_label_set_text(s_empty_lbl,
                              still_scanning
                              ? "Broadcasting WHO_IS_HERE beacon..."
                              : "No devices found. Tap Refresh to scan again.");
            lv_obj_set_style_text_color(s_empty_lbl, C_WARM, 0);
            lv_obj_set_style_text_font(s_empty_lbl, &lv_font_inter_regular_16, 0);
            lv_obj_set_style_opa(s_empty_lbl, LV_OPA_70, 0);
        }
        return;
    }

    s_empty_lbl = NULL;

    /* Render all nodes matching the current scan type */
    int shown = 0;
    for (int i = 0; i < n; i++) {
        ESP_LOGI("ui/page2", "Node[%d]: name='%s' is_bt=%d", i, nodes[i].name, nodes[i].is_bt);
        if (nodes[i].is_bt == s_page_is_bt) {
            make_node_item(s_panel, &nodes[i], s_accent,
                           s_page_is_bt, s_scr2);
            shown++;
        }
    }
    
    ESP_LOGI("ui/page2", "Shown %d nodes", shown);

    if (shown == 0) {
        if (s_page_is_bt) {
            s_empty_lbl = lv_label_create(s_panel);
            lv_label_set_text(s_empty_lbl,
                              "No scanned BLE devices found yet. You can use Direct MAC above.");
            lv_obj_set_style_text_color(s_empty_lbl, C_WARM, 0);
            lv_obj_set_style_text_font(s_empty_lbl, &lv_font_inter_regular_16, 0);
            lv_obj_set_style_opa(s_empty_lbl, LV_OPA_70, 0);
        } else {
            s_empty_lbl = lv_label_create(s_panel);
            lv_label_set_text(s_empty_lbl,
                              still_scanning
                              ? "Broadcasting WHO_IS_HERE beacon..."
                              : "No matching devices found. Tap Refresh to scan again.");
            lv_obj_set_style_text_color(s_empty_lbl, C_WARM, 0);
            lv_obj_set_style_text_font(s_empty_lbl, &lv_font_inter_regular_16, 0);
            lv_obj_set_style_opa(s_empty_lbl, LV_OPA_70, 0);
        }
    }
}

/* ── Wireless scan result callback (called from WiFi/BLE task) ───── */
static void on_scan_result(void)
{
    /* Must not touch LVGL from non-LVGL tasks.
     * Mark dirty and let a short-period LVGL timer coalesce updates. */
    s_refresh_dirty = true;
    if (!s_refresh_tmr) {
        s_refresh_tmr = lv_timer_create(refresh_timer_cb, 150, NULL);
    }
}

/* ── Page 2 builder (called from ui_page1.c) ─────────────────────── */
void build_page2(bool is_bt)
{
    g_p2_is_bt    = is_bt;   /* expose to page3 for post-connect navigation */
    s_page_is_bt  = is_bt;
    s_accent      = is_bt ? C_BLUE : C_DEEP_ORANGE;
    lv_color_t accent = s_accent;

    /* Reset widget pointers from any previous page2 instance */
    s_scan_row    = NULL;
    s_prog_bg     = NULL;
    s_heading_lbl = NULL;
    s_refresh_btn = NULL;
    s_empty_lbl   = NULL;
    if (s_refresh_tmr) {
        lv_timer_delete(s_refresh_tmr);
        s_refresh_tmr = NULL;
    }
    s_refresh_dirty = false;

    /* Clear stale results from a previous scan session */
    wireless_clear_results();

    lv_obj_t *scr2 = lv_obj_create(NULL);
    s_scr2 = scr2;
    lv_obj_clear_flag(scr2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr2, C_BLACK, 0);
    lv_obj_set_style_bg_opa(scr2, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr2, 0, 0);

    /* Glow circle */
    lv_obj_t *glow = lv_obj_create(scr2);
    lv_obj_set_size(glow, P2_GLOW_SIZE, P2_GLOW_SIZE);
    lv_obj_set_pos(glow, P2_GLOW_X, P2_GLOW_Y);
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(glow, accent, 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_50, 0);
    lv_obj_set_style_border_width(glow, 0, 0);
    lv_obj_set_style_pad_all(glow, 0, 0);
    lv_obj_clear_flag(glow, LV_OBJ_FLAG_SCROLLABLE);

    /* Finder icon centered in glow */
    lv_obj_t *find_icon = lv_img_create(glow);
    lv_img_set_src(find_icon, &finder);
    lv_img_set_zoom(find_icon, 37);
    lv_obj_set_style_blend_mode(find_icon, LV_BLEND_MODE_ADDITIVE, 0);
    lv_obj_center(find_icon);

    /* Scan text + 3-dot fade animation */
    lv_obj_t *scan_row = lv_obj_create(scr2);
    s_scan_row = scan_row;
    lv_obj_remove_style_all(scan_row);
    lv_obj_set_pos(scan_row, P2_SCAN_X, P2_SCAN_Y);
    lv_obj_set_size(scan_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(scan_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(scan_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scan_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scan_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(scan_row, 6, 0);

    lv_obj_t *lbl_scan = lv_label_create(scan_row);
    lv_label_set_text(lbl_scan, is_bt ? "SCANNING FOR NEARBY BLE DEVICES"
                                      : "SCANNING FOR NEARBY ESP-NOW NODES");
    lv_obj_set_style_text_color(lbl_scan, C_PURE_WHITE, 0);
    lv_obj_set_style_text_font(lbl_scan, &lv_font_inter_regular_32, 0);
    lv_obj_set_style_text_letter_space(lbl_scan, 1, 0);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *fdot = lv_obj_create(scan_row);
        lv_obj_set_size(fdot, 8, 8);
        lv_obj_set_style_radius(fdot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(fdot, C_PURE_WHITE, 0);
        lv_obj_set_style_bg_opa(fdot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(fdot, 0, 0);
        lv_obj_set_style_pad_all(fdot, 0, 0);

        lv_anim_t da;
        lv_anim_init(&da);
        lv_anim_set_exec_cb(&da, dot_opa_anim_cb);
        lv_anim_set_var(&da, fdot);
        lv_anim_set_values(&da, 0, 100);
        lv_anim_set_duration(&da, 800);
        lv_anim_set_delay(&da, (uint32_t)(i * 150));
        lv_anim_set_repeat_count(&da, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&da);
    }

    /* Flying-chunk progress bar */
    lv_obj_t *prog_bg = lv_obj_create(scr2);
    s_prog_bg = prog_bg;
    lv_obj_remove_style_all(prog_bg);
    lv_obj_set_size(prog_bg, P2_PROG_W, 8);
    lv_obj_set_pos(prog_bg, P2_PROG_X, P2_PROG_Y);
    lv_obj_set_style_bg_color(prog_bg, C_PROGRESS_BG, 0);
    lv_obj_set_style_bg_opa(prog_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(prog_bg, 50, 0);
    lv_obj_set_style_clip_corner(prog_bg, true, 0); /* clip children to pill shape */

    lv_obj_t *prog_fill = lv_obj_create(prog_bg);
    lv_obj_remove_style_all(prog_fill);
    lv_obj_set_size(prog_fill, P2_PROG_CHUNK_W, 8);
    lv_obj_set_pos(prog_fill, -P2_PROG_CHUNK_W, 0);
    lv_obj_set_style_bg_color(prog_fill, accent, 0);
    lv_obj_set_style_bg_opa(prog_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(prog_fill, 50, 0);

    lv_anim_t pa;
    lv_anim_init(&pa);
    lv_anim_set_exec_cb(&pa, prog_anim_cb);
    lv_anim_set_var(&pa, prog_fill);
    lv_anim_set_values(&pa, -P2_PROG_CHUNK_W, P2_PROG_W);
    lv_anim_set_duration(&pa, 1800);
    lv_anim_set_path_cb(&pa, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&pa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&pa);

    /* Section heading with live node count */
    lv_obj_t *lbl_heading = lv_label_create(scr2);
    s_heading_lbl = lbl_heading;
    lv_label_set_text(lbl_heading, is_bt
                      ? "DISCOVERED BLE DEVICES"
                      : "DISCOVERED ESP-NOW NODES");
    lv_obj_set_style_text_color(lbl_heading, C_WARM, 0);
    lv_obj_set_style_text_font(lbl_heading, &lv_font_inter_regular_20, 0);
    lv_obj_set_style_text_letter_space(lbl_heading, 1, 0);
    lv_obj_set_style_opa(lbl_heading, LV_OPA_70, 0);
    lv_obj_set_pos(lbl_heading, P2_HEAD_X, P2_HEAD_Y);

    /* Refresh button — hidden initially, shown after scan completes */
    lv_obj_t *rbtn = lv_obj_create(scr2);
    s_refresh_btn = rbtn;
    lv_obj_set_size(rbtn, 130, 36);
    lv_obj_set_pos(rbtn, SCREEN_W - 130 - 24, P2_HEAD_Y - 2);
    lv_obj_set_style_bg_color(rbtn, accent, 0);
    lv_obj_set_style_bg_opa(rbtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rbtn, 8, 0);
    lv_obj_set_style_border_width(rbtn, 0, 0);
    lv_obj_set_style_pad_hor(rbtn, 14, 0);
    lv_obj_set_style_pad_ver(rbtn, 6, 0);
    lv_obj_clear_flag(rbtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(rbtn, LV_OBJ_FLAG_HIDDEN);   /* hidden until scan stops */
    lv_obj_add_event_cb(rbtn, refresh_btn_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rbtn_lbl = lv_label_create(rbtn);
    lv_label_set_text(rbtn_lbl, LV_SYMBOL_REFRESH "  Refresh");
    lv_obj_set_style_text_color(rbtn_lbl, C_PURE_WHITE, 0);
    lv_obj_set_style_text_font(rbtn_lbl, &lv_font_inter_bold_13, 0);
    lv_obj_center(rbtn_lbl);

    /* Scrollable device list */
    lv_obj_t *panel = lv_obj_create(scr2);
    s_panel = panel;
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, P2_NODE_W, SCREEN_H - P2_PANEL_Y);
    lv_obj_set_pos(panel, P2_PANEL_X, P2_PANEL_Y);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_pad_row(panel, 16, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    if (is_bt) {
        /* Always show direct-connect entry immediately for debug testing. */
        add_bt_direct_connect_item();
    }

    /* Initial "waiting" label */
    s_empty_lbl = lv_label_create(panel);
    lv_label_set_text(s_empty_lbl, is_bt
                      ? "Scanning for BLE devices... You can test with Direct MAC above."
                      : "Broadcasting WHO_IS_HERE beacon...");
    lv_obj_set_style_text_color(s_empty_lbl, C_WARM, 0);
    lv_obj_set_style_text_font(s_empty_lbl, &lv_font_inter_regular_16, 0);
    lv_obj_set_style_opa(s_empty_lbl, LV_OPA_70, 0);

    /* Load screen first, then start the actual wireless scan */
    lv_screen_load_anim(scr2, LV_SCREEN_LOAD_ANIM_FADE_IN, 300, 0, false);

    /* Start real wireless scan */
    if (is_bt) {
        ble_scan_start(on_scan_result);
    } else {
        espnow_scan_start(on_scan_result);
    }
}
