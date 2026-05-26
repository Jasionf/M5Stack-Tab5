# Locate SSH keys: prefer ~/.esp_keys/, fall back to the existing SSHKEYBOARD project keys.
if(EXISTS "$ENV{HOME}/.esp_keys/id_ed25519")
    set(SSH_KEY_DIR "$ENV{HOME}/.esp_keys")
elseif(EXISTS "/Users/m5-vincent/Documents/SSHKEYBOARD/esp32-terminal/keys/id_ed25519")
    set(SSH_KEY_DIR "/Users/m5-vincent/Documents/SSHKEYBOARD/esp32-terminal/keys")
else()
    message(FATAL_ERROR "SSH key pair not found. Expected ~/.esp_keys/id_ed25519 or /Users/m5-vincent/Documents/SSHKEYBOARD/esp32-terminal/keys/id_ed25519")
endif()

idf_component_register(
    SRCS
        "main.c"
        "keyboard/keyboard_mgr.c"
        "ui/ui_terminal.c"
        "ui/ui_nokia.c"
        "ui/ui_gamepage.cpp"

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
    INCLUDE_DIRS "." "keyboard" "wireless" "ui"
    PRIV_REQUIRES
        bt
        nvs_flash
        esp_wifi
        esp_event
        esp_driver_i2c
        esp_netif
        esp32-wolfssh-client
        libvterm
        Jet
    EMBED_TXTFILES "${SSH_KEY_DIR}/id_ed25519"
                   "${SSH_KEY_DIR}/id_ed25519.pub"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)

set_source_files_properties("ui/ui_gamepage.cpp" PROPERTIES COMPILE_OPTIONS -O3)
