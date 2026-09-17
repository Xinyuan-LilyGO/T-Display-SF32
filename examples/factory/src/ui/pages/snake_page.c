#include "snake_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>

#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define SNAKE_COLS             18U
#define SNAKE_ROWS             18U
#define SNAKE_MAX_LENGTH       (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_CELL_SIZE        16
#define SNAKE_BOARD_SIZE       (SNAKE_COLS * SNAKE_CELL_SIZE)
#define SNAKE_BOARD_X          24
#define SNAKE_BOARD_Y          108
#define SNAKE_TIMER_MS         50U
#define SNAKE_START_STEP_MS    230U
#define SNAKE_MIN_STEP_MS      80U
#define SNAKE_SPEED_STEP_MS    6U

typedef enum
{
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_RIGHT,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
} snake_direction_t;

typedef struct
{
    uint8_t x[SNAKE_MAX_LENGTH];
    uint8_t y[SNAKE_MAX_LENGTH];
    uint16_t length;
    uint8_t food_x;
    uint8_t food_y;
    snake_direction_t direction;
    snake_direction_t pending_direction;
    uint32_t score;
    uint32_t elapsed_ms;
    uint32_t step_ms;
    bool running;
    bool paused;
    bool game_over;
    bool won;
} snake_game_t;

static snake_game_t snake_game;
static uint32_t snake_best_score;
static uint32_t snake_random_state = 0x534E414BU;
static lv_obj_t *snake_board;
static lv_obj_t *snake_score_label;
static lv_obj_t *snake_best_label;
static lv_obj_t *snake_length_label;
static lv_obj_t *snake_speed_label;
static lv_obj_t *snake_state_label;
static lv_obj_t *snake_overlay_label;
static lv_obj_t *snake_pause_icon;

static lv_obj_t *snake_create_label(lv_obj_t *parent, const char *text,
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

static void snake_style_button(lv_obj_t *button, uint32_t color)
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

static lv_obj_t *snake_create_icon_button(lv_obj_t *parent,
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
    snake_style_button(button, color);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    icon = snake_create_label(button, symbol, &lv_font_montserrat_20,
                              UI_COLOR_TEXT);
    lv_obj_center(icon);
    return button;
}

static uint32_t snake_random(void)
{
    snake_random_state = snake_random_state * 1664525UL + 1013904223UL;
    return snake_random_state;
}

static bool snake_cell_occupied(uint8_t x, uint8_t y)
{
    uint16_t index;

    for (index = 0U; index < snake_game.length; index++)
    {
        if (snake_game.x[index] == x && snake_game.y[index] == y)
            return true;
    }
    return false;
}

static bool snake_place_food(void)
{
    uint16_t attempt;
    uint16_t cell;

    if (snake_game.length >= SNAKE_MAX_LENGTH)
        return false;
    for (attempt = 0U; attempt < SNAKE_MAX_LENGTH; attempt++)
    {
        uint8_t x = (uint8_t)(snake_random() % SNAKE_COLS);
        uint8_t y = (uint8_t)(snake_random() % SNAKE_ROWS);

        if (!snake_cell_occupied(x, y))
        {
            snake_game.food_x = x;
            snake_game.food_y = y;
            return true;
        }
    }

    for (cell = 0U; cell < SNAKE_MAX_LENGTH; cell++)
    {
        uint8_t x = (uint8_t)(cell % SNAKE_COLS);
        uint8_t y = (uint8_t)(cell / SNAKE_COLS);

        if (!snake_cell_occupied(x, y))
        {
            snake_game.food_x = x;
            snake_game.food_y = y;
            return true;
        }
    }
    return false;
}

static void snake_update_ui(void)
{
    if (snake_score_label != RT_NULL)
        lv_label_set_text_fmt(snake_score_label, "%lu",
                              (unsigned long)snake_game.score);
    if (snake_best_label != RT_NULL)
        lv_label_set_text_fmt(snake_best_label, "%lu",
                              (unsigned long)snake_best_score);
    if (snake_length_label != RT_NULL)
        lv_label_set_text_fmt(snake_length_label, "%u",
                              (unsigned int)snake_game.length);
    if (snake_speed_label != RT_NULL)
    {
        uint32_t speed = 1000U / snake_game.step_ms;

        lv_label_set_text_fmt(snake_speed_label, "%lu /s",
                              (unsigned long)speed);
    }
    if (snake_state_label != RT_NULL)
    {
        const char *text = "RUNNING";
        uint32_t color = UI_COLOR_SUCCESS;

        if (snake_game.game_over)
        {
            text = snake_game.won ? "BOARD CLEARED" : "GAME OVER";
            color = snake_game.won ? UI_COLOR_PRIMARY : UI_COLOR_ERROR;
        }
        else if (snake_game.paused)
        {
            text = "PAUSED";
            color = UI_COLOR_WARNING;
        }
        lv_label_set_text(snake_state_label, text);
        lv_obj_set_style_text_color(snake_state_label,
                                    lv_color_hex(color), 0);
    }
    if (snake_overlay_label != RT_NULL)
    {
        if (snake_game.game_over || snake_game.paused)
        {
            lv_label_set_text(snake_overlay_label,
                              snake_game.game_over
                                  ? (snake_game.won ? "CLEARED" : "GAME OVER")
                                  : "PAUSED");
            lv_obj_set_style_text_color(
                snake_overlay_label,
                lv_color_hex(snake_game.game_over
                                 ? (snake_game.won ? UI_COLOR_PRIMARY
                                                   : UI_COLOR_ERROR)
                                 : UI_COLOR_WARNING),
                0);
            lv_obj_remove_flag(snake_overlay_label, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(snake_overlay_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (snake_pause_icon != RT_NULL)
        lv_label_set_text(snake_pause_icon,
                          snake_game.paused ? LV_SYMBOL_PLAY
                                            : LV_SYMBOL_PAUSE);
}

static void snake_new_game(void)
{
    uint16_t index;

    memset(&snake_game, 0, sizeof(snake_game));
    snake_game.length = 4U;
    snake_game.direction = SNAKE_DIR_RIGHT;
    snake_game.pending_direction = SNAKE_DIR_RIGHT;
    snake_game.step_ms = SNAKE_START_STEP_MS;
    snake_game.running = true;
    for (index = 0U; index < snake_game.length; index++)
    {
        snake_game.x[index] = (uint8_t)(8U - index);
        snake_game.y[index] = 9U;
    }
    snake_random_state ^= (uint32_t)rt_tick_get() + snake_best_score;
    (void)snake_place_food();
    snake_update_ui();
    if (snake_board != RT_NULL)
        lv_obj_invalidate(snake_board);
}

static bool snake_direction_is_opposite(snake_direction_t first,
                                        snake_direction_t second)
{
    return ((first == SNAKE_DIR_UP && second == SNAKE_DIR_DOWN) ||
            (first == SNAKE_DIR_DOWN && second == SNAKE_DIR_UP) ||
            (first == SNAKE_DIR_LEFT && second == SNAKE_DIR_RIGHT) ||
            (first == SNAKE_DIR_RIGHT && second == SNAKE_DIR_LEFT));
}

static void snake_set_direction(snake_direction_t direction)
{
    if (snake_game.game_over || snake_game.paused)
        return;
    if (!snake_direction_is_opposite(snake_game.direction, direction))
        snake_game.pending_direction = direction;
}

static void snake_finish(bool won)
{
    snake_game.running = false;
    snake_game.game_over = true;
    snake_game.won = won;
    if (snake_game.score > snake_best_score)
        snake_best_score = snake_game.score;
    snake_update_ui();
}

static void snake_step(void)
{
    int16_t next_x = snake_game.x[0];
    int16_t next_y = snake_game.y[0];
    uint16_t collision_length;
    uint16_t old_length;
    uint16_t index;
    bool grow;

    snake_game.direction = snake_game.pending_direction;
    switch (snake_game.direction)
    {
    case SNAKE_DIR_UP:    next_y--; break;
    case SNAKE_DIR_RIGHT: next_x++; break;
    case SNAKE_DIR_DOWN:  next_y++; break;
    case SNAKE_DIR_LEFT:  next_x--; break;
    default: break;
    }
    if (next_x < 0 || next_x >= (int16_t)SNAKE_COLS ||
            next_y < 0 || next_y >= (int16_t)SNAKE_ROWS)
    {
        snake_finish(false);
        return;
    }

    grow = next_x == snake_game.food_x && next_y == snake_game.food_y;
    collision_length = snake_game.length - (grow ? 0U : 1U);
    for (index = 0U; index < collision_length; index++)
    {
        if (snake_game.x[index] == (uint8_t)next_x &&
                snake_game.y[index] == (uint8_t)next_y)
        {
            snake_finish(false);
            return;
        }
    }

    old_length = snake_game.length;
    if (grow && snake_game.length < SNAKE_MAX_LENGTH)
        snake_game.length++;
    for (index = snake_game.length - 1U; index > 0U; index--)
    {
        uint16_t source = index - 1U;

        if (source >= old_length)
            source = old_length - 1U;
        snake_game.x[index] = snake_game.x[source];
        snake_game.y[index] = snake_game.y[source];
    }
    snake_game.x[0] = (uint8_t)next_x;
    snake_game.y[0] = (uint8_t)next_y;

    if (grow)
    {
        snake_game.score += 10U;
        if (snake_game.step_ms > SNAKE_MIN_STEP_MS + SNAKE_SPEED_STEP_MS)
            snake_game.step_ms -= SNAKE_SPEED_STEP_MS;
        else
            snake_game.step_ms = SNAKE_MIN_STEP_MS;
        if (!snake_place_food())
        {
            snake_finish(true);
            return;
        }
        snake_update_ui();
    }
    if (snake_board != RT_NULL)
        lv_obj_invalidate(snake_board);
}

static void snake_draw_cell(lv_layer_t *layer, const lv_area_t *board,
                            uint8_t x, uint8_t y, uint32_t color,
                            int32_t inset, int32_t radius)
{
    lv_draw_rect_dsc_t rect;
    lv_area_t area;

    area.x1 = board->x1 + 1 + x * SNAKE_CELL_SIZE + inset;
    area.y1 = board->y1 + 1 + y * SNAKE_CELL_SIZE + inset;
    area.x2 = board->x1 + 1 + (x + 1) * SNAKE_CELL_SIZE - 1 - inset;
    area.y2 = board->y1 + 1 + (y + 1) * SNAKE_CELL_SIZE - 1 - inset;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_hex(color);
    rect.bg_opa = LV_OPA_COVER;
    rect.radius = radius;
    lv_draw_rect(layer, &rect, &area);
}

static void snake_board_draw(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_area_t board;
    lv_draw_line_dsc_t line;
    uint16_t index;
    uint8_t grid;

    lv_obj_get_coords(object, &board);
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(UI_COLOR_TRACK);
    line.opa = LV_OPA_30;
    line.width = 1;
    for (grid = 1U; grid < SNAKE_COLS; grid++)
    {
        line.p1.x = board.x1 + 1 + grid * SNAKE_CELL_SIZE;
        line.p1.y = board.y1 + 1;
        line.p2.x = line.p1.x;
        line.p2.y = board.y1 + SNAKE_BOARD_SIZE;
        lv_draw_line(layer, &line);
    }
    for (grid = 1U; grid < SNAKE_ROWS; grid++)
    {
        line.p1.x = board.x1 + 1;
        line.p1.y = board.y1 + 1 + grid * SNAKE_CELL_SIZE;
        line.p2.x = board.x1 + SNAKE_BOARD_SIZE;
        line.p2.y = line.p1.y;
        lv_draw_line(layer, &line);
    }

    snake_draw_cell(layer, &board, snake_game.food_x, snake_game.food_y,
                    UI_COLOR_SECONDARY, 3, LV_RADIUS_CIRCLE);
    for (index = snake_game.length; index > 0U; index--)
    {
        uint16_t segment = index - 1U;

        snake_draw_cell(layer, &board,
                        snake_game.x[segment], snake_game.y[segment],
                        segment == 0U ? UI_COLOR_PRIMARY : UI_COLOR_SUCCESS,
                        segment == 0U ? 1 : 2, 3);
    }
}

static void snake_board_gesture(lv_event_t *event)
{
    lv_indev_t *input = lv_event_get_indev(event);
    lv_dir_t direction;

    if (input == RT_NULL)
        input = lv_indev_active();
    if (input == RT_NULL)
        return;
    direction = lv_indev_get_gesture_dir(input);
    if (direction == LV_DIR_TOP)
        snake_set_direction(SNAKE_DIR_UP);
    else if (direction == LV_DIR_RIGHT)
        snake_set_direction(SNAKE_DIR_RIGHT);
    else if (direction == LV_DIR_BOTTOM)
        snake_set_direction(SNAKE_DIR_DOWN);
    else if (direction == LV_DIR_LEFT)
        snake_set_direction(SNAKE_DIR_LEFT);
    lv_event_stop_bubbling(event);
}

static void snake_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void snake_direction_clicked(lv_event_t *event)
{
    snake_set_direction((snake_direction_t)(uintptr_t)
                            lv_event_get_user_data(event));
}

static void snake_toggle_pause(void)
{
    if (snake_game.game_over)
    {
        snake_new_game();
        return;
    }
    snake_game.paused = !snake_game.paused;
    snake_game.running = !snake_game.paused;
    snake_game.elapsed_ms = 0U;
    snake_update_ui();
}

static void snake_pause_clicked(lv_event_t *event)
{
    (void)event;
    snake_toggle_pause();
}

static void snake_reset_clicked(lv_event_t *event)
{
    (void)event;
    snake_new_game();
}

static void snake_add_stat(lv_obj_t *parent, const char *name,
                           lv_obj_t **value, int32_t y)
{
    lv_obj_t *label = snake_create_label(parent, name,
                                         &lv_font_montserrat_12,
                                         UI_COLOR_MUTED);

    lv_obj_set_pos(label, 332, y);
    *value = snake_create_label(parent, "0", &lv_font_montserrat_20,
                                UI_COLOR_TEXT);
    lv_obj_set_size(*value, 120, 28);
    lv_obj_set_pos(*value, 332, y + 18);
}

static void snake_page_on_create(ui_page_t *page)
{
    lv_obj_t *back;
    lv_obj_t *label;
    lv_obj_t *button;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    back = snake_create_icon_button(page->root, 12,
                                    UI_STATUS_BAR_HEIGHT + 4,
                                    44, LV_SYMBOL_LEFT,
                                    UI_COLOR_BACKGROUND,
                                    snake_back_clicked, RT_NULL);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    label = snake_create_label(page->root, "SNAKE",
                               &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_pos(label, 72, UI_STATUS_BAR_HEIGHT + 12);
    snake_state_label = snake_create_label(
        page->root, "RUNNING", &lv_font_montserrat_14, UI_COLOR_SUCCESS);
    lv_obj_set_size(snake_state_label, 180, 24);
    lv_obj_set_style_text_align(snake_state_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(snake_state_label, 280, UI_STATUS_BAR_HEIGHT + 15);

    snake_board = lv_obj_create(page->root);
    lv_obj_set_size(snake_board, SNAKE_BOARD_SIZE + 2,
                    SNAKE_BOARD_SIZE + 2);
    lv_obj_set_pos(snake_board, SNAKE_BOARD_X, SNAKE_BOARD_Y);
    lv_obj_set_style_bg_color(snake_board, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(snake_board, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(snake_board,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(snake_board, 1, 0);
    lv_obj_set_style_radius(snake_board, 4, 0);
    lv_obj_set_style_pad_all(snake_board, 0, 0);
    lv_obj_set_style_shadow_width(snake_board, 0, 0);
    lv_obj_remove_flag(snake_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(snake_board, snake_board_draw,
                        LV_EVENT_DRAW_MAIN, RT_NULL);
    lv_obj_add_event_cb(snake_board, snake_board_gesture,
                        LV_EVENT_GESTURE, RT_NULL);

    snake_overlay_label = snake_create_label(
        snake_board, "", &lv_font_montserrat_24, UI_COLOR_WARNING);
    lv_obj_set_size(snake_overlay_label, 220, 38);
    lv_obj_set_style_text_align(snake_overlay_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(snake_overlay_label);
    lv_obj_add_flag(snake_overlay_label, LV_OBJ_FLAG_HIDDEN);

    snake_add_stat(page->root, "SCORE", &snake_score_label, 108);
    snake_add_stat(page->root, "BEST", &snake_best_label, 164);
    snake_add_stat(page->root, "LENGTH", &snake_length_label, 220);
    snake_add_stat(page->root, "SPEED", &snake_speed_label, 276);

    snake_create_icon_button(page->root, 374, 326, 40, LV_SYMBOL_UP,
                             UI_COLOR_PANEL, snake_direction_clicked,
                             (void *)(uintptr_t)SNAKE_DIR_UP);
    snake_create_icon_button(page->root, 330, 370, 40, LV_SYMBOL_LEFT,
                             UI_COLOR_PANEL, snake_direction_clicked,
                             (void *)(uintptr_t)SNAKE_DIR_LEFT);
    snake_create_icon_button(page->root, 374, 370, 40, LV_SYMBOL_DOWN,
                             UI_COLOR_PANEL, snake_direction_clicked,
                             (void *)(uintptr_t)SNAKE_DIR_DOWN);
    snake_create_icon_button(page->root, 418, 370, 40, LV_SYMBOL_RIGHT,
                             UI_COLOR_PANEL, snake_direction_clicked,
                             (void *)(uintptr_t)SNAKE_DIR_RIGHT);

    button = snake_create_icon_button(page->root, 336, 420, 52,
                                      LV_SYMBOL_PAUSE, UI_COLOR_SECONDARY,
                                      snake_pause_clicked, RT_NULL);
    snake_pause_icon = lv_obj_get_child(button, 0);
    snake_create_icon_button(page->root, 404, 420, 52,
                             LV_SYMBOL_REFRESH, UI_COLOR_PANEL,
                             snake_reset_clicked, RT_NULL);
    snake_new_game();
}

static void snake_page_on_destroy(ui_page_t *page)
{
    (void)page;
    snake_board = RT_NULL;
    snake_score_label = RT_NULL;
    snake_best_label = RT_NULL;
    snake_length_label = RT_NULL;
    snake_speed_label = RT_NULL;
    snake_state_label = RT_NULL;
    snake_overlay_label = RT_NULL;
    snake_pause_icon = RT_NULL;
    memset(&snake_game, 0, sizeof(snake_game));
}

static void snake_page_on_timer(ui_page_t *page)
{
    (void)page;
    if (!snake_game.running || snake_game.paused || snake_game.game_over)
        return;
    snake_game.elapsed_ms += SNAKE_TIMER_MS;
    if (snake_game.elapsed_ms >= snake_game.step_ms)
    {
        snake_game.elapsed_ms = 0U;
        snake_step();
    }
}

static void snake_page_on_event(ui_page_t *page, const ui_event_t *event)
{
    uint32_t key;

    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    key = (uint32_t)event->data & UI_EVENT_KEY_CODE_MASK;
    if (key == LV_KEY_ESC || key == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
    else if (key == LV_KEY_UP || key == LV_KEY_PREV || key == '2')
        snake_set_direction(SNAKE_DIR_UP);
    else if (key == LV_KEY_RIGHT || key == '6')
        snake_set_direction(SNAKE_DIR_RIGHT);
    else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT || key == '8')
        snake_set_direction(SNAKE_DIR_DOWN);
    else if (key == LV_KEY_LEFT || key == '4')
        snake_set_direction(SNAKE_DIR_LEFT);
    else if (key == LV_KEY_ENTER || key == '5')
        snake_toggle_pause();
}

rt_err_t snake_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_SNAKE,
        .app_id = UI_APP_NONE,
        .name = "Snake Page",
        .on_create = snake_page_on_create,
        .on_destroy = snake_page_on_destroy,
        .on_timer = snake_page_on_timer,
        .on_event = snake_page_on_event,
        .timer_interval_ms = SNAKE_TIMER_MS,
    };

    return ui_navigation_register_page(&page);
}
