/**
 * @file ui_home.c
 * @brief Modern Home Screen for NEON TETRIS
 */

#include <stdio.h>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_home.h"

static const char *TAG = "ui_home";
static ui_home_t *s_ui_home = NULL;

/* Modern Color Palette */
#define C_BG            lv_color_hex(0x0A0A0F)
#define C_PRIMARY       lv_color_hex(0xA855F7)
#define C_ACCENT        lv_color_hex(0xEC4899)
#define C_TEXT          lv_color_hex(0xF5F5F7)
#define C_TEXT_DIM      lv_color_hex(0x9CA3AF)
#define C_CARD_BG       lv_color_hex(0x1A1A24)

static void obj_clear_style(lv_obj_t *o)
{
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void block_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void start_block_anim(lv_obj_t *block, uint32_t delay_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, block);
    lv_anim_set_exec_cb(&a, block_opa_cb);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_time(&a, 500);
    lv_anim_set_playback_time(&a, 500);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void ui_home_init(ui_home_t *ui, lv_obj_t *scr)
{
    if (ui == NULL || scr == NULL) {
        return;
    }
    
    s_ui_home = ui;
    ui->root = scr;
    
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    /* Ambient glow effects */
    lv_obj_t *glow1 = lv_obj_create(scr);
    lv_obj_set_size(glow1, 500, 500);
    lv_obj_set_pos(glow1, -100, -100);
    lv_obj_set_style_bg_color(glow1, C_PRIMARY, 0);
    lv_obj_set_style_bg_opa(glow1, LV_OPA_10, 0);
    lv_obj_set_style_radius(glow1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow1, 0, 0);
    lv_obj_clear_flag(glow1, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *glow2 = lv_obj_create(scr);
    lv_obj_set_size(glow2, 400, 400);
    lv_obj_set_pos(glow2, 1000, 500);
    lv_obj_set_style_bg_color(glow2, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(glow2, LV_OPA_10, 0);
    lv_obj_set_style_radius(glow2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow2, 0, 0);
    lv_obj_clear_flag(glow2, LV_OBJ_FLAG_SCROLLABLE);
    
    /* Center content container */
    lv_obj_t *center = lv_obj_create(scr);
    lv_obj_set_size(center, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(center, LV_ALIGN_CENTER, 0, -40);
    obj_clear_style(center);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(center, 32, 0);
    
    /* Logo icon */
    lv_obj_t *logo_box = lv_obj_create(center);
    lv_obj_set_size(logo_box, 120, 120);
    lv_obj_set_style_bg_color(logo_box, C_PRIMARY, 0);
    lv_obj_set_style_bg_grad_color(logo_box, C_ACCENT, 0);
    lv_obj_set_style_bg_grad_dir(logo_box, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(logo_box, 24, 0);
    lv_obj_set_style_border_width(logo_box, 0, 0);
    lv_obj_set_style_shadow_width(logo_box, 40, 0);
    lv_obj_set_style_shadow_color(logo_box, C_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(logo_box, LV_OPA_50, 0);
    lv_obj_clear_flag(logo_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *logo_icon = lv_label_create(logo_box);
    lv_label_set_text(logo_icon, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_font(logo_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(logo_icon, C_TEXT, 0);
    lv_obj_center(logo_icon);
    
    /* Title */
    lv_obj_t *title = lv_label_create(center);
    lv_label_set_text(title, "NEON TETRIS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    
    /* Subtitle */
    lv_obj_t *subtitle = lv_label_create(center);
    lv_label_set_text(subtitle, "Welcome to the world of gaming");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, C_TEXT_DIM, 0);
    
    /* Start button */
    lv_obj_t *start_btn = lv_btn_create(center);
    lv_obj_set_size(start_btn, 280, 70);
    lv_obj_set_style_radius(start_btn, 35, 0);
    lv_obj_set_style_bg_color(start_btn, C_PRIMARY, 0);
    lv_obj_set_style_bg_grad_color(start_btn, C_ACCENT, 0);
    lv_obj_set_style_bg_grad_dir(start_btn, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(start_btn, 0, 0);
    lv_obj_set_style_shadow_width(start_btn, 40, 0);
    lv_obj_set_style_shadow_color(start_btn, C_PRIMARY, 0);
    lv_obj_set_style_shadow_opa(start_btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_ofs_y(start_btn, 8, 0);
    lv_obj_t *btn_label = lv_label_create(start_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_PLAY "  START GAME");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(btn_label, C_TEXT, 0);
    lv_obj_center(btn_label);
    
    ui->start_btn = start_btn;
    
    /* Footer dots — smooth pulse */
    lv_obj_t *spinner = lv_obj_create(scr);
    lv_obj_set_size(spinner, 62, 14);
    obj_clear_style(spinner);
    lv_obj_align(spinner, LV_ALIGN_BOTTOM_MID, 0, -40);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *block = lv_obj_create(spinner);
        lv_obj_set_size(block, 10, 10);
        lv_obj_set_pos(block, i * 26, 2);
        lv_obj_set_style_radius(block, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(block, C_PRIMARY, 0);
        lv_obj_set_style_bg_opa(block, LV_OPA_20, 0);
        lv_obj_set_style_border_width(block, 0, 0);
        lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
        start_block_anim(block, (uint32_t)(i * 200));
    }
    
    ESP_LOGI(TAG, "Modern home UI initialized");
}

void ui_home_set_connected(ui_home_t *ui, bool connected)
{
    /* Connection status not shown on home screen in modern design */
    (void)ui;
    (void)connected;
}

void ui_home_set_start_cb(ui_home_t *ui, lv_event_cb_t cb, void *user)
{
    if (ui == NULL || ui->start_btn == NULL || cb == NULL) {
        return;
    }
    lv_obj_add_event_cb(ui->start_btn, cb, LV_EVENT_CLICKED, user);
}
