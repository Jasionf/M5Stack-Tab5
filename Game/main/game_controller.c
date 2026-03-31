#include "game_controller.h"
#include "ui_home.h"
#include "ui_game.h"
#include "tetris_engine.h"
#include "tetris_view.h"
#include "input_uart.h"
#include "input_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "game_ctrl";

typedef enum {
    MSG_INPUT = 0,
    MSG_CONNECTED,
    MSG_START_GAME,
} msg_type_t;

typedef struct {
    msg_type_t type;
    input_action_event_t input;
    bool connected;
} msg_t;

static QueueHandle_t s_queue;
static ui_home_t s_ui_home;
static ui_game_t s_ui_game;
static tetris_t s_tetris;
static tetris_view_t s_view;
static lv_timer_t *s_tick_timer;
static lv_timer_t *s_gameover_timer;
static bool s_connected;
static bool s_in_game;
static bool s_paused;
static bool s_fast_drop;

static void render_game(void)
{
    tetris_view_render(&s_view, &s_tetris);
    ui_game_update_stats(&s_ui_game, s_tetris.score, s_tetris.level, s_tetris.lines_cleared);
    ui_game_render_next(&s_ui_game, s_tetris.next_type);
    ui_game_show_pause(&s_ui_game, s_paused);
}

static void on_input(const input_action_event_t *evt, void *user)
{
    (void)user;
    if (!evt) {
        return;
    }
    msg_t msg = {
        .type = MSG_INPUT,
        .input = *evt,
    };
    if (s_queue) {
        xQueueSend(s_queue, &msg, 0);
    }
}

static void on_status(bool connected, void *user)
{
    (void)user;
    msg_t msg = {
        .type = MSG_CONNECTED,
        .connected = connected,
    };
    if (s_queue) {
        xQueueSend(s_queue, &msg, 0);
    }
}

static void on_start_button(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    msg_t msg = {
        .type = MSG_START_GAME,
    };
    if (s_queue) {
        xQueueSend(s_queue, &msg, 0);
    }
}

static void gameover_restart_cb(lv_timer_t *timer)
{
    (void)timer;
    s_gameover_timer = NULL;
    ui_game_show_gameover(&s_ui_game, false);
    tetris_reset(&s_tetris);
    s_paused = false;
    s_fast_drop = false;
    render_game();
    if (s_tick_timer) {
        lv_timer_set_period(s_tick_timer, tetris_get_drop_interval(&s_tetris));
        lv_timer_resume(s_tick_timer);
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_in_game || s_paused) {
        return;
    }
    tetris_tick(&s_tetris);
    render_game();
    if (s_tetris.game_over) {
        lv_timer_pause(s_tick_timer);
        ui_game_show_gameover(&s_ui_game, true);
        if (s_gameover_timer == NULL) {
            s_gameover_timer = lv_timer_create(gameover_restart_cb, 2500, NULL);
            lv_timer_set_repeat_count(s_gameover_timer, 1);
        }
    } else {
        lv_timer_set_period(s_tick_timer, tetris_get_drop_interval(&s_tetris));
    }
}

static void process_queue(lv_timer_t *timer)
{
    (void)timer;
    msg_t msg;
    while (s_queue && xQueueReceive(s_queue, &msg, 0) == pdTRUE) {
        if (msg.type == MSG_CONNECTED) {
            s_connected = msg.connected;
            ui_home_set_connected(&s_ui_home, s_connected);
            ui_game_set_connected(&s_ui_game, s_connected);
            continue;
        }

        if (msg.type == MSG_START_GAME) {
            tetris_reset(&s_tetris);
            render_game();
            lv_screen_load(s_ui_game.root);
            if (s_tick_timer) {
                lv_timer_set_period(s_tick_timer, 500);
                lv_timer_resume(s_tick_timer);
            }
            s_in_game = true;
            s_paused = false;
            s_fast_drop = false;
            ESP_LOGI(TAG, "Start button: game start");
            continue;
        }

        if (!s_in_game) {
            continue;
        }

        switch (msg.input.action) {
            case INPUT_ACTION_LEFT:
                if (msg.input.pressed && !s_paused) {
                    tetris_move(&s_tetris, -1, 0);
                }
                break;
            case INPUT_ACTION_RIGHT:
                if (msg.input.pressed && !s_paused) {
                    tetris_move(&s_tetris, 1, 0);
                }
                break;
            case INPUT_ACTION_DOWN:
                if (msg.input.pressed && !s_paused) {
                    tetris_move(&s_tetris, 0, 1);
                }
                break;
            case INPUT_ACTION_ROTATE:
            case INPUT_ACTION_B:  /* B button also rotates */
                if (msg.input.pressed && !s_paused) {
                    tetris_rotate(&s_tetris);
                }
                break;
            case INPUT_ACTION_START:
                if (msg.input.pressed) {
                    tetris_reset(&s_tetris);
                }
                break;
            case INPUT_ACTION_PAUSE:
                if (msg.input.pressed) {
                    s_paused = !s_paused;
                }
                break;
            case INPUT_ACTION_ACCEL:
                s_fast_drop = msg.input.pressed;
                if (s_tick_timer) {
                    lv_timer_set_period(s_tick_timer, s_fast_drop ? 100 : 500);
                }
                break;
            case INPUT_ACTION_EXIT:
                if (msg.input.pressed) {
                    lv_screen_load(s_ui_home.root);
                    if (s_tick_timer) {
                        lv_timer_pause(s_tick_timer);
                    }
                    s_in_game = false;
                    s_paused = false;
                    s_fast_drop = false;
                }
                break;
            default:
                break;
        }

        if (!s_paused) {
            render_game();
        }
    }
}

void game_controller_init(void)
{
    s_queue = xQueueCreate(16, sizeof(msg_t));

    lv_obj_t *home_scr = lv_obj_create(NULL);
    lv_obj_t *game_scr = lv_obj_create(NULL);

    ui_home_init(&s_ui_home, home_scr);
    ui_game_init(&s_ui_game, game_scr);
    ui_home_set_connected(&s_ui_home, false);
    ui_game_set_connected(&s_ui_game, false);
    ui_home_set_start_cb(&s_ui_home, on_start_button, NULL);

    lv_screen_load(home_scr);

    tetris_init(&s_tetris);
    if (tetris_view_init(&s_view, s_ui_game.canvas)) {
        render_game();
    } else {
        ESP_LOGW(TAG, "Tetris view buffer alloc failed");
    }

    s_tick_timer = lv_timer_create(tick_cb, 500, NULL);
    lv_timer_pause(s_tick_timer);

    lv_timer_create(process_queue, 30, NULL);

    esp_err_t uart_ret = input_uart_start(on_input, on_status, NULL);
    if (uart_ret != ESP_OK) {
        ESP_LOGW(TAG, "UART input not started: %s", esp_err_to_name(uart_ret));
    }
}
