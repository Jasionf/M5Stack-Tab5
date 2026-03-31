#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *root;
    lv_obj_t *conn_row;
    lv_obj_t *conn_txt;
    lv_obj_t *start_btn;
} ui_home_t;

void ui_home_init(ui_home_t *ui, lv_obj_t *scr);
void ui_home_set_connected(ui_home_t *ui, bool connected);
void ui_home_set_start_cb(ui_home_t *ui, lv_event_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif
