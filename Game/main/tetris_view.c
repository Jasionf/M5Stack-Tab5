#include "tetris_view.h"
#include "esp_heap_caps.h"

#define CELL_SIZE 32

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

bool tetris_view_init(tetris_view_t *view, lv_obj_t *canvas)
{
    if (view == NULL || canvas == NULL) {
        return false;
    }

    view->canvas = canvas;
    view->width = TETRIS_COLS * CELL_SIZE;
    view->height = TETRIS_ROWS * CELL_SIZE;

    size_t buf_size = (size_t)view->width * (size_t)view->height * sizeof(lv_color_t);
    view->buffer = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (view->buffer == NULL) {
        view->buffer = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_8BIT);
    }
    if (view->buffer == NULL) {
        return false;
    }

    lv_canvas_set_buffer(view->canvas, view->buffer, view->width, view->height, LV_COLOR_FORMAT_RGB565);
    return true;
}

void tetris_view_render(tetris_view_t *view, const tetris_t *t)
{
    if (view == NULL || view->canvas == NULL || view->buffer == NULL || t == NULL) {
        return;
    }

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.border_width = 1;
    rect.border_color = lv_color_hex(0x1F1B2A);

    lv_canvas_fill_bg(view->canvas, lv_color_hex(0x14121C), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(view->canvas, &layer);

    for (int y = 0; y < TETRIS_ROWS; y++) {
        for (int x = 0; x < TETRIS_COLS; x++) {
            uint8_t cell = tetris_get_cell(t, x, y);
            if (cell > 0 && cell < (sizeof(colors) / sizeof(colors[0]))) {
                rect.bg_color = colors[cell];
                rect.border_opa = LV_OPA_40;
            } else {
                rect.bg_color = lv_color_hex(0x14121C);
                rect.border_opa = LV_OPA_10;
            }

            lv_area_t area;
            area.x1 = x * CELL_SIZE;
            area.y1 = y * CELL_SIZE;
            area.x2 = area.x1 + CELL_SIZE - 1;
            area.y2 = area.y1 + CELL_SIZE - 1;
            lv_draw_rect(&layer, &rect, &area);
        }
    }

    lv_canvas_finish_layer(view->canvas, &layer);
}

void tetris_view_deinit(tetris_view_t *view)
{
    if (view == NULL) {
        return;
    }
    if (view->buffer) {
        heap_caps_free(view->buffer);
    }
    view->buffer = NULL;
    view->canvas = NULL;
}
