idf_component_register(
    SRCS
        # Application entry
        "main.c"

        # Game UI
        "ui_home.c"
        "ui_game.c"

        # Game logic
        "tetris_engine.c"
        "tetris_view.c"
        "game_controller.c"

        # Input
        "input_uart.c"
        "uart_mgr.c"

    INCLUDE_DIRS "."
    PRIV_REQUIRES
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
