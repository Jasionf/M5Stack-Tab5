/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief BSP Display Example
 * @details Show an image on the screen with a simple startup animation (LVGL)
 * @example https://espressif.github.io/esp-launchpad/?flashConfigURL=https://espressif.github.io/esp-bsp/config.toml&app=display-
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "wireless_mgr.h"

extern void example_lvgl_demo_ui(lv_obj_t *scr);

void app_main(void)
{
    /* ── 1. Display + LVGL (must come first — initialises I2C & IO expander) ── */
    bsp_display_start();

    ESP_LOGI("main", "Display LVGL animation");
    bsp_display_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
#if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS > 1 && CONFIG_LV_USE_GESTURE_RECOGNITION)
    lv_indev_t *indev = bsp_display_get_input_dev();
    lv_indev_set_rotation_rad_threshold(indev, 0.15f);
#endif
    example_lvgl_demo_ui(scr);

    bsp_display_unlock();
    bsp_display_backlight_on();

    /* ── 2. Wireless stack — after BSP so I2C/IO expander are ready ── */
    esp_err_t w = wireless_init();
    if (w != ESP_OK) {
        ESP_LOGW("main", "wireless_init failed (%s) — continuing without wireless",
                 esp_err_to_name(w));
    }
}
