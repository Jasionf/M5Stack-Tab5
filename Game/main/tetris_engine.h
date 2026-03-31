#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TETRIS_COLS 10
#define TETRIS_ROWS 20
#define TETRIS_BUFFER_ROWS 4  /* invisible rows at top */

typedef struct {
    uint8_t board[TETRIS_ROWS + TETRIS_BUFFER_ROWS][TETRIS_COLS];
    int cur_type;
    int cur_rot;
    int cur_x;
    int cur_y;
    int next_type;
    bool game_over;
    bool paused;
    uint32_t score;
    uint32_t lines_cleared;
    uint32_t level;
} tetris_t;

void tetris_init(tetris_t *t);
void tetris_reset(tetris_t *t);
bool tetris_tick(tetris_t *t);
bool tetris_move(tetris_t *t, int dx, int dy);
bool tetris_rotate(tetris_t *t);
void tetris_hard_drop(tetris_t *t);
void tetris_toggle_pause(tetris_t *t);
uint8_t tetris_get_cell(const tetris_t *t, int x, int y);
uint32_t tetris_get_drop_interval(const tetris_t *t);

#ifdef __cplusplus
}
#endif
