idf_component_register(
    SRCS
        "main.c"
        "wireless/wireless_mgr.c"
        "wireless/espnow_mgr.c"
        "wireless/ble_mgr.c"
        "wireless/uart_mgr.c"
        "ui/ui_home.c"
        "ui/ui_scan.c"
        "ui/ui_connect.c"
        "ui/ui_connected.c"
        "ui/ui_terminal.c"

        "assets/images/choose_gif.c"
        "assets/images/logo_espnow.c"
        "assets/images/bluetooth_logo.c"
        "assets/images/arrow_orange.c"
        "assets/images/arrow_blue.c"
        "assets/images/finder.c"
        "assets/images/node.c"

        "assets/fonts/font_inter_bold_48.c"
        "assets/fonts/font_inter_light_14.c"
        "assets/fonts/font_inter_bold_32.c"
        "assets/fonts/font_inter_regular_16.c"
        "assets/fonts/font_inter_bold_13.c"
        "assets/fonts/font_inter_regular_14.c"
        "assets/fonts/font_inter_bold_14.c"
        "assets/fonts/font_inter_bold_10.c"
        "assets/fonts/font_inter_bold_24.c"
        "assets/fonts/font_inter_regular_12.c"
        "assets/fonts/font_inter_medium_10.c"
        "assets/fonts/font_inter_regular_24.c"
        "assets/fonts/font_inter_regular_20.c"
        "assets/fonts/font_inter_regular_32.c"
    INCLUDE_DIRS "." "wireless" "ui"
    PRIV_REQUIRES
        bt
        nvs_flash
        esp_wifi
        esp_event
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
