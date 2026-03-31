idf_component_register(
    SRCS
        # Application entry
        "main.c"

        # Wireless stack
        "wireless_mgr.c"
        "espnow_mgr.c"
        "ble_mgr.c"
        "uart_mgr.c"

        # UI pages
        "ui_page1.c"
        "ui_page2.c"
        "ui_page3.c"
        "ui_page4.c"
        "ui_page5.c"

        # Image assets
        "images/choose_gif.c"
        "images/logo_espnow.c"
        "images/bluetooth_logo.c"
        "images/arrow_orange.c"
        "images/arrow_blue.c"
        "images/finder.c"
        "images/node.c"
        "images/bluetooth.c"
        "images/espnow.c"
        "images/UI1.c"
        "images/UI2.c"
        "images/UI3.c"
        "images/UI4.c"

        # Font assets
        "fonts/font_inter_bold_48.c"
        "fonts/font_inter_light_14.c"
        "fonts/font_inter_bold_32.c"
        "fonts/font_inter_regular_16.c"
        "fonts/font_inter_bold_13.c"
        "fonts/font_inter_regular_14.c"
        "fonts/font_inter_bold_14.c"
        "fonts/font_inter_bold_10.c"
        "fonts/font_inter_bold_24.c"
        "fonts/font_inter_regular_12.c"
        "fonts/font_inter_medium_10.c"
        "fonts/font_inter_regular_24.c"
        "fonts/font_inter_regular_20.c"
        "fonts/font_inter_regular_32.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        bt
        nvs_flash
        esp_wifi
        esp_event
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
