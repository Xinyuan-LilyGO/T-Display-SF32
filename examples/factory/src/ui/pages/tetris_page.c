#include "tetris_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>

#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define TETRIS_COLS          10
#define TETRIS_ROWS          20
#define TETRIS_CELL_SIZE     17
#define TETRIS_BOARD_WIDTH   (TETRIS_COLS * TETRIS_CELL_SIZE)
#define TETRIS_BOARD_HEIGHT  (TETRIS_ROWS * TETRIS_CELL_SIZE)
#define TETRIS_BOARD_X       24
#define TETRIS_BOARD_Y       104
#define TETRIS_TIMER_MS      50U
#define TETRIS_START_DROP_MS 700U
#define TETRIS_MIN_DROP_MS   100U
#define TETRIS_LEVEL_STEP_MS 50U
#define TETRIS_PIECE_COUNT   7U

typedef struct
{
    uint8_t board[TETRIS_ROWS][TETRIS_COLS];
    uint8_t piece;
    uint8_t rotation;
    uint8_t next_piece;
    int8_t x;
    int8_t y;
    uint32_t score;
    uint32_t lines;
    uint32_t level;
    uint32_t elapsed_ms;
    uint32_t drop_ms;
    bool running;
    bool paused;
    bool game_over;
} tetris_game_t;

static const int8_t tetris_shapes[TETRIS_PIECE_COUNT][4][4][2] = {
    {
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
        {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
        {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
        {{1, 0}, {1, 1}, {1, 2}, {1, 3}},
    },
    {
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
    },
    {
        {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{1, 0}, {1, 1}, {2, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {2, 1}, {1, 2}},
        {{1, 0}, {0, 1}, {1, 1}, {1, 2}},
    },
    {
        {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
        {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
        {{1, 1}, {2, 1}, {0, 2}, {1, 2}},
        {{0, 0}, {0, 1}, {1, 1}, {1, 2}},
    },
    {
        {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
        {{2, 0}, {1, 1}, {2, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {1, 2}, {2, 2}},
        {{1, 0}, {0, 1}, {1, 1}, {0, 2}},
    },
    {
        {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{1, 0}, {2, 0}, {1, 1}, {1, 2}},
        {{0, 1}, {1, 1}, {2, 1}, {2, 2}},
        {{1, 0}, {1, 1}, {0, 2}, {1, 2}},
    },
    {
        {{2, 0}, {0, 1}, {1, 1}, {2, 1}},
        {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
        {{0, 1}, {1, 1}, {2, 1}, {0, 2}},
        {{0, 0}, {1, 0}, {1, 1}, {1, 2}},
    },
};

static const uint32_t tetris_colors[TETRIS_PIECE_COUNT] = {
    0x43B6FFU, 0xF0CF61U, 0xA66CFFU, 0x39FF88U,
    0xFF3B5CU, 0x4D7CFFU, 0xFF9D00U,
};

static tetris_game_t tetris_game;
static uint32_t tetris_best_score;
static uint32_t tetris_random_state = 0x54455452UL;
static lv_obj_t *tetris_board;
static lv_obj_t *tetris_preview;
static lv_obj_t *tetris_score_label;
static lv_obj_t *tetris_best_label;
static lv_obj_t *tetris_lines_label;
static lv_obj_t *tetris_level_label;
static lv_obj_t *tetris_state_label;
static lv_obj_t *tetris_overlay_label;
static lv_obj_t *tetris_pause_icon;

static lv_obj_t *tetris_create_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static void tetris_style_button(lv_obj_t *button, uint32_t color)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_TRACK),
                              LV_STATE_PRESSED);
}

static lv_obj_t *tetris_create_icon_button(lv_obj_t *parent,
                                           int32_t x, int32_t y,
                                           int32_t size,
                                           const char *symbol,
                                           uint32_t color,
                                           lv_event_cb_t callback,
                                           void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *icon;

    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, y);
    tetris_style_button(button, color);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    icon = tetris_create_label(button, symbol, &lv_font_montserrat_20,
                               UI_COLOR_TEXT);
    lv_obj_center(icon);
    return button;
}

static uint32_t tetris_random(void)
{
    tetris_random_state = tetris_random_state * 1664525UL + 1013904223UL;
    return tetris_random_state;
}

static bool tetris_piece_fits(uint8_t piece, uint8_t rotation,
                              int8_t origin_x, int8_t origin_y)
{
    uint8_t block;

    for (block = 0U; block < 4U; block++)
    {
        int16_t x = origin_x + tetris_shapes[piece][rotation][block][0];
        int16_t y = origin_y + tetris_shapes[piece][rotation][block][1];

        if (x < 0 || x >= TETRIS_COLS || y >= TETRIS_ROWS)
            return false;
        if (y >= 0 && tetris_game.board[y][x] != 0U)
            return false;
    }
    return true;
}

static void tetris_update_ui(void)
{
    if (tetris_score_label != RT_NULL)
        lv_label_set_text_fmt(tetris_score_label, "%lu",
                              (unsigned long)tetris_game.score);
    if (tetris_best_label != RT_NULL)
        lv_label_set_text_fmt(tetris_best_label, "%lu",
                              (unsigned long)tetris_best_score);
    if (tetris_lines_label != RT_NULL)
        lv_label_set_text_fmt(tetris_lines_label, "%lu",
                              (unsigned long)tetris_game.lines);
    if (tetris_level_label != RT_NULL)
        lv_label_set_text_fmt(tetris_level_label, "%lu",
                              (unsigned long)tetris_game.level);
    if (tetris_state_label != RT_NULL)
    {
        const char *text = "RUNNING";
        uint32_t color = UI_COLOR_SUCCESS;

        if (tetris_game.game_over)
        {
            text = "GAME OVER";
            color = UI_COLOR_ERROR;
        }
        else if (tetris_game.paused)
        {
            text = "PAUSED";
            color = UI_COLOR_WARNING;
        }
        lv_label_set_text(tetris_state_label, text);
        lv_obj_set_style_text_color(tetris_state_label,
                                    lv_color_hex(color), 0);
    }
    if (tetris_overlay_label != RT_NULL)
    {
        if (tetris_game.game_over || tetris_game.paused)
        {
            lv_label_set_text(tetris_overlay_label,
                              tetris_game.game_over ? "GAME OVER" : "PAUSED");
            lv_obj_set_style_text_color(
                tetris_overlay_label,
                lv_color_hex(tetris_game.game_over ? UI_COLOR_ERROR
                                                    : UI_COLOR_WARNING), 0);
            lv_obj_remove_flag(tetris_overlay_label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(tetris_overlay_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (tetris_pause_icon != RT_NULL)
        lv_label_set_text(tetris_pause_icon,
                          tetris_game.paused ? LV_SYMBOL_PLAY
                                             : LV_SYMBOL_PAUSE);
}

static void tetris_finish(void)
{
    tetris_game.running = false;
    tetris_game.game_over = true;
    if (tetris_game.score > tetris_best_score)
        tetris_best_score = tetris_game.score;
    tetris_update_ui();
}

static bool tetris_spawn_piece(void)
{
    tetris_game.piece = tetris_game.next_piece;
    tetris_game.next_piece = (uint8_t)(tetris_random() %
                                       TETRIS_PIECE_COUNT);
    tetris_game.rotation = 0U;
    tetris_game.x = 3;
    tetris_game.y = 0;
    if (tetris_preview != RT_NULL)
        lv_obj_invalidate(tetris_preview);
    if (!tetris_piece_fits(tetris_game.piece, tetris_game.rotation,
                           tetris_game.x, tetris_game.y))
    {
        tetris_finish();
        return false;
    }
    return true;
}

static uint32_t tetris_clear_lines(void)
{
    int16_t read_row;
    int16_t write_row = TETRIS_ROWS - 1;
    uint32_t cleared = 0U;

    for (read_row = TETRIS_ROWS - 1; read_row >= 0; read_row--)
    {
        uint8_t column;
        bool full = true;

        for (column = 0U; column < TETRIS_COLS; column++)
        {
            if (tetris_game.board[read_row][column] == 0U)
            {
                full = false;
                break;
            }
        }
        if (full)
        {
            cleared++;
            continue;
        }
        if (write_row != read_row)
        {
            memcpy(tetris_game.board[write_row],
                   tetris_game.board[read_row], TETRIS_COLS);
        }
        write_row--;
    }
    while (write_row >= 0)
    {
        memset(tetris_game.board[write_row], 0, TETRIS_COLS);
        write_row--;
    }
    return cleared;
}

static void tetris_lock_piece(void)
{
    static const uint16_t line_scores[5] = {0U, 100U, 300U, 500U, 800U};
    uint8_t block;
    uint32_t cleared;
    uint32_t speed_reduction;

    for (block = 0U; block < 4U; block++)
    {
        int16_t x = tetris_game.x +
                    tetris_shapes[tetris_game.piece]
                                  [tetris_game.rotation][block][0];
        int16_t y = tetris_game.y +
                    tetris_shapes[tetris_game.piece]
                                  [tetris_game.rotation][block][1];

        if (y < 0)
        {
            tetris_finish();
            return;
        }
        tetris_game.board[y][x] = (uint8_t)(tetris_game.piece + 1U);
    }

    cleared = tetris_clear_lines();
    if (cleared > 4U)
        cleared = 4U;
    tetris_game.score += line_scores[cleared] * tetris_game.level;
    tetris_game.lines += cleared;
    tetris_game.level = tetris_game.lines / 10U + 1U;
    speed_reduction = (tetris_game.level - 1U) * TETRIS_LEVEL_STEP_MS;
    if (speed_reduction < TETRIS_START_DROP_MS - TETRIS_MIN_DROP_MS)
    {
        tetris_game.drop_ms = TETRIS_START_DROP_MS - speed_reduction;
    }
    else
    {
        tetris_game.drop_ms = TETRIS_MIN_DROP_MS;
    }
    if (tetris_game.score > tetris_best_score)
        tetris_best_score = tetris_game.score;
    tetris_update_ui();
    (void)tetris_spawn_piece();
    if (tetris_board != RT_NULL)
        lv_obj_invalidate(tetris_board);
}

static bool tetris_move(int8_t delta_x, int8_t delta_y)
{
    int8_t next_x = (int8_t)(tetris_game.x + delta_x);
    int8_t next_y = (int8_t)(tetris_game.y + delta_y);

    if (tetris_game.paused || tetris_game.game_over ||
            !tetris_piece_fits(tetris_game.piece, tetris_game.rotation,
                               next_x, next_y))
        return false;
    tetris_game.x = next_x;
    tetris_game.y = next_y;
    if (tetris_board != RT_NULL)
        lv_obj_invalidate(tetris_board);
    return true;
}

static void tetris_rotate(void)
{
    static const int8_t kicks[] = {0, -1, 1, -2, 2};
    uint8_t next_rotation;
    uint8_t index;

    if (tetris_game.paused || tetris_game.game_over)
        return;
    next_rotation = (uint8_t)((tetris_game.rotation + 1U) % 4U);
    for (index = 0U; index < sizeof(kicks) / sizeof(kicks[0]); index++)
    {
        int8_t next_x = (int8_t)(tetris_game.x + kicks[index]);

        if (tetris_piece_fits(tetris_game.piece, next_rotation,
                              next_x, tetris_game.y))
        {
            tetris_game.x = next_x;
            tetris_game.rotation = next_rotation;
            if (tetris_board != RT_NULL)
                lv_obj_invalidate(tetris_board);
            return;
        }
    }
}

static void tetris_soft_drop(void)
{
    if (tetris_game.paused || tetris_game.game_over)
        return;
    if (tetris_move(0, 1))
    {
        tetris_game.score++;
        tetris_update_ui();
    }
    else
    {
        tetris_lock_piece();
    }
    tetris_game.elapsed_ms = 0U;
}

static void tetris_hard_drop(void)
{
    uint32_t distance = 0U;

    if (tetris_game.paused || tetris_game.game_over)
        return;
    while (tetris_move(0, 1))
        distance++;
    tetris_game.score += distance * 2U;
    tetris_lock_piece();
    tetris_game.elapsed_ms = 0U;
}

static void tetris_new_game(void)
{
    memset(&tetris_game, 0, sizeof(tetris_game));
    tetris_game.level = 1U;
    tetris_game.drop_ms = TETRIS_START_DROP_MS;
    tetris_game.running = true;
    tetris_random_state ^= (uint32_t)rt_tick_get() + tetris_best_score;
    tetris_game.next_piece = (uint8_t)(tetris_random() %
                                       TETRIS_PIECE_COUNT);
    (void)tetris_spawn_piece();
    tetris_update_ui();
    if (tetris_board != RT_NULL)
        lv_obj_invalidate(tetris_board);
}

static void tetris_toggle_pause(void)
{
    if (tetris_game.game_over)
    {
        tetris_new_game();
        return;
    }
    tetris_game.paused = !tetris_game.paused;
    tetris_game.running = !tetris_game.paused;
    tetris_game.elapsed_ms = 0U;
    tetris_update_ui();
}

static void tetris_draw_cell(lv_layer_t *layer, const lv_area_t *board,
                             int16_t x, int16_t y, uint32_t color,
                             lv_opa_t opacity, int32_t inset)
{
    lv_draw_rect_dsc_t rect;
    lv_area_t area;

    if (x < 0 || x >= TETRIS_COLS || y < 0 || y >= TETRIS_ROWS)
        return;
    area.x1 = board->x1 + 1 + x * TETRIS_CELL_SIZE + inset;
    area.y1 = board->y1 + 1 + y * TETRIS_CELL_SIZE + inset;
    area.x2 = board->x1 + 1 + (x + 1) * TETRIS_CELL_SIZE - 1 - inset;
    area.y2 = board->y1 + 1 + (y + 1) * TETRIS_CELL_SIZE - 1 - inset;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_hex(color);
    rect.bg_opa = opacity;
    rect.radius = 2;
    lv_draw_rect(layer, &rect, &area);
}

static void tetris_board_draw(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t board;
    lv_draw_line_dsc_t line;
    int8_t ghost_y;
    uint8_t row;
    uint8_t column;
    uint8_t block;

    lv_obj_get_coords(object, &board);
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(UI_COLOR_TRACK);
    line.opa = LV_OPA_30;
    line.width = 1;
    for (column = 1U; column < TETRIS_COLS; column++)
    {
        line.p1.x = board.x1 + 1 + column * TETRIS_CELL_SIZE;
        line.p1.y = board.y1 + 1;
        line.p2.x = line.p1.x;
        line.p2.y = board.y1 + TETRIS_BOARD_HEIGHT;
        lv_draw_line(layer, &line);
    }
    for (row = 1U; row < TETRIS_ROWS; row++)
    {
        line.p1.x = board.x1 + 1;
        line.p1.y = board.y1 + 1 + row * TETRIS_CELL_SIZE;
        line.p2.x = board.x1 + TETRIS_BOARD_WIDTH;
        line.p2.y = line.p1.y;
        lv_draw_line(layer, &line);
    }

    for (row = 0U; row < TETRIS_ROWS; row++)
    {
        for (column = 0U; column < TETRIS_COLS; column++)
        {
            uint8_t cell = tetris_game.board[row][column];

            if (cell != 0U)
            {
                tetris_draw_cell(layer, &board, column, row,
                                 tetris_colors[cell - 1U],
                                 LV_OPA_COVER, 1);
            }
        }
    }

    ghost_y = tetris_game.y;
    while (tetris_piece_fits(tetris_game.piece, tetris_game.rotation,
                             tetris_game.x, (int8_t)(ghost_y + 1)))
        ghost_y++;
    if (ghost_y != tetris_game.y)
    {
        for (block = 0U; block < 4U; block++)
        {
            int16_t x = tetris_game.x +
                        tetris_shapes[tetris_game.piece]
                                      [tetris_game.rotation][block][0];
            int16_t y = ghost_y +
                        tetris_shapes[tetris_game.piece]
                                      [tetris_game.rotation][block][1];

            tetris_draw_cell(layer, &board, x, y,
                             tetris_colors[tetris_game.piece],
                             LV_OPA_30, 3);
        }
    }
    for (block = 0U; block < 4U; block++)
    {
        int16_t x = tetris_game.x +
                    tetris_shapes[tetris_game.piece]
                                  [tetris_game.rotation][block][0];
        int16_t y = tetris_game.y +
                    tetris_shapes[tetris_game.piece]
                                  [tetris_game.rotation][block][1];

        tetris_draw_cell(layer, &board, x, y,
                         tetris_colors[tetris_game.piece],
                         LV_OPA_COVER, 1);
    }
}

static void tetris_preview_draw(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t area;
    lv_draw_rect_dsc_t rect;
    uint8_t block;

    lv_obj_get_coords(object, &area);
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_hex(tetris_colors[tetris_game.next_piece]);
    rect.bg_opa = LV_OPA_COVER;
    rect.radius = 2;
    for (block = 0U; block < 4U; block++)
    {
        int16_t x = tetris_shapes[tetris_game.next_piece][0][block][0];
        int16_t y = tetris_shapes[tetris_game.next_piece][0][block][1];
        lv_area_t cell = {
            .x1 = area.x1 + 13 + x * 16,
            .y1 = area.y1 + 12 + y * 16,
            .x2 = area.x1 + 26 + x * 16,
            .y2 = area.y1 + 25 + y * 16,
        };

        lv_draw_rect(layer, &rect, &cell);
    }
}

static void tetris_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

typedef enum
{
    TETRIS_ACTION_LEFT = 0,
    TETRIS_ACTION_ROTATE,
    TETRIS_ACTION_DOWN,
    TETRIS_ACTION_RIGHT,
    TETRIS_ACTION_DROP,
} tetris_action_t;

static void tetris_action_clicked(lv_event_t *event)
{
    tetris_action_t action = (tetris_action_t)(uintptr_t)
                                 lv_event_get_user_data(event);

    switch (action)
    {
    case TETRIS_ACTION_LEFT:   (void)tetris_move(-1, 0); break;
    case TETRIS_ACTION_ROTATE: tetris_rotate(); break;
    case TETRIS_ACTION_DOWN:   tetris_soft_drop(); break;
    case TETRIS_ACTION_RIGHT:  (void)tetris_move(1, 0); break;
    case TETRIS_ACTION_DROP:   tetris_hard_drop(); break;
    default: break;
    }
}

static void tetris_pause_clicked(lv_event_t *event)
{
    (void)event;
    tetris_toggle_pause();
}

static void tetris_reset_clicked(lv_event_t *event)
{
    (void)event;
    tetris_new_game();
}

static void tetris_add_stat(lv_obj_t *parent, const char *name,
                            lv_obj_t **value, int32_t x, int32_t y)
{
    lv_obj_t *label = tetris_create_label(parent, name,
                                          &lv_font_montserrat_12,
                                          UI_COLOR_MUTED);

    lv_obj_set_pos(label, x, y);
    *value = tetris_create_label(parent, "0", &lv_font_montserrat_18,
                                 UI_COLOR_TEXT);
    lv_obj_set_size(*value, 112, 24);
    lv_obj_set_pos(*value, x, y + 16);
}

static void tetris_page_on_create(ui_page_t *page)
{
    lv_obj_t *back;
    lv_obj_t *label;
    lv_obj_t *button;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    back = tetris_create_icon_button(page->root, 12,
                                     UI_STATUS_BAR_HEIGHT + 4,
                                     44, LV_SYMBOL_LEFT,
                                     UI_COLOR_BACKGROUND,
                                     tetris_back_clicked, RT_NULL);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    label = tetris_create_label(page->root, "TETRIS",
                                &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_pos(label, 72, UI_STATUS_BAR_HEIGHT + 12);
    tetris_state_label = tetris_create_label(
        page->root, "RUNNING", &lv_font_montserrat_14, UI_COLOR_SUCCESS);
    lv_obj_set_size(tetris_state_label, 180, 24);
    lv_obj_set_style_text_align(tetris_state_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(tetris_state_label, 280, UI_STATUS_BAR_HEIGHT + 15);

    tetris_board = lv_obj_create(page->root);
    lv_obj_set_size(tetris_board, TETRIS_BOARD_WIDTH + 2,
                    TETRIS_BOARD_HEIGHT + 2);
    lv_obj_set_pos(tetris_board, TETRIS_BOARD_X, TETRIS_BOARD_Y);
    lv_obj_set_style_bg_color(tetris_board, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(tetris_board, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tetris_board,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(tetris_board, 1, 0);
    lv_obj_set_style_radius(tetris_board, 4, 0);
    lv_obj_set_style_pad_all(tetris_board, 0, 0);
    lv_obj_set_style_shadow_width(tetris_board, 0, 0);
    lv_obj_remove_flag(tetris_board, LV_OBJ_FLAG_SCROLLABLE |
                                     LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tetris_board, tetris_board_draw,
                        LV_EVENT_DRAW_MAIN, RT_NULL);

    tetris_overlay_label = tetris_create_label(
        tetris_board, "", &lv_font_montserrat_20, UI_COLOR_WARNING);
    lv_obj_set_size(tetris_overlay_label, 150, 34);
    lv_obj_set_style_text_align(tetris_overlay_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(tetris_overlay_label);
    lv_obj_add_flag(tetris_overlay_label, LV_OBJ_FLAG_HIDDEN);

    label = tetris_create_label(page->root, "NEXT",
                                &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_pos(label, 218, 106);
    tetris_preview = lv_obj_create(page->root);
    lv_obj_set_size(tetris_preview, 96, 74);
    lv_obj_set_pos(tetris_preview, 218, 128);
    lv_obj_set_style_bg_color(tetris_preview,
                              lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(tetris_preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tetris_preview,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(tetris_preview, 1, 0);
    lv_obj_set_style_radius(tetris_preview, 4, 0);
    lv_obj_set_style_pad_all(tetris_preview, 0, 0);
    lv_obj_set_style_shadow_width(tetris_preview, 0, 0);
    lv_obj_remove_flag(tetris_preview, LV_OBJ_FLAG_SCROLLABLE |
                                       LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tetris_preview, tetris_preview_draw,
                        LV_EVENT_DRAW_MAIN, RT_NULL);

    tetris_add_stat(page->root, "SCORE", &tetris_score_label, 334, 106);
    tetris_add_stat(page->root, "BEST", &tetris_best_label, 334, 154);
    tetris_add_stat(page->root, "LINES", &tetris_lines_label, 218, 218);
    tetris_add_stat(page->root, "LEVEL", &tetris_level_label, 334, 218);

    tetris_create_icon_button(page->root, 218, 326, 44, LV_SYMBOL_LEFT,
                              UI_COLOR_PANEL, tetris_action_clicked,
                              (void *)(uintptr_t)TETRIS_ACTION_LEFT);
    tetris_create_icon_button(page->root, 270, 326, 44, LV_SYMBOL_REFRESH,
                              UI_COLOR_PRIMARY, tetris_action_clicked,
                              (void *)(uintptr_t)TETRIS_ACTION_ROTATE);
    tetris_create_icon_button(page->root, 322, 326, 44, LV_SYMBOL_DOWN,
                              UI_COLOR_PANEL, tetris_action_clicked,
                              (void *)(uintptr_t)TETRIS_ACTION_DOWN);
    tetris_create_icon_button(page->root, 374, 326, 44, LV_SYMBOL_RIGHT,
                              UI_COLOR_PANEL, tetris_action_clicked,
                              (void *)(uintptr_t)TETRIS_ACTION_RIGHT);
    tetris_create_icon_button(page->root, 426, 326, 44, LV_SYMBOL_DOWNLOAD,
                              UI_COLOR_SECONDARY, tetris_action_clicked,
                              (void *)(uintptr_t)TETRIS_ACTION_DROP);

    button = tetris_create_icon_button(page->root, 270, 398, 56,
                                       LV_SYMBOL_PAUSE,
                                       UI_COLOR_SECONDARY,
                                       tetris_pause_clicked, RT_NULL);
    tetris_pause_icon = lv_obj_get_child(button, 0);
    tetris_create_icon_button(page->root, 342, 398, 56,
                              LV_SYMBOL_REFRESH, UI_COLOR_PANEL,
                              tetris_reset_clicked, RT_NULL);
    tetris_new_game();
}

static void tetris_page_on_destroy(ui_page_t *page)
{
    (void)page;
    tetris_board = RT_NULL;
    tetris_preview = RT_NULL;
    tetris_score_label = RT_NULL;
    tetris_best_label = RT_NULL;
    tetris_lines_label = RT_NULL;
    tetris_level_label = RT_NULL;
    tetris_state_label = RT_NULL;
    tetris_overlay_label = RT_NULL;
    tetris_pause_icon = RT_NULL;
    memset(&tetris_game, 0, sizeof(tetris_game));
}

static void tetris_page_on_timer(ui_page_t *page)
{
    (void)page;
    if (!tetris_game.running || tetris_game.paused || tetris_game.game_over)
        return;
    tetris_game.elapsed_ms += TETRIS_TIMER_MS;
    if (tetris_game.elapsed_ms >= tetris_game.drop_ms)
    {
        tetris_game.elapsed_ms = 0U;
        if (!tetris_move(0, 1))
            tetris_lock_piece();
    }
}

static void tetris_page_on_event(ui_page_t *page, const ui_event_t *event)
{
    uint32_t key;

    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    key = (uint32_t)event->data & UI_EVENT_KEY_CODE_MASK;
    if (key == LV_KEY_ESC || key == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
    else if (key == LV_KEY_LEFT || key == '4')
        (void)tetris_move(-1, 0);
    else if (key == LV_KEY_RIGHT || key == '6')
        (void)tetris_move(1, 0);
    else if (key == LV_KEY_UP || key == LV_KEY_PREV || key == '2')
        tetris_rotate();
    else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT || key == '8')
        tetris_soft_drop();
    else if (key == LV_KEY_ENTER)
    {
        if (tetris_game.game_over)
            tetris_new_game();
        else
            tetris_hard_drop();
    }
    else if (key == '5')
        tetris_toggle_pause();
}

rt_err_t tetris_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_TETRIS,
        .app_id = UI_APP_NONE,
        .name = "Tetris Page",
        .on_create = tetris_page_on_create,
        .on_destroy = tetris_page_on_destroy,
        .on_timer = tetris_page_on_timer,
        .on_event = tetris_page_on_event,
        .timer_interval_ms = TETRIS_TIMER_MS,
    };

    return ui_navigation_register_page(&page);
}
