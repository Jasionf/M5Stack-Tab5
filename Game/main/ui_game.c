/**
 * @file ui_game.c
 * @brief Modern Game Screen for NEON TETRIS
 */

#include "ui_game.h"
#include "lvgl.h"
#include "tetris_engine.h"
#include "esp_heap_caps.h"
#include <stdio.h>

/* Modern Color Palette */
#define C_BG            lv_color_hex(0x0A0A0F)
#define C_PRIMARY       lv_color_hex(0xA855F7)
#define C_ACCENT        lv_color_hex(0xEC4899)
#define C_TEXT          lv_color_hex(0xF5F5F7)
#define C_TEXT_DIM      lv_color_hex(0x9CA3AF)
#define C_CARD_BG       lv_color_hex(0x1A1A24)
#define C_BOARD_BG      lv_color_hex(0x14121C)
#define C_BORDER        lv_color_hex(0x2A2A3A)

static void obj_clear_style(lv_obj_t *o)
{
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}


void ui_game_init(ui_game_t *ui, lv_obj_t *scr)
{
    if (ui == NULL || scr == NULL) {
        return;
    }
    
    ui->root = scr;
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    
    /* Ambient glows */
    lv_obj_t *glow1 = lv_obj_create(scr);
    lv_obj_set_size(glow1, 400, 400);
    lv_obj_set_pos(glow1, 100, -50);
    lv_obj_set_style_bg_color(glow1, C_PRIMARY, 0);
    lv_obj_set_style_bg_opa(glow1, LV_OPA_10, 0);
    lv_obj_set_style_radius(glow1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow1, 0, 0);
    lv_obj_clear_flag(glow1, LV_OBJ_FLAG_SCROLLABLE);
    
    /* Game board container */
    lv_obj_t *board = lv_obj_create(scr);
    lv_obj_set_size(board, 344, 668);
    lv_obj_align(board, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(board, C_BOARD_BG, 0);
    lv_obj_set_style_border_color(board, C_BORDER, 0);
    lv_obj_set_style_border_width(board, 3, 0);
    lv_obj_set_style_radius(board, 24, 0);
    lv_obj_set_style_shadow_width(board, 50, 0);
    lv_obj_set_style_shadow_color(board, C_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(board, LV_OPA_20, 0);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *canvas = lv_canvas_create(board);
    lv_obj_set_size(canvas, 320, 640);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
    ui->canvas = canvas;
    
    /* Left info panel */
    int left_x = 40;
    int info_y = 80;
    
    /* Info card */
    lv_obj_t *info_card = lv_obj_create(scr);
    lv_obj_set_size(info_card, 200, 440);
    lv_obj_set_pos(info_card, left_x, info_y);
    lv_obj_set_style_bg_color(info_card, C_CARD_BG, 0);
    lv_obj_set_style_border_color(info_card, C_BORDER, 0);
    lv_obj_set_style_border_width(info_card, 2, 0);
    lv_obj_set_style_radius(info_card, 20, 0);
    lv_obj_set_style_pad_all(info_card, 20, 0);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
    
    /* Score */
    lv_obj_t *score_title = lv_label_create(info_card);
    lv_label_set_text(score_title, "SCORE");
    lv_obj_set_style_text_font(score_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(score_title, C_TEXT_DIM, 0);
    lv_obj_set_pos(score_title, 0, 0);
    
    lv_obj_t *score_label = lv_label_create(info_card);
    lv_label_set_text(score_label, "0");
    lv_obj_set_style_text_font(score_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(score_label, C_PRIMARY, 0);
    lv_obj_set_pos(score_label, 0, 24);
    ui->score_label = score_label;
    
    /* Level */
    lv_obj_t *level_title = lv_label_create(info_card);
    lv_label_set_text(level_title, "LEVEL");
    lv_obj_set_style_text_font(level_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(level_title, C_TEXT_DIM, 0);
    lv_obj_set_pos(level_title, 0, 80);
    
    lv_obj_t *level_label = lv_label_create(info_card);
    lv_label_set_text(level_label, "0");
    lv_obj_set_style_text_font(level_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(level_label, C_ACCENT, 0);
    lv_obj_set_pos(level_label, 0, 104);
    ui->level_label = level_label;
    
    /* Lines */
    lv_obj_t *lines_title = lv_label_create(info_card);
    lv_label_set_text(lines_title, "LINES");
    lv_obj_set_style_text_font(lines_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lines_title, C_TEXT_DIM, 0);
    lv_obj_set_pos(lines_title, 0, 160);
    
    lv_obj_t *lines_label = lv_label_create(info_card);
    lv_label_set_text(lines_label, "0");
    lv_obj_set_style_text_font(lines_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lines_label, C_TEXT, 0);
    lv_obj_set_pos(lines_label, 0, 184);
    ui->lines_label = lines_label;
    
    /* Next piece preview */
    lv_obj_t *next_title = lv_label_create(info_card);
    lv_label_set_text(next_title, "NEXT");
    lv_obj_set_style_text_font(next_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(next_title, C_TEXT_DIM, 0);
    lv_obj_set_pos(next_title, 0, 230);
    
    lv_obj_t *next_canvas = lv_canvas_create(info_card);
    lv_obj_set_size(next_canvas, 120, 120);
    lv_obj_set_pos(next_canvas, 20, 260);
    lv_obj_set_style_bg_color(next_canvas, C_BOARD_BG, 0);
    lv_obj_set_style_border_color(next_canvas, C_BORDER, 0);
    lv_obj_set_style_border_width(next_canvas, 2, 0);
    lv_obj_set_style_radius(next_canvas, 16, 0);
    ui->next_canvas = next_canvas;
    
    /* Allocate buffer for next canvas */
    size_t buf_size = 120 * 120 * sizeof(lv_color_t);
    ui->next_buffer = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (ui->next_buffer == NULL) {
        ui->next_buffer = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_8BIT);
    }
    if (ui->next_buffer) {
        lv_canvas_set_buffer(next_canvas, ui->next_buffer, 120, 120, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(next_canvas, C_BOARD_BG, LV_OPA_COVER);
    }
    
    // /* Control instructions — top-right corner, aligned with right panel */
    // lv_obj_t *inst_card = lv_obj_create(scr);
    // lv_obj_set_size(inst_card, 200, 340);
    // lv_obj_set_pos(inst_card, 1040, 120);
    // lv_obj_set_style_bg_color(inst_card, C_CARD_BG, 0);
    // lv_obj_set_style_border_color(inst_card, C_BORDER, 0);
    // lv_obj_set_style_border_width(inst_card, 2, 0);
    // lv_obj_set_style_radius(inst_card, 20, 0);
    // lv_obj_set_style_pad_all(inst_card, 20, 0);
    // lv_obj_clear_flag(inst_card, LV_OBJ_FLAG_SCROLLABLE);
    
    // lv_obj_t *inst_title = lv_label_create(inst_card);
    // lv_label_set_text(inst_title, "CONTROLS");
    // lv_obj_set_style_text_font(inst_title, &lv_font_montserrat_16, 0);
    // lv_obj_set_style_text_color(inst_title, C_PRIMARY, 0);
    // lv_obj_set_pos(inst_title, 0, 0);

    // /* key icon | description  — all using LVGL built-in symbols or ASCII */
    // static const char *ctrl_keys[] = {
    //     LV_SYMBOL_LEFT,    "Move Left",
    //     LV_SYMBOL_RIGHT,   "Move Right",
    //     LV_SYMBOL_DOWN,    "Soft Drop",
    //     LV_SYMBOL_REFRESH, "Rotate / B",
    //     LV_SYMBOL_UP,      "Hard Drop",
    //     LV_SYMBOL_PAUSE,   "Pause",
    //     LV_SYMBOL_HOME,    "Quit",
    // };

    // for (int i = 0; i < 7; i++) {
    //     int y = 28 + i * 32;

    //     lv_obj_t *key = lv_label_create(inst_card);
    //     lv_label_set_text(key, ctrl_keys[i * 2]);
    //     lv_obj_set_style_text_font(key, &lv_font_montserrat_16, 0);
    //     lv_obj_set_style_text_color(key, C_ACCENT, 0);
    //     lv_obj_set_pos(key, 0, y);

    //     lv_obj_t *desc = lv_label_create(inst_card);
    //     lv_label_set_text(desc, ctrl_keys[i * 2 + 1]);
    //     lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    //     lv_obj_set_style_text_color(desc, C_TEXT, 0);
    //     lv_obj_set_pos(desc, 36, y + 1);
    // }
/* Control instructions — 右侧面板上方 */
lv_obj_t *inst_card = lv_obj_create(scr);
lv_obj_set_size(inst_card, 200, 390);
lv_obj_set_pos(inst_card, 1040, 80);
lv_obj_set_style_bg_color(inst_card, C_CARD_BG, 0);
lv_obj_set_style_border_color(inst_card, C_BORDER, 0);
lv_obj_set_style_border_width(inst_card, 2, 0);
lv_obj_set_style_radius(inst_card, 20, 0);
lv_obj_set_style_pad_all(inst_card, 16, 0);
lv_obj_clear_flag(inst_card, LV_OBJ_FLAG_SCROLLABLE);

/* 标题 */
lv_obj_t *inst_title = lv_label_create(inst_card);
lv_label_set_text(inst_title, "CONTROLS");
lv_obj_set_style_text_font(inst_title, &lv_font_montserrat_14, 0);
lv_obj_set_style_text_color(inst_title, C_TEXT_DIM, 0);
lv_obj_set_pos(inst_title, 0, 0);

/* 分隔线 */
lv_obj_t *divider = lv_obj_create(inst_card);
lv_obj_set_size(divider, 168, 1);
lv_obj_set_pos(divider, 0, 22);
lv_obj_set_style_bg_color(divider, C_BORDER, 0);
lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
lv_obj_set_style_border_width(divider, 0, 0);
lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

/* 每行：键帽 badge + 描述文字 */
static const char *ctrl_keys[] = {
    LV_SYMBOL_LEFT,    "Move Left",
    LV_SYMBOL_RIGHT,   "Move Right",
    LV_SYMBOL_DOWN,    "Soft Drop",
    LV_SYMBOL_REFRESH, "Rotate",
    LV_SYMBOL_UP,      "Hard Drop",
    LV_SYMBOL_PAUSE,   "Pause",
    LV_SYMBOL_HOME,    "Quit",
};

for (int i = 0; i < 7; i++) {
    int row_y = 34 + i * 48;

    /* 整行背景（hover 感） */
    lv_obj_t *row_bg = lv_obj_create(inst_card);
    lv_obj_set_size(row_bg, 168, 38);
    lv_obj_set_pos(row_bg, 0, row_y);
    lv_obj_set_style_bg_color(row_bg, C_BG, 0);
    lv_obj_set_style_bg_opa(row_bg, LV_OPA_40, 0);
    lv_obj_set_style_border_width(row_bg, 0, 0);
    lv_obj_set_style_radius(row_bg, 10, 0);
    lv_obj_clear_flag(row_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* 键帽容器 */
    lv_obj_t *badge = lv_obj_create(inst_card);
    lv_obj_set_size(badge, 36, 26);
    lv_obj_set_pos(badge, 6, row_y + 6);
    lv_obj_set_style_bg_color(badge, C_BORDER, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(badge, C_PRIMARY, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_60, 0);
    lv_obj_set_style_radius(badge, 7, 0);
    /* 底部阴影模拟键帽立体感 */
    lv_obj_set_style_shadow_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *key_icon = lv_label_create(badge);
    lv_label_set_text(key_icon, ctrl_keys[i * 2]);
    lv_obj_set_style_text_font(key_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(key_icon, C_PRIMARY, 0);
    lv_obj_center(key_icon);

    /* 描述文字 */
    lv_obj_t *desc = lv_label_create(inst_card);
    lv_label_set_text(desc, ctrl_keys[i * 2 + 1]);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(desc, C_TEXT, 0);
    lv_obj_set_pos(desc, 52, row_y + 10);
}
    
    /* Right control panel */
    int right_x = 1280 - 240;
    int ctrl_y = 120;
    
    /* Control buttons container */
    lv_obj_t *ctrl_card = lv_obj_create(scr);
    lv_obj_set_size(ctrl_card, 200, 480);
    lv_obj_set_pos(ctrl_card, right_x, ctrl_y);
    lv_obj_set_style_bg_color(ctrl_card, C_CARD_BG, 0);
    lv_obj_set_style_border_color(ctrl_card, C_BORDER, 0);
    lv_obj_set_style_border_width(ctrl_card, 2, 0);
    lv_obj_set_style_radius(ctrl_card, 20, 0);
    lv_obj_set_style_pad_all(ctrl_card, 20, 0);
    lv_obj_clear_flag(ctrl_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctrl_card, LV_OBJ_FLAG_HIDDEN);
    
    int btn_size = 70;
    int btn_gap = 15;
    int center_x = 55;
    int center_y = 180;
    
    /* Rotate button (top) */
    lv_obj_t *btn_rotate = lv_btn_create(ctrl_card);
    lv_obj_set_size(btn_rotate, btn_size, btn_size);
    lv_obj_set_pos(btn_rotate, center_x, center_y - btn_size - btn_gap);
    lv_obj_set_style_radius(btn_rotate, 16, 0);
    lv_obj_set_style_bg_color(btn_rotate, C_ACCENT, 0);
    lv_obj_set_style_border_width(btn_rotate, 0, 0);
    lv_obj_t *lbl_rotate = lv_label_create(btn_rotate);
    lv_label_set_text(lbl_rotate, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(lbl_rotate, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_rotate);
    ui->btn_rotate = btn_rotate;
    
    /* Left button */
    lv_obj_t *btn_left = lv_btn_create(ctrl_card);
    lv_obj_set_size(btn_left, btn_size, btn_size);
    lv_obj_set_pos(btn_left, center_x - btn_size - btn_gap, center_y);
    lv_obj_set_style_radius(btn_left, 16, 0);
    lv_obj_set_style_bg_color(btn_left, C_PRIMARY, 0);
    lv_obj_set_style_border_width(btn_left, 0, 0);
    lv_obj_t *lbl_left = lv_label_create(btn_left);
    lv_label_set_text(lbl_left, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_left, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_left);
    ui->btn_left = btn_left;
    
    /* Right button */
    lv_obj_t *btn_right = lv_btn_create(ctrl_card);
    lv_obj_set_size(btn_right, btn_size, btn_size);
    lv_obj_set_pos(btn_right, center_x + btn_size + btn_gap, center_y);
    lv_obj_set_style_radius(btn_right, 16, 0);
    lv_obj_set_style_bg_color(btn_right, C_PRIMARY, 0);
    lv_obj_set_style_border_width(btn_right, 0, 0);
    lv_obj_t *lbl_right = lv_label_create(btn_right);
    lv_label_set_text(lbl_right, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(lbl_right, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_right);
    ui->btn_right = btn_right;
    
    /* Down button */
    lv_obj_t *btn_down = lv_btn_create(ctrl_card);
    lv_obj_set_size(btn_down, btn_size, btn_size);
    lv_obj_set_pos(btn_down, center_x, center_y + btn_size + btn_gap);
    lv_obj_set_style_radius(btn_down, 16, 0);
    lv_obj_set_style_bg_color(btn_down, C_PRIMARY, 0);
    lv_obj_set_style_border_width(btn_down, 0, 0);
    lv_obj_t *lbl_down = lv_label_create(btn_down);
    lv_label_set_text(lbl_down, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(lbl_down, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_down);
    ui->btn_down = btn_down;
    
    /* Drop button */
    lv_obj_t *btn_drop = lv_btn_create(ctrl_card);
    lv_obj_set_size(btn_drop, 160, 55);
    lv_obj_set_pos(btn_drop, 10, 360);
    lv_obj_set_style_radius(btn_drop, 16, 0);
    lv_obj_set_style_bg_color(btn_drop, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_border_width(btn_drop, 0, 0);
    lv_obj_t *lbl_drop = lv_label_create(btn_drop);
    lv_label_set_text(lbl_drop, "DROP");
    lv_obj_set_style_text_font(lbl_drop, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_drop);
    ui->btn_drop = btn_drop;
    
    /* Top bar buttons */
    /* Pause button */
    lv_obj_t *btn_pause = lv_btn_create(scr);
    lv_obj_set_size(btn_pause, 100, 50);
    lv_obj_set_pos(btn_pause, 1280 - 230, 20);
    lv_obj_set_style_radius(btn_pause, 12, 0);
    lv_obj_set_style_bg_color(btn_pause, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(btn_pause, 0, 0);
    lv_obj_t *lbl_pause = lv_label_create(btn_pause);
    lv_label_set_text(lbl_pause, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_font(lbl_pause, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_pause);
    ui->btn_pause = btn_pause;
    lv_obj_add_flag(btn_pause, LV_OBJ_FLAG_HIDDEN);
    
    /* Home button */
    lv_obj_t *btn_home = lv_btn_create(scr);
    lv_obj_set_size(btn_home, 100, 50);
    lv_obj_set_pos(btn_home, 1280 - 120, 20);
    lv_obj_set_style_radius(btn_home, 12, 0);
    lv_obj_set_style_bg_color(btn_home, lv_color_hex(0x6B7280), 0);
    lv_obj_set_style_border_width(btn_home, 0, 0);
    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(lbl_home, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_home);
    ui->btn_home = btn_home;
    lv_obj_add_flag(btn_home, LV_OBJ_FLAG_HIDDEN);
    
    /* Pause overlay — gray screen + pause icon */
    lv_obj_t *pause_overlay = lv_obj_create(scr);
    lv_obj_set_size(pause_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(pause_overlay, lv_color_hex(0x404050), 0);
    lv_obj_set_style_bg_opa(pause_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(pause_overlay, 0, 0);
    lv_obj_clear_flag(pause_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pause_icon = lv_label_create(pause_overlay);
    lv_label_set_text(pause_icon, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_font(pause_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(pause_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(pause_icon, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *pause_text = lv_label_create(pause_overlay);
    lv_label_set_text(pause_text, "PAUSED");
    lv_obj_set_style_text_font(pause_text, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pause_text, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(pause_text, LV_ALIGN_CENTER, 0, 30);

    lv_obj_add_flag(pause_overlay, LV_OBJ_FLAG_HIDDEN);
    ui->pause_overlay = pause_overlay;

    /* Game over overlay */
    lv_obj_t *gameover_overlay = lv_obj_create(scr);
    lv_obj_set_size(gameover_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(gameover_overlay, lv_color_hex(0x0A0A0F), 0);
    lv_obj_set_style_bg_opa(gameover_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(gameover_overlay, 0, 0);
    lv_obj_clear_flag(gameover_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *go_title = lv_label_create(gameover_overlay);
    lv_label_set_text(go_title, "GAME OVER");
    lv_obj_set_style_text_font(go_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(go_title, C_ACCENT, 0);
    lv_obj_align(go_title, LV_ALIGN_CENTER, 0, -24);

    lv_obj_t *go_sub = lv_label_create(gameover_overlay);
    lv_label_set_text(go_sub, "Next round starting...");
    lv_obj_set_style_text_font(go_sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(go_sub, C_TEXT_DIM, 0);
    lv_obj_align(go_sub, LV_ALIGN_CENTER, 0, 28);

    lv_obj_add_flag(gameover_overlay, LV_OBJ_FLAG_HIDDEN);
    ui->gameover_overlay = gameover_overlay;
    
    /* Hidden status row for compatibility */
    ui->status_row = NULL;
    ui->status_txt = NULL;
}

void ui_game_set_connected(ui_game_t *ui, bool connected)
{
    /* Not displayed in modern design */
    (void)ui;
    (void)connected;
}

void ui_game_set_button_callbacks(ui_game_t *ui, lv_event_cb_t cb, void *user)
{
    if (ui == NULL || cb == NULL) {
        return;
    }
    lv_obj_add_event_cb(ui->btn_left, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(ui->btn_right, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(ui->btn_down, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(ui->btn_rotate, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(ui->btn_drop, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(ui->btn_pause, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(ui->btn_home, cb, LV_EVENT_CLICKED, user);
}

void ui_game_update_stats(ui_game_t *ui, uint32_t score, uint32_t level, uint32_t lines)
{
    if (ui == NULL) {
        return;
    }
    char buf[32];
    
    if (ui->score_label) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
        lv_label_set_text(ui->score_label, buf);
    }
    
    if (ui->level_label) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)level);
        lv_label_set_text(ui->level_label, buf);
    }
    
    if (ui->lines_label) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)lines);
        lv_label_set_text(ui->lines_label, buf);
    }
}

void ui_game_render_next(ui_game_t *ui, int next_type)
{
    if (ui == NULL || ui->next_canvas == NULL || ui->next_buffer == NULL) {
        return;
    }
    
    static const lv_color_t colors[] = {
        LV_COLOR_MAKE(0x00, 0x00, 0x00),
        LV_COLOR_MAKE(0x5E, 0xC5, 0xFF), /* I */
        LV_COLOR_MAKE(0xFF, 0xD8, 0x5E), /* O */
        LV_COLOR_MAKE(0xC0, 0x84, 0xFC), /* T */
        LV_COLOR_MAKE(0x6A, 0xFF, 0xB5), /* S */
        LV_COLOR_MAKE(0xFF, 0x6B, 0x98), /* Z */
        LV_COLOR_MAKE(0x6B, 0x8C, 0xFF), /* J */
        LV_COLOR_MAKE(0xFF, 0x9A, 0x4D), /* L */
    };
    
    static const int8_t shapes[7][4][2] = {
        {{0,1},{1,1},{2,1},{3,1}}, /* I */
        {{1,0},{2,0},{1,1},{2,1}}, /* O */
        {{1,0},{0,1},{1,1},{2,1}}, /* T */
        {{1,0},{2,0},{0,1},{1,1}}, /* S */
        {{0,0},{1,0},{1,1},{2,1}}, /* Z */
        {{0,0},{0,1},{1,1},{2,1}}, /* J */
        {{2,0},{0,1},{1,1},{2,1}}, /* L */
    };
    
    lv_canvas_fill_bg(ui->next_canvas, C_BOARD_BG, LV_OPA_COVER);
    
    if (next_type >= 0 && next_type < 7) {
        lv_draw_rect_dsc_t rect;
        lv_draw_rect_dsc_init(&rect);
        rect.bg_opa = LV_OPA_COVER;
        rect.bg_color = colors[next_type + 1];
        rect.border_width = 1;
        rect.border_color = lv_color_hex(0x1F1B2A);
        rect.border_opa = LV_OPA_40;
        rect.radius = 4;
        
        lv_layer_t layer;
        lv_canvas_init_layer(ui->next_canvas, &layer);
        
        int cell_size = 22;
        int offset_x = 16;
        int offset_y = 30;
        
        for (int i = 0; i < 4; i++) {
            int x = shapes[next_type][i][0];
            int y = shapes[next_type][i][1];
            
            lv_area_t area;
            area.x1 = offset_x + x * cell_size;
            area.y1 = offset_y + y * cell_size;
            area.x2 = area.x1 + cell_size - 1;
            area.y2 = area.y1 + cell_size - 1;
            lv_draw_rect(&layer, &rect, &area);
        }
        
        lv_canvas_finish_layer(ui->next_canvas, &layer);
    }
}

void ui_game_show_pause(ui_game_t *ui, bool paused)
{
    if (ui == NULL || ui->pause_overlay == NULL) {
        return;
    }

    if (paused) {
        lv_obj_clear_flag(ui->pause_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui->pause_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_game_show_gameover(ui_game_t *ui, bool gameover)
{
    if (ui == NULL || ui->gameover_overlay == NULL) {
        return;
    }

    if (gameover) {
        lv_obj_clear_flag(ui->gameover_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui->gameover_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}
