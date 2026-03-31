#pragma once

#include "lvgl.h"
#include "tetris_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *canvas;
    lv_color_t *buffer;
    int width;
    int height;
} tetris_view_t;

bool tetris_view_init(tetris_view_t *view, lv_obj_t *canvas);
void tetris_view_render(tetris_view_t *view, const tetris_t *t);
void tetris_view_deinit(tetris_view_t *view);

#ifdef __cplusplus
}
#endif
