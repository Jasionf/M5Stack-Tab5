/*
 * ui_page4.c — Terminal / post-connection screen
 * Page 4 of 4
 *
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ui_common.h"

/* ── Page 4 builder (called after successful connection) ────────── */
void build_page4(bool is_bt)
{
    lv_obj_t *scr4 = lv_obj_create(NULL);
    lv_obj_clear_flag(scr4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr4, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(scr4, C_BLACK, 0);
    lv_obj_set_style_bg_opa(scr4, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr4, 0, 0);

    /* Status badge */
    lv_obj_t *badge = lv_obj_create(scr4);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(badge, C_CONNECTED, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_hor(badge, 16, 0);
    lv_obj_set_style_pad_ver(badge, 6, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 48, 40);

    lv_obj_t *lbl_status = lv_label_create(badge);
    lv_label_set_text(lbl_status, is_bt ? "BT HID  CONNECTED" : "ESP-NOW  CONNECTED");
    lv_obj_set_style_text_color(lbl_status, C_PURE_WHITE, 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_inter_bold_13, 0);

    /* Page title */
    lv_obj_t *lbl_title = lv_label_create(scr4);
    lv_label_set_text(lbl_title, "TERMINAL");
    lv_obj_set_style_text_color(lbl_title, C_WHITE, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_inter_bold_48, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 80);

    /* Placeholder terminal output area */
    lv_obj_t *term_bg = lv_obj_create(scr4);
    lv_obj_set_size(term_bg, SCREEN_W - 96, SCREEN_H - 200);
    lv_obj_set_pos(term_bg, 48, 160);
    lv_obj_set_style_bg_color(term_bg, C_CARD_BG, 0);
    lv_obj_set_style_bg_opa(term_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(term_bg, 12, 0);
    lv_obj_set_style_border_width(term_bg, 0, 0);
    lv_obj_set_style_pad_all(term_bg, 20, 0);
    lv_obj_set_scrollbar_mode(term_bg, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *lbl_prompt = lv_label_create(term_bg);
    lv_label_set_text(lbl_prompt, "> Ready.");
    lv_obj_set_style_text_color(lbl_prompt, C_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_prompt, &lv_font_inter_regular_16, 0);

    lv_screen_load_anim(scr4, LV_SCREEN_LOAD_ANIM_FADE_IN, 300, 0, false);
}
