#include "tetris_engine.h"
#include <string.h>
#include "esp_random.h"

static const int8_t shapes[7][4][4][2] = {
    /* I */
    {
        {{0,1},{1,1},{2,1},{3,1}},
        {{2,0},{2,1},{2,2},{2,3}},
        {{0,2},{1,2},{2,2},{3,2}},
        {{1,0},{1,1},{1,2},{1,3}},
    },
    /* O */
    {
        {{1,0},{2,0},{1,1},{2,1}},
        {{1,0},{2,0},{1,1},{2,1}},
        {{1,0},{2,0},{1,1},{2,1}},
        {{1,0},{2,0},{1,1},{2,1}},
    },
    /* T */
    {
        {{1,0},{0,1},{1,1},{2,1}},
        {{1,0},{1,1},{2,1},{1,2}},
        {{0,1},{1,1},{2,1},{1,2}},
        {{1,0},{0,1},{1,1},{1,2}},
    },
    /* S */
    {
        {{1,0},{2,0},{0,1},{1,1}},
        {{1,0},{1,1},{2,1},{2,2}},
        {{1,1},{2,1},{0,2},{1,2}},
        {{0,0},{0,1},{1,1},{1,2}},
    },
    /* Z */
    {
        {{0,0},{1,0},{1,1},{2,1}},
        {{2,0},{1,1},{2,1},{1,2}},
        {{0,1},{1,1},{1,2},{2,2}},
        {{1,0},{0,1},{1,1},{0,2}},
    },
    /* J */
    {
        {{0,0},{0,1},{1,1},{2,1}},
        {{1,0},{2,0},{1,1},{1,2}},
        {{0,1},{1,1},{2,1},{2,2}},
        {{1,0},{1,1},{0,2},{1,2}},
    },
    /* L */
    {
        {{2,0},{0,1},{1,1},{2,1}},
        {{1,0},{1,1},{1,2},{2,2}},
        {{0,1},{1,1},{2,1},{0,2}},
        {{0,0},{1,0},{1,1},{1,2}},
    },
};

static bool collides(const tetris_t *t, int nx, int ny, int nrot)
{
    for (int i = 0; i < 4; i++) {
        int x = nx + shapes[t->cur_type][nrot][i][0];
        int y = ny + shapes[t->cur_type][nrot][i][1];
        if (x < 0 || x >= TETRIS_COLS || y >= TETRIS_ROWS + TETRIS_BUFFER_ROWS) {
            return true;
        }
        if (y >= 0 && t->board[y][x] != 0) {
            return true;
        }
    }
    return false;
}

static void lock_piece(tetris_t *t)
{
    for (int i = 0; i < 4; i++) {
        int x = t->cur_x + shapes[t->cur_type][t->cur_rot][i][0];
        int y = t->cur_y + shapes[t->cur_type][t->cur_rot][i][1];
        if (x >= 0 && x < TETRIS_COLS && y >= 0 && y < TETRIS_ROWS + TETRIS_BUFFER_ROWS) {
            t->board[y][x] = (uint8_t)(t->cur_type + 1);
        }
    }
}

static void clear_lines(tetris_t *t)
{
    int lines = 0;
    for (int y = TETRIS_ROWS + TETRIS_BUFFER_ROWS - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < TETRIS_COLS; x++) {
            if (t->board[y][x] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            lines++;
            for (int yy = y; yy > 0; yy--) {
                memcpy(t->board[yy], t->board[yy - 1], TETRIS_COLS);
            }
            memset(t->board[0], 0, TETRIS_COLS);
            y++; /* re-check same row after shift */
        }
    }
    
    /* Score based on lines cleared at once */
    if (lines > 0) {
        t->lines_cleared += lines;
        uint32_t line_score[] = {0, 100, 300, 500, 800}; /* 1-4 lines */
        t->score += line_score[lines < 5 ? lines : 4] * (t->level + 1);
        
        /* Level up every 10 lines */
        t->level = t->lines_cleared / 10;
    }
}

static void spawn_piece(tetris_t *t)
{
    t->cur_type = t->next_type;
    t->next_type = (int)(esp_random() % 7);
    t->cur_rot = 0;
    t->cur_x = 3;
    t->cur_y = -1;  /* Start above visible area */
    if (collides(t, t->cur_x, t->cur_y, t->cur_rot)) {
        t->game_over = true;
    }
}

void tetris_init(tetris_t *t)
{
    if (t == NULL) {
        return;
    }
    tetris_reset(t);
}

void tetris_reset(tetris_t *t)
{
    if (t == NULL) {
        return;
    }
    memset(t->board, 0, sizeof(t->board));
    t->game_over = false;
    t->paused = false;
    t->score = 0;
    t->lines_cleared = 0;
    t->level = 0;
    t->next_type = (int)(esp_random() % 7);
    spawn_piece(t);
}

bool tetris_tick(tetris_t *t)
{
    if (t == NULL || t->game_over || t->paused) {
        return false;
    }
    if (!collides(t, t->cur_x, t->cur_y + 1, t->cur_rot)) {
        t->cur_y++;
        return true;
    }
    lock_piece(t);
    clear_lines(t);
    spawn_piece(t);
    return true;
}

bool tetris_move(tetris_t *t, int dx, int dy)
{
    if (t == NULL || t->game_over || t->paused) {
        return false;
    }
    int nx = t->cur_x + dx;
    int ny = t->cur_y + dy;
    if (collides(t, nx, ny, t->cur_rot)) {
        return false;
    }
    t->cur_x = nx;
    t->cur_y = ny;
    return true;
}

bool tetris_rotate(tetris_t *t)
{
    if (t == NULL || t->game_over || t->paused) {
        return false;
    }
    int nrot = (t->cur_rot + 1) % 4;
    
    /* Try basic rotation */
    if (!collides(t, t->cur_x, t->cur_y, nrot)) {
        t->cur_rot = nrot;
        return true;
    }
    
    /* Wall kick: try shifting left/right */
    int kicks[][2] = {{-1, 0}, {1, 0}, {-2, 0}, {2, 0}, {0, -1}};
    for (int i = 0; i < 5; i++) {
        int nx = t->cur_x + kicks[i][0];
        int ny = t->cur_y + kicks[i][1];
        if (!collides(t, nx, ny, nrot)) {
            t->cur_x = nx;
            t->cur_y = ny;
            t->cur_rot = nrot;
            return true;
        }
    }
    
    return false;
}

void tetris_hard_drop(tetris_t *t)
{
    if (t == NULL || t->game_over || t->paused) {
        return;
    }
    int drop_distance = 0;
    while (!collides(t, t->cur_x, t->cur_y + 1, t->cur_rot)) {
        t->cur_y++;
        drop_distance++;
    }
    t->score += drop_distance * 2;  /* Bonus for hard drop */
    lock_piece(t);
    clear_lines(t);
    spawn_piece(t);
}

void tetris_toggle_pause(tetris_t *t)
{
    if (t == NULL || t->game_over) {
        return;
    }
    t->paused = !t->paused;
}

uint32_t tetris_get_drop_interval(const tetris_t *t)
{
    if (t == NULL) {
        return 500;
    }
    /* Speed increases with level: 500ms at level 0, down to 100ms at level 10+ */
    uint32_t base = 500;
    uint32_t reduction = t->level * 40;
    if (reduction > 400) {
        reduction = 400;
    }
    return base - reduction;
}

uint8_t tetris_get_cell(const tetris_t *t, int x, int y)
{
    if (t == NULL || x < 0 || x >= TETRIS_COLS || y < 0 || y >= TETRIS_ROWS) {
        return 0;
    }
    /* Adjust y to account for buffer rows */
    int board_y = y + TETRIS_BUFFER_ROWS;
    uint8_t v = t->board[board_y][x];
    
    /* Draw current piece */
    for (int i = 0; i < 4; i++) {
        int px = t->cur_x + shapes[t->cur_type][t->cur_rot][i][0];
        int py = t->cur_y + shapes[t->cur_type][t->cur_rot][i][1];
        if (px == x && py == board_y) {
            return (uint8_t)(t->cur_type + 1);
        }
    }
    return v;
}
