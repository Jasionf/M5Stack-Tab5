/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief GameDemo - M5Stack Tab5 Game Application
 * @details NEON TETRIS - A game demonstration project for M5Stack Tab5 using LVGL
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "game_controller.h"

void app_main(void)
{
    bsp_display_start();

    ESP_LOGI("GameDemo", "Starting NEON TETRIS UI");
    bsp_display_lock(0);
    
    // Get display and set landscape orientation
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
#if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS > 1 && CONFIG_LV_USE_GESTURE_RECOGNITION)
    lv_indev_t *indev = bsp_display_get_input_dev();
    lv_indev_set_rotation_rad_threshold(indev, 0.15f);
#endif
    (void)scr;
    game_controller_init();

    bsp_display_unlock();
    bsp_display_backlight_on();
}
