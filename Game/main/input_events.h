#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_ACTION_LEFT = 0,
    INPUT_ACTION_RIGHT,
    INPUT_ACTION_DOWN,
    INPUT_ACTION_ROTATE,
    INPUT_ACTION_START,
    INPUT_ACTION_PAUSE,
    INPUT_ACTION_ACCEL,
    INPUT_ACTION_EXIT,
    INPUT_ACTION_B,  /* B button for rotate */
} input_action_t;

typedef struct {
    input_action_t action;
    bool pressed;
} input_action_event_t;

typedef void (*input_action_cb_t)(const input_action_event_t *evt, void *user);

typedef void (*input_status_cb_t)(bool connected, void *user);

#ifdef __cplusplus
}
#endif
