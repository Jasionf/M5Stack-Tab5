/*
 * ui_common.h — Shared types, constants, image/font declarations,
 *               and cross-page extern/function declarations.
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "lvgl.h"
#include "wireless_mgr.h"   /* wireless_node_t, WIRELESS_MAX_NODES */
#include <stdbool.h>
#include <stdio.h>

/* ── Image assets ──────────────────────────────────────────────── */
extern const lv_image_dsc_t choose_gif_dsc;
LV_IMG_DECLARE(logo_espnow)
LV_IMG_DECLARE(bluetooth_logo)
LV_IMG_DECLARE(arrow_orange)
LV_IMG_DECLARE(arrow_blue)
LV_IMG_DECLARE(finder)
LV_IMG_DECLARE(node)

/* ── Font assets ───────────────────────────────────────────────── */
LV_FONT_DECLARE(lv_font_inter_bold_48)
LV_FONT_DECLARE(lv_font_inter_light_14)
LV_FONT_DECLARE(lv_font_inter_bold_32)
LV_FONT_DECLARE(lv_font_inter_regular_16)
LV_FONT_DECLARE(lv_font_inter_bold_13)
LV_FONT_DECLARE(lv_font_inter_regular_14)
LV_FONT_DECLARE(lv_font_inter_bold_14)
LV_FONT_DECLARE(lv_font_inter_bold_10)
LV_FONT_DECLARE(lv_font_inter_bold_24)
LV_FONT_DECLARE(lv_font_inter_regular_12)
LV_FONT_DECLARE(lv_font_inter_medium_10)
LV_FONT_DECLARE(lv_font_inter_regular_24)
LV_FONT_DECLARE(lv_font_inter_regular_20)
LV_FONT_DECLARE(lv_font_inter_regular_32)

/* ── Color palette ─────────────────────────────────────────────── */
#define C_ORANGE        lv_color_hex(0xFFB786)
#define C_WHITE         lv_color_hex(0xE5E2E1)
#define C_GRAY          lv_color_hex(0x9B877B)
#define C_CARD_BG       lv_color_hex(0x1C1B1B)
#define C_ICON_ESPNOW   lv_color_hex(0x332B26)
#define C_ICON_BT       lv_color_hex(0x1F2732)
#define C_BLUE          lv_color_hex(0x3D90FF)
#define C_BLACK         lv_color_hex(0x000000)
#define C_PURE_WHITE    lv_color_hex(0xFFFFFF)
#define C_PROGRESS_BG   lv_color_hex(0xD9D9D9)
#define C_NODE_BG       lv_color_hex(0x2A2A2A)
#define C_WARM          lv_color_hex(0xDEC1AF)
#define C_BADGE_BT_BG   lv_color_hex(0x3D90FF)
#define C_DARK_ORANGE   lv_color_hex(0x572800)
#define C_DEEP_ORANGE   lv_color_hex(0xF57C00)
#define C_CONNECTED     lv_color_hex(0x2E7D32)

/* ── Layout constants (landscape 1280×720) ──────────────────────── */
#define SCREEN_W        1280
#define SCREEN_H        720

/* Page 1 – card grid */
#define CARD_W          500
#define CARD_H          325
#define CARD_RADIUS     24
#define CARD_PAD_H      28
#define CARD_PAD_V      28
#define CARD_GAP        56
#define CARD_MARGIN     ((SCREEN_W - CARD_W * 2 - CARD_GAP) / 2)   /* = 112 */
#define CARD_Y          295
#define ICON_BG_SIZE    64
#define ICON_BG_RADIUS  16
#define CARD_LIFT_PX    24
#define CARD_ANIM_MS    280

/* Page 2 – scan screen */
#define P2_GLOW_X       93
#define P2_GLOW_Y       43
#define P2_GLOW_SIZE    69
#define P2_SCAN_X       185
#define P2_SCAN_Y       43
#define P2_PROG_X       185
#define P2_PROG_Y       99
#define P2_PROG_W       540
#define P2_PROG_CHUNK_W 98
#define P2_HEAD_X       105
#define P2_HEAD_Y       170
#define P2_PANEL_X      93
#define P2_PANEL_Y      218
#define P2_NODE_W       1070

/* ── Connect button context (per-node, passed as event user data) ── */
/*
 * Each "CONNECT" button on the scan-result list carries one of these.
 * It stores the MAC / address type needed to initiate the real
 * wireless connection when the button is tapped.
 */
typedef struct {
    wireless_node_t node;    /* full node info (name, MAC, type …) */
    lv_obj_t       *scr;    /* back-reference to the scan screen    */
} connect_btn_ctx_t;

/* ── Global UI state (defined in their owning .c files) ─────────── */
extern int        g_selected;           /* ui_page1.c */
extern lv_obj_t  *g_cards[2];           /* ui_page1.c */
extern lv_obj_t  *g_p2_overlay;         /* ui_page3.c */
extern lv_obj_t  *g_p2_spinner;         /* ui_page3.c */
extern lv_obj_t  *g_p2_result;          /* ui_page3.c */
extern lv_obj_t  *g_connect_btn;        /* ui_page3.c */
extern bool       g_p2_is_bt;           /* ui_page2.c — set in build_page2 */

/* ── Cross-page function declarations ─────────────────────── */
void example_lvgl_demo_ui(lv_obj_t *scr);  /* Page 1 — called from main.c    */
void build_page2(bool is_bt);               /* Page 2 — called from page 1    */
void connect_click_cb(lv_event_t *e);       /* Page 3 — called from page 2,
                                               user_data = connect_btn_ctx_t* */
void build_page4(bool is_bt);               /* Page 4 — (stub)               */
void build_page5(bool is_bt);               /* Page 5 — terminal, post-connect */
