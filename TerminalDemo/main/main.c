#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "ui_common.h"
#include "esp_io_expander_pi4ioe5v6408.h"

static esp_io_expander_handle_t io_expander = NULL;
static const char *TAG = "TerminalDemo";

void app_main(void)
{
    
    const bsp_display_cfg_t display_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .sw_rotate = true,
        },
    };
    if (!bsp_display_start_with_config(&display_cfg)) {
        ESP_LOGW(TAG, "full-screen LVGL buffers failed, falling back to BSP defaults");
        bsp_display_start();
    }
    
    io_expander = bsp_io_expander_init();
    ESP_LOGI(TAG, "IO expander PI4IOE5V6408 handle: %p", io_expander);

    
    
    ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_output_mode(io_expander, IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL));
    ESP_ERROR_CHECK(esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 1));
    ESP_LOGI(TAG, "IO expander P2 enabled (high)");

    ESP_LOGI("main", "Display LVGL animation");
    bsp_display_lock(0);
#if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS > 1 && CONFIG_LV_USE_GESTURE_RECOGNITION)
    lv_indev_t *indev = bsp_display_get_input_dev();
    lv_indev_set_rotation_rad_threshold(indev, 0.15f);
#endif
    build_gamepage();

    bsp_display_unlock();
    bsp_display_backlight_on();
}
