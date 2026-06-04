#include "gui_main.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define LVLG_DEBUG 1
#if LVLG_DEBUG
#define LVLG_LOG(fmt,...) rt_kprintf("[LVLG] " fmt "\n",##__VA_ARGS__)
#else
#define LVLG_LOG(...)
#endif

/* 声明图像资源（请根据实际图像替换） */
LV_IMAGE_DECLARE(_hua_RGB565A8_120x120);


/*********************
 *      DEFINES
 *********************/
#define ICON_OUTER_RADIUS  (480 / 9)

#define ICON_OUTER_DIAMETER  (ICON_OUTER_RADIUS * 2)
#define ICON_INNER_RADIUS    ((ICON_OUTER_RADIUS * 8) / 9)
#define ICON_INNER_DIAMETER  (ICON_INNER_RADIUS * 2)

#define MAX_APP_ROW_NUM   6
#define MAX_APP_COL_NUM   6

#define ICON_IMG_WIDTH    120
#define ICON_IMG_HEIGHT   120

#define PAGE_SCRL_WIDTH   ((ICON_OUTER_DIAMETER * MAX_APP_COL_NUM) + LV_HOR_RES)
#define PAGE_SCRL_HEIGHT  ((ICON_OUTER_DIAMETER * MAX_APP_ROW_NUM) + LV_VER_RES)

#define C0R0_COORD_X      (PAGE_SCRL_WIDTH >> 1)
#define C0R0_COORD_Y      (0)

#define LIMIT_RECT_WIDTH   (LV_HOR_RES - 16)
#define LIMIT_RECT_HEIGHT  (LV_VER_RES - 20)
#define LIMIT_ROUND_RADIUS (LV_HOR_RES >> 1)

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_obj_t *scr;              /* 主屏幕 */
    lv_obj_t *page;             /* 滚动容器 */
    lv_obj_t **icons;           /* 图标数组 */
    lv_point_t *icon_pivots;    /* 图标基准点数组 */
    lv_obj_t *center_icon;      /* 当前屏幕中心最近的图标 */
    lv_point_t scroll_sum;      /* 滚动累计量 */
    bool scroll_actived;        /* 是否正在滚动 */
    float zoom;                 /* 缩放因子 (0~1) */
    bool is_pressed;            /* 是否按下 */
} honeycomb_menu_t;

static honeycomb_menu_t g_menu;

/**********************
 *  STATIC FUNCTIONS
 **********************/
/**
 * @brief 根据索引计算六边形螺旋布局中的行列坐标
 * @param index  图标索引，0 为中心
 * @param row    输出行坐标 (0 ~ MAX_ROW-1)
 * @param col    输出列坐标 (0 ~ MAX_COL-1)
 * @note  中心点偏移量设为 (MAX_ROW/2, MAX_COL/2)
 */
void get_row_col_by_index(uint16_t index, uint16_t *row, uint16_t *col)
{
    // 索引0直接返回中心
    if (index == 0) {
        *row = MAX_APP_ROW_NUM / 2;
        *col = MAX_APP_COL_NUM / 2;
        return;
    }

    // 第一步：确定 index 位于第几圈（layer）
    uint16_t layer = 0;
    uint16_t total = 0;
    uint16_t layer_icons = 1;  // 第0圈只有1个图标（中心）
    
    while (total + layer_icons <= index) {
        total += layer_icons;
        layer++;
        layer_icons = layer * 6;   // 第n圈的图标数量 = 6*n
    }

    // 第二步：计算在该圈内的偏移量（从该圈起点开始）
    uint16_t offset = index - total;            // 0 ~ layer*6 - 1
    uint16_t edge_len = layer;                  // 每条边的步数（不含起点）
    uint16_t edge_idx = offset / edge_len;      // 在哪条边上 (0~5)
    uint16_t step = offset % edge_len;          // 在该边上的第几步 (0~edge_len-1)

    // 第三步：确定该圈的起点坐标（相对于中心）
    // 第1圈起点：(0, -1)？实际上我们采用立方体坐标或轴向坐标更清晰。
    // 这里使用源文件已验证的起始规则：
    // 第 n 圈的起点在 (col = -n, row = 0) 相对于中心
    int16_t r = 0;
    int16_t c = -layer;

    // 第四步：根据所在边和步数移动坐标
    // 注意：六边形的六条边方向依次为：右下、下、左下、左上、上、右上
    // 但不同实现方向顺序可能不同。这里使用与打印结果匹配的顺序。
    // 参考源文件：
    // case 0: col++; row--; break; // 右上
    // case 1: col++;        break; // 右
    // case 2: row++;        break; // 右下
    // case 3: col--; row++; break; // 左下
    // case 4: col--;        break; // 左
    // case 5: row--;        break; // 左上
    // 我们需要根据 edge_idx 和 step 依次应用移动。

    // 先移动到该边的起始位置（前 edge_idx 条边完整走过）
    for (uint16_t e = 0; e < edge_idx; e++) {
        for (uint16_t s = 0; s < edge_len; s++) {
            switch (e) {
                case 0: c++; r--; break;
                case 1: c++;       break;
                case 2: r++;       break;
                case 3: c--; r++;  break;
                case 4: c--;       break;
                case 5: r--;       break;
            }
        }
    }

    // 再在当前边上移动 step 步
    for (uint16_t s = 0; s < step; s++) {
        switch (edge_idx) {
            case 0: c++; r--; break;
            case 1: c++;       break;
            case 2: r++;       break;
            case 3: c--; r++;  break;
            case 4: c--;       break;
            case 5: r--;       break;
        }
    }

    // 最后加上中心偏移量
    *row = (uint16_t)(r + MAX_APP_ROW_NUM / 2);
    *col = (uint16_t)(c + MAX_APP_COL_NUM / 2);
}

void test_get_row_col_by_index() {
    for (uint16_t i = 0; i < MAX_APP_ROW_NUM * MAX_APP_COL_NUM; i++) {
        uint16_t row, col;
        get_row_col_by_index(i, &row, &col);
        LVLG_LOG("Index %d: row=%d, col=%d", i, row, col);
    }
}

/**********************
 *   PUBLIC FUNCTION
 **********************/
/* 创建蜂窝菜单界面 */
lv_obj_t *honeycomb_menu_create(lv_obj_t *parent)
{
    /* 初始化上下文 */
    memset(&g_menu, 0, sizeof(g_menu));
    g_menu.zoom = 1.0f;

    uint32_t max_icons = MAX_APP_COL_NUM * MAX_APP_ROW_NUM;
    g_menu.icons = lv_malloc(max_icons * sizeof(lv_obj_t *));
    g_menu.icon_pivots = lv_malloc(max_icons * sizeof(lv_point_t));
    
    if (!g_menu.icons || !g_menu.icon_pivots) {
        LV_LOG_ERROR("Failed to allocate memory");
        return NULL;
    }
    
    memset(g_menu.icons, 0, max_icons * sizeof(lv_obj_t *));
    memset(g_menu.icon_pivots, 0, max_icons * sizeof(lv_point_t));

    /* 创建页面容器 */
    g_menu.page = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_menu.page, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_scrollbar_mode(g_menu.page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(g_menu.page, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_menu.page, 0, LV_STATE_DEFAULT);

    /* 配置滚动吸附 */
    lv_obj_set_scroll_snap_x(g_menu.page, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_snap_y(g_menu.page, LV_SCROLL_SNAP_CENTER);

    /* 设置可滚动区域大小 */
    lv_obj_set_scroll_dir(g_menu.page, LV_DIR_ALL);

    for (uint16_t i = 0; i < 10; i++) {
        uint16_t row, col;
        get_row_col_by_index(i, &row, &col);
        LVLG_LOG("Index %d: row=%d, col=%d", i, row, col);
        
    }
    
    return g_menu.page;
}

/* 销毁蜂窝菜单 */
void honeycomb_menu_destroy(void)
{
    if (g_menu.icons) {
        lv_free(g_menu.icons);
        g_menu.icons = NULL;
    }
    if (g_menu.icon_pivots) {
        lv_free(g_menu.icon_pivots);
        g_menu.icon_pivots = NULL;
    }
    if (g_menu.page) {
        lv_obj_delete(g_menu.page);
        g_menu.page = NULL;
    }
}


void gui_main(void)
{
    honeycomb_menu_create(NULL);
}