#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *root;
    lv_obj_t *canvas;
    lv_obj_t *status_row;
    lv_obj_t *status_txt;
    lv_obj_t *btn_left;
    lv_obj_t *btn_right;
    lv_obj_t *btn_down;
    lv_obj_t *btn_rotate;
    lv_obj_t *btn_drop;
    lv_obj_t *btn_pause;
    lv_obj_t *btn_home;
    lv_obj_t *score_label;
    lv_obj_t *level_label;
    lv_obj_t *lines_label;
    lv_obj_t *next_canvas;
    lv_obj_t *pause_overlay;
    lv_obj_t *gameover_overlay;
    lv_color_t *next_buffer;
} ui_game_t;

void ui_game_init(ui_game_t *ui, lv_obj_t *scr);
void ui_game_set_connected(ui_game_t *ui, bool connected);
void ui_game_set_button_callbacks(ui_game_t *ui, lv_event_cb_t cb, void *user);
void ui_game_update_stats(ui_game_t *ui, uint32_t score, uint32_t level, uint32_t lines);
void ui_game_render_next(ui_game_t *ui, int next_type);
void ui_game_show_pause(ui_game_t *ui, bool paused);
void ui_game_show_gameover(ui_game_t *ui, bool gameover);

#ifdef __cplusplus
}
#endif
