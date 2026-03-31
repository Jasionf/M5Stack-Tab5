/*
 * ui_page1.c — Home screen: ESP-NOW / BT connection method selection
 * Page 1 of 4
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_common.h"

/* ── Page-1 global state ─────────────────────────────────────────── */
int        g_selected  = -1;
lv_obj_t  *g_cards[2]  = {NULL, NULL};

/* ── Animation helpers ───────────────────────────────────────────── */
static void card_y_anim_cb(void *obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void card_set_lifted(int idx, bool lifted)
{
    lv_obj_t  *card  = g_cards[idx];
    bool        is_bt = (idx == 1);
    lv_color_t  glow  = is_bt ? C_BLUE : C_ORANGE;

    int32_t from_y = lv_obj_get_y(card);
    int32_t to_y   = lifted ? CARD_Y - CARD_LIFT_PX : CARD_Y;
    if (from_y != to_y) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, card_y_anim_cb);
        lv_anim_set_var(&a, card);
        lv_anim_set_values(&a, from_y, to_y);
        lv_anim_set_duration(&a, CARD_ANIM_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }

    /* Soft outer glow via shadow */
    if (lifted) {
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_shadow_color(card, glow, 0);
        lv_obj_set_style_shadow_width(card, 24, 0);
        lv_obj_set_style_shadow_spread(card, 4, 0);
        lv_obj_set_style_shadow_ofs_x(card, 0, 0);
        lv_obj_set_style_shadow_ofs_y(card, 0, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_70, 0);
    } else {
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
    }
}

static void card_click_cb(lv_event_t *e)
{
    lv_obj_t *card = (lv_obj_t *)lv_event_get_user_data(e);
    int idx = -1;
    if      (card == g_cards[0]) idx = 0;  // UART card
    else if (card == g_cards[1]) idx = 1;  // BLE card
    if (idx < 0) return;

    if (g_selected == idx) {
        /* Second tap on same card */
        if (idx == 0) {
            /* UART: directly enter terminal page */
            build_page5(false);  // false = not BT, it's UART
        } else {
            /* BLE: enter scan screen */
            build_page2(true);
        }
    } else {
        if (g_selected >= 0) card_set_lifted(g_selected, false);
        g_selected = idx;
        card_set_lifted(idx, true);
    }
}

/* ── Card widget builder ─────────────────────────────────────────── */
static void make_card(lv_obj_t *parent, bool is_bt, int32_t x)
{
    const lv_color_t  btn_color   = is_bt ? C_BLUE     : C_ORANGE;
    const lv_color_t  icon_bg_col = is_bt ? C_ICON_BT  : C_ICON_ESPNOW;
    const void       *icon_src    = is_bt ? (const void *)&bluetooth_logo
                                          : (const void *)&logo_espnow;
    const void       *arrow_src   = is_bt ? (const void *)&arrow_blue
                                          : (const void *)&arrow_orange;
    int               card_idx    = is_bt ? 1 : 0;

    lv_obj_t *card = lv_obj_create(parent);
    g_cards[card_idx] = card;
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_pos(card, x, CARD_Y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(card, C_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_hor(card, CARD_PAD_H, 0);
    lv_obj_set_style_pad_top(card, CARD_PAD_V, 0);
    lv_obj_set_style_pad_bottom(card, CARD_PAD_V, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, (void *)card);

    /* Icon background */
    lv_obj_t *icon_bg = lv_obj_create(card);
    lv_obj_set_size(icon_bg, ICON_BG_SIZE, ICON_BG_SIZE);
    lv_obj_set_style_radius(icon_bg, ICON_BG_RADIUS, 0);
    lv_obj_set_style_bg_color(icon_bg, icon_bg_col, 0);
    lv_obj_set_style_bg_opa(icon_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(icon_bg, 0, 0);
    lv_obj_set_style_pad_all(icon_bg, 0, 0);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon_bg, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(icon_bg, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *icon = lv_img_create(icon_bg);
    lv_img_set_src(icon, icon_src);
    lv_obj_set_style_blend_mode(icon, LV_BLEND_MODE_ADDITIVE, 0);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(icon);

    /* Title */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, is_bt ? "BLUETOOTH HID" : "UART");
    lv_obj_set_style_text_color(title, C_PURE_WHITE, 0);
    lv_obj_set_style_text_font(title, &lv_font_inter_bold_32, 0);
    lv_obj_add_flag(title, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, ICON_BG_SIZE + 16);

    /* Description */
    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, is_bt
        ? "Standard peripheral interface for\nseamless integration with\nworkstation environments."
        : "High-speed, low-latency serial link\nfor industrial\nhardware nodes.");
    lv_obj_set_style_text_color(desc, C_GRAY, 0);
    lv_obj_set_style_text_font(desc, &lv_font_inter_regular_16, 0);
    lv_obj_set_style_text_line_space(desc, 5, 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(desc, CARD_W - CARD_PAD_H * 2);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(desc, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, ICON_BG_SIZE + 16 + 44);

    /* "SCAN DEVICE(S)" button */
    lv_obj_t *btn = lv_obj_create(card);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_radius(btn, 20, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(btn, btn_color, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_pad_hor(btn, 18, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 6, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, is_bt ? "SCAN DEVICES" : "CONNECT DEVICE");
    lv_obj_set_style_text_color(btn_lbl, btn_color, 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_inter_bold_13, 0);
    lv_obj_add_flag(btn_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *btn_arr = lv_img_create(btn);
    lv_img_set_src(btn_arr, arrow_src);
    lv_img_set_zoom(btn_arr, 512);
    lv_obj_add_flag(btn_arr, LV_OBJ_FLAG_EVENT_BUBBLE);
}

/* ── Page 1 entry point (called from main.c) ─────────────────────── */
void example_lvgl_demo_ui(lv_obj_t *scr)
{
    /* Physical display 720×1280 portrait → rotate 90° → logical 1280×720 landscape */
    lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_90);

    g_selected  = -1;
    g_cards[0]  = NULL;
    g_cards[1]  = NULL;

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr, C_BLACK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Title row 1: "TAB5" (orange) + " SMART" (white) */
    lv_obj_t *row1 = lv_obj_create(scr);
    lv_obj_remove_style_all(row1);
    lv_obj_set_size(row1, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_set_style_pad_column(row1, 0, 0);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row1, LV_ALIGN_TOP_MID, 0, 72);

    lv_obj_t *lbl_tab5 = lv_label_create(row1);
    lv_label_set_text(lbl_tab5, "TAB5");
    lv_obj_set_style_text_color(lbl_tab5, C_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_tab5, &lv_font_inter_bold_48, 0);

    lv_obj_t *lbl_smart = lv_label_create(row1);
    lv_label_set_text(lbl_smart, " SMART");
    lv_obj_set_style_text_color(lbl_smart, C_WHITE, 0);
    lv_obj_set_style_text_font(lbl_smart, &lv_font_inter_bold_48, 0);

    /* Title row 2: "TERMINAL" */
    lv_obj_t *lbl_term = lv_label_create(scr);
    lv_label_set_text(lbl_term, "TERMINAL");
    lv_obj_set_style_text_color(lbl_term, C_WHITE, 0);
    lv_obj_set_style_text_font(lbl_term, &lv_font_inter_bold_48, 0);
    lv_obj_align(lbl_term, LV_ALIGN_TOP_MID, 0, 132);

    /* Subtitle */
    lv_obj_t *lbl_sub = lv_label_create(scr);
    lv_label_set_text(lbl_sub, "SELECT YOUR CONNECTION METHOD");
    lv_obj_set_style_text_color(lbl_sub, C_GRAY, 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_inter_light_14, 0);
    lv_obj_set_style_text_letter_space(lbl_sub, 3, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 200);

    /* Tap-hint GIF */
    lv_obj_t *gif = lv_gif_create(scr);
    lv_gif_set_src(gif, &choose_gif_dsc);
    lv_obj_set_size(gif, 76, 76);
    lv_obj_align(gif, LV_ALIGN_TOP_MID, 0, 226);

    /* Two protocol cards */
    make_card(scr, false, CARD_MARGIN);
    make_card(scr, true,  CARD_MARGIN + CARD_W + CARD_GAP);
}
