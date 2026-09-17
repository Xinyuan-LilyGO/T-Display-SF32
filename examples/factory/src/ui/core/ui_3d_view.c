#include "ui_3d_view.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <ulog.h>

#include "platform_psram.h"
#include "ui_theme.h"

#define UI_3D_VERTEX_COUNT        8U
#define UI_3D_FACE_COUNT          6U
#define UI_3D_FACE_VERTEX_COUNT   4U
#define UI_3D_FRAME_INTERVAL_MS  33U
#define UI_3D_CAMERA_DISTANCE     4.2f
#define UI_3D_AUTO_X_SPEED        0.0025f
#define UI_3D_AUTO_Y_SPEED        0.0100f
#define UI_3D_DRAG_SCALE          0.0120f
#define UI_3D_INERTIA_DECAY       0.92f
#define UI_3D_INERTIA_MIN         0.0005f
#define UI_3D_CANVAS_SIZE        240U
#define UI_3D_CANVAS_FOCAL       276.0f
#define UI_3D_PALETTE_BACKGROUND   0U
#define UI_3D_PALETTE_FACE_BASE    1U
#define UI_3D_PALETTE_EDGE         7U
#define UI_3D_PI                  3.14159265358979323846f
#define UI_3D_TWO_PI              (2.0f * UI_3D_PI)
#define UI_3D_VIEW_MAGIC          0x33445657UL

typedef struct
{
    float x;
    float y;
    float z;
} ui_3d_vec3_t;

typedef struct
{
    uint8_t vertex[UI_3D_FACE_VERTEX_COUNT];
    uint32_t color;
} ui_3d_face_t;

typedef struct
{
    uint8_t face_index;
    uint8_t diagonal;
    float depth;
    float light;
} ui_3d_visible_face_t;

typedef struct
{
    uint32_t magic;
    lv_obj_t *object;
    lv_obj_t *canvas;
    lv_timer_t *timer;
    void *canvas_allocation;
    lv_draw_buf_t *canvas_draw_buf;
    uint8_t *canvas_pixels;
    uint32_t canvas_stride;
    float angle_x;
    float angle_y;
    float angle_z;
    float velocity_x;
    float velocity_y;
    bool dragging;
    bool auto_rotate;
    bool face_mask_valid;
    uint8_t visible_face_mask;
    uint8_t face_diagonal_mask;
} ui_3d_view_t;

static const ui_3d_vec3_t ui_3d_cube_vertices[UI_3D_VERTEX_COUNT] = {
    {-1.0f, -1.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f},
    {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f},
    { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f},
    {-1.0f,  1.0f,  1.0f},
};

static const ui_3d_face_t ui_3d_cube_faces[UI_3D_FACE_COUNT] = {
    {{4U, 5U, 6U, 7U}, UI_COLOR_WARM},
    {{1U, 0U, 3U, 2U}, UI_COLOR_PRIMARY},
    {{0U, 4U, 7U, 3U}, UI_COLOR_SECONDARY},
    {{5U, 1U, 2U, 6U}, 0x21B8FFU},
    {{3U, 7U, 6U, 2U}, 0xA66CFFU},
    {{0U, 1U, 5U, 4U}, 0xF0CF61U},
};

static void ui_3d_view_event(lv_event_t *event);

static ui_3d_view_t *ui_3d_view_get_state(const lv_obj_t *object)
{
    ui_3d_view_t *view;

    if (object == RT_NULL)
        return RT_NULL;
    view = (ui_3d_view_t *)lv_obj_get_user_data((lv_obj_t *)object);
    if (view == RT_NULL || view->magic != UI_3D_VIEW_MAGIC ||
            view->object != object)
        return RT_NULL;
    return view;
}

static float ui_3d_wrap_angle(float angle)
{
    if (angle > UI_3D_TWO_PI)
        angle -= UI_3D_TWO_PI;
    else if (angle < -UI_3D_TWO_PI)
        angle += UI_3D_TWO_PI;
    return angle;
}

static ui_3d_vec3_t ui_3d_subtract(ui_3d_vec3_t a,
                                    ui_3d_vec3_t b)
{
    ui_3d_vec3_t result = {
        a.x - b.x,
        a.y - b.y,
        a.z - b.z,
    };

    return result;
}

static ui_3d_vec3_t ui_3d_cross(ui_3d_vec3_t a, ui_3d_vec3_t b)
{
    ui_3d_vec3_t result = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };

    return result;
}

static float ui_3d_dot(ui_3d_vec3_t a, ui_3d_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* LVGL's software rasterizer cannot reliably mask collinear triangles. */
static int64_t ui_3d_triangle_area2(const lv_point_precise_t *a,
                                    const lv_point_precise_t *b,
                                    const lv_point_precise_t *c)
{
    int64_t ab_x = (int64_t)(int32_t)b->x - (int64_t)(int32_t)a->x;
    int64_t ab_y = (int64_t)(int32_t)b->y - (int64_t)(int32_t)a->y;
    int64_t ac_x = (int64_t)(int32_t)c->x - (int64_t)(int32_t)a->x;
    int64_t ac_y = (int64_t)(int32_t)c->y - (int64_t)(int32_t)a->y;

    return ab_x * ac_y - ab_y * ac_x;
}

static bool ui_3d_choose_face_diagonal(const lv_point_precise_t *points,
                                       const ui_3d_face_t *face,
                                       int64_t areas[4],
                                       bool keep_previous,
                                       uint8_t *diagonal)
{
    bool diagonal_02_valid;
    bool diagonal_13_valid;
    int64_t diagonal_02_min;
    int64_t diagonal_13_min;

    areas[0] = ui_3d_triangle_area2(
        &points[face->vertex[0]], &points[face->vertex[1]],
        &points[face->vertex[2]]);
    areas[1] = ui_3d_triangle_area2(
        &points[face->vertex[0]], &points[face->vertex[2]],
        &points[face->vertex[3]]);
    areas[2] = ui_3d_triangle_area2(
        &points[face->vertex[0]], &points[face->vertex[1]],
        &points[face->vertex[3]]);
    areas[3] = ui_3d_triangle_area2(
        &points[face->vertex[1]], &points[face->vertex[2]],
        &points[face->vertex[3]]);

    diagonal_02_valid = areas[0] > 0LL && areas[1] > 0LL;
    diagonal_13_valid = areas[2] > 0LL && areas[3] > 0LL;
    if (!diagonal_02_valid && !diagonal_13_valid)
        return false;
    if (keep_previous &&
            ((*diagonal == 0U && diagonal_02_valid) ||
             (*diagonal != 0U && diagonal_13_valid)))
        return true;

    diagonal_02_min = areas[0] < areas[1] ? areas[0] : areas[1];
    diagonal_13_min = areas[2] < areas[3] ? areas[2] : areas[3];
    *diagonal = diagonal_13_valid &&
                (!diagonal_02_valid || diagonal_13_min > diagonal_02_min)
                    ? 1U : 0U;

    return true;
}

static void ui_3d_log_face_changes(ui_3d_view_t *view,
                                   const lv_point_precise_t *points,
                                   const int64_t areas[UI_3D_FACE_COUNT][4],
                                   uint8_t visible_mask,
                                   uint8_t diagonal_mask)
{
    uint8_t changed_mask = view->face_mask_valid
                               ? (uint8_t)(view->visible_face_mask ^
                                           visible_mask)
                               : (uint8_t)((1U << UI_3D_FACE_COUNT) - 1U);
    uint32_t index;

    if (view->face_mask_valid)
        changed_mask |= (uint8_t)((view->face_diagonal_mask ^
                                   diagonal_mask) & visible_mask);
    if (changed_mask == 0U)
        return;

    // LOG_I("3D face mask: 0x%02x -> 0x%02x diag=0x%02x "
    //       "angle_mrad=(%d,%d,%d)",
    //       view->face_mask_valid ? view->visible_face_mask : 0U,
    //       visible_mask, diagonal_mask,
    //       (int)(view->angle_x * 1000.0f),
    //       (int)(view->angle_y * 1000.0f),
    //       (int)(view->angle_z * 1000.0f));
    for (index = 0U; index < UI_3D_FACE_COUNT; index++)
    {
        const ui_3d_face_t *face;

        if ((changed_mask & (1U << index)) == 0U)
            continue;
        face = &ui_3d_cube_faces[index];
        // LOG_I("3D face=%u %s d=%s area02=%d/%d area13=%d/%d "
        //       "p=(%d,%d)(%d,%d)(%d,%d)(%d,%d)",
        //       (unsigned int)index,
        //       (visible_mask & (1U << index)) != 0U ? "show" : "hide",
        //       (diagonal_mask & (1U << index)) != 0U ? "13" : "02",
        //       (int)areas[index][0], (int)areas[index][1],
        //       (int)areas[index][2], (int)areas[index][3],
        //       (int)points[face->vertex[0]].x,
        //       (int)points[face->vertex[0]].y,
        //       (int)points[face->vertex[1]].x,
        //       (int)points[face->vertex[1]].y,
        //       (int)points[face->vertex[2]].x,
        //       (int)points[face->vertex[2]].y,
        //       (int)points[face->vertex[3]].x,
        //       (int)points[face->vertex[3]].y);
    }
    view->visible_face_mask = visible_mask;
    view->face_diagonal_mask = diagonal_mask;
    view->face_mask_valid = true;
}

static ui_3d_vec3_t ui_3d_rotate_vertex(ui_3d_vec3_t vertex,
                                        float sx, float cx,
                                        float sy, float cy,
                                        float sz, float cz)
{
    ui_3d_vec3_t rotated_x;
    ui_3d_vec3_t rotated_y;
    ui_3d_vec3_t rotated_z;

    rotated_x.x = vertex.x;
    rotated_x.y = vertex.y * cx - vertex.z * sx;
    rotated_x.z = vertex.y * sx + vertex.z * cx;

    rotated_y.x = rotated_x.x * cy + rotated_x.z * sy;
    rotated_y.y = rotated_x.y;
    rotated_y.z = -rotated_x.x * sy + rotated_x.z * cy;

    rotated_z.x = rotated_y.x * cz - rotated_y.y * sz;
    rotated_z.y = rotated_y.x * sz + rotated_y.y * cz;
    rotated_z.z = rotated_y.z;
    return rotated_z;
}

static lv_color_t ui_3d_shade_color(uint32_t color, float light)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (light < 0.0f)
        light = 0.0f;
    else if (light > 1.0f)
        light = 1.0f;
    red = (uint8_t)((float)((color >> 16) & 0xFFU) * light);
    green = (uint8_t)((float)((color >> 8) & 0xFFU) * light);
    blue = (uint8_t)((float)(color & 0xFFU) * light);
    return lv_color_make(red, green, blue);
}

static void ui_3d_sort_faces(ui_3d_visible_face_t *faces,
                             uint32_t count)
{
    uint32_t index;

    for (index = 1U; index < count; index++)
    {
        ui_3d_visible_face_t current = faces[index];
        uint32_t position = index;

        while (position > 0U &&
                faces[position - 1U].depth < current.depth)
        {
            faces[position] = faces[position - 1U];
            position--;
        }
        faces[position] = current;
    }
}

static int32_t ui_3d_edge_value(const lv_point_precise_t *a,
                                const lv_point_precise_t *b,
                                int32_t x, int32_t y)
{
    return ((int32_t)b->x - (int32_t)a->x) *
               (y - (int32_t)a->y) -
           ((int32_t)b->y - (int32_t)a->y) *
               (x - (int32_t)a->x);
}

static void ui_3d_canvas_set_pixel(ui_3d_view_t *view,
                                   int32_t x, int32_t y,
                                   uint8_t palette_index)
{
    if (x < 0 || y < 0 || x >= (int32_t)UI_3D_CANVAS_SIZE ||
            y >= (int32_t)UI_3D_CANVAS_SIZE)
        return;
    view->canvas_pixels[(uint32_t)y * view->canvas_stride +
                        (uint32_t)x] = palette_index;
}

static void ui_3d_canvas_draw_triangle(ui_3d_view_t *view,
                                       const lv_point_precise_t *a,
                                       const lv_point_precise_t *b,
                                       const lv_point_precise_t *c,
                                       uint8_t palette_index)
{
    int32_t min_x = LV_MIN3((int32_t)a->x, (int32_t)b->x,
                            (int32_t)c->x);
    int32_t max_x = LV_MAX3((int32_t)a->x, (int32_t)b->x,
                            (int32_t)c->x);
    int32_t min_y = LV_MIN3((int32_t)a->y, (int32_t)b->y,
                            (int32_t)c->y);
    int32_t max_y = LV_MAX3((int32_t)a->y, (int32_t)b->y,
                            (int32_t)c->y);
    int32_t edge_ab_step_x = -((int32_t)b->y - (int32_t)a->y);
    int32_t edge_bc_step_x = -((int32_t)c->y - (int32_t)b->y);
    int32_t edge_ca_step_x = -((int32_t)a->y - (int32_t)c->y);
    int32_t edge_ab_step_y = (int32_t)b->x - (int32_t)a->x;
    int32_t edge_bc_step_y = (int32_t)c->x - (int32_t)b->x;
    int32_t edge_ca_step_y = (int32_t)a->x - (int32_t)c->x;
    int32_t edge_ab_row;
    int32_t edge_bc_row;
    int32_t edge_ca_row;
    int32_t y;

    if (min_x < 0)
        min_x = 0;
    if (min_y < 0)
        min_y = 0;
    if (max_x >= (int32_t)UI_3D_CANVAS_SIZE)
        max_x = (int32_t)UI_3D_CANVAS_SIZE - 1;
    if (max_y >= (int32_t)UI_3D_CANVAS_SIZE)
        max_y = (int32_t)UI_3D_CANVAS_SIZE - 1;

    edge_ab_row = ui_3d_edge_value(a, b, min_x, min_y);
    edge_bc_row = ui_3d_edge_value(b, c, min_x, min_y);
    edge_ca_row = ui_3d_edge_value(c, a, min_x, min_y);
    for (y = min_y; y <= max_y; y++)
    {
        int32_t edge_ab = edge_ab_row;
        int32_t edge_bc = edge_bc_row;
        int32_t edge_ca = edge_ca_row;
        int32_t x;

        for (x = min_x; x <= max_x; x++)
        {
            if (edge_ab >= 0 && edge_bc >= 0 && edge_ca >= 0)
                ui_3d_canvas_set_pixel(view, x, y, palette_index);
            edge_ab += edge_ab_step_x;
            edge_bc += edge_bc_step_x;
            edge_ca += edge_ca_step_x;
        }
        edge_ab_row += edge_ab_step_y;
        edge_bc_row += edge_bc_step_y;
        edge_ca_row += edge_ca_step_y;
    }
}

static void ui_3d_canvas_draw_face(ui_3d_view_t *view,
                                   const lv_point_precise_t *points,
                                   const ui_3d_face_t *face,
                                   uint8_t diagonal,
                                   uint8_t palette_index)
{
    if (diagonal == 0U)
    {
        ui_3d_canvas_draw_triangle(
            view, &points[face->vertex[0]], &points[face->vertex[1]],
            &points[face->vertex[2]], palette_index);
        ui_3d_canvas_draw_triangle(
            view, &points[face->vertex[0]], &points[face->vertex[2]],
            &points[face->vertex[3]], palette_index);
    }
    else
    {
        ui_3d_canvas_draw_triangle(
            view, &points[face->vertex[0]], &points[face->vertex[1]],
            &points[face->vertex[3]], palette_index);
        ui_3d_canvas_draw_triangle(
            view, &points[face->vertex[1]], &points[face->vertex[2]],
            &points[face->vertex[3]], palette_index);
    }
}

static void ui_3d_canvas_draw_line(ui_3d_view_t *view,
                                   lv_point_precise_t start,
                                   lv_point_precise_t end,
                                   uint8_t palette_index)
{
    int32_t x = (int32_t)start.x;
    int32_t y = (int32_t)start.y;
    int32_t end_x = (int32_t)end.x;
    int32_t end_y = (int32_t)end.y;
    int32_t dx = LV_ABS(end_x - x);
    int32_t sx = x < end_x ? 1 : -1;
    int32_t dy = -LV_ABS(end_y - y);
    int32_t sy = y < end_y ? 1 : -1;
    int32_t error = dx + dy;

    while (true)
    {
        int32_t doubled_error;

        ui_3d_canvas_set_pixel(view, x, y, palette_index);
        ui_3d_canvas_set_pixel(view, x + 1, y, palette_index);
        ui_3d_canvas_set_pixel(view, x, y + 1, palette_index);
        if (x == end_x && y == end_y)
            break;
        doubled_error = 2 * error;
        if (doubled_error >= dy)
        {
            error += dy;
            x += sx;
        }
        if (doubled_error <= dx)
        {
            error += dx;
            y += sy;
        }
    }
}

static void ui_3d_canvas_draw_face_edges(
    ui_3d_view_t *view, const lv_point_precise_t *points,
    const ui_3d_face_t *face,
    bool drawn_edges[UI_3D_VERTEX_COUNT][UI_3D_VERTEX_COUNT])
{
    uint32_t edge;

    for (edge = 0U; edge < UI_3D_FACE_VERTEX_COUNT; edge++)
    {
        uint8_t first = face->vertex[edge];
        uint8_t second = face->vertex[(edge + 1U) %
                                      UI_3D_FACE_VERTEX_COUNT];
        uint8_t low = first < second ? first : second;
        uint8_t high = first < second ? second : first;

        if (drawn_edges[low][high])
            continue;
        drawn_edges[low][high] = true;
        ui_3d_canvas_draw_line(view, points[first], points[second],
                               UI_3D_PALETTE_EDGE);
    }
}

static void ui_3d_draw_face(lv_layer_t *layer,
                            const lv_point_precise_t *points,
                            const ui_3d_face_t *face,
                            uint8_t diagonal,
                            lv_color_t color)
{
    lv_draw_triangle_dsc_t triangle;

    lv_draw_triangle_dsc_init(&triangle);
    triangle.bg_color = color;
    triangle.bg_opa = LV_OPA_COVER;
    if (diagonal == 0U)
    {
        triangle.p[0] = points[face->vertex[0]];
        triangle.p[1] = points[face->vertex[1]];
        triangle.p[2] = points[face->vertex[2]];
        lv_draw_triangle(layer, &triangle);

        triangle.p[0] = points[face->vertex[0]];
        triangle.p[1] = points[face->vertex[2]];
        triangle.p[2] = points[face->vertex[3]];
    }
    else
    {
        triangle.p[0] = points[face->vertex[0]];
        triangle.p[1] = points[face->vertex[1]];
        triangle.p[2] = points[face->vertex[3]];
        lv_draw_triangle(layer, &triangle);

        triangle.p[0] = points[face->vertex[1]];
        triangle.p[1] = points[face->vertex[2]];
        triangle.p[2] = points[face->vertex[3]];
    }
    lv_draw_triangle(layer, &triangle);
}

static void ui_3d_draw_face_edges(lv_layer_t *layer,
                                  const lv_point_precise_t *points,
                                  const ui_3d_face_t *face,
                                  bool drawn_edges[UI_3D_VERTEX_COUNT]
                                                  [UI_3D_VERTEX_COUNT])
{
    lv_draw_line_dsc_t line;
    uint32_t edge;

    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(UI_COLOR_TEXT);
    line.opa = LV_OPA_80;
    line.width = 2;
    line.round_start = 1;
    line.round_end = 1;
    for (edge = 0U; edge < UI_3D_FACE_VERTEX_COUNT; edge++)
    {
        uint8_t first = face->vertex[edge];
        uint8_t second = face->vertex[(edge + 1U) %
                                      UI_3D_FACE_VERTEX_COUNT];
        uint8_t low = first < second ? first : second;
        uint8_t high = first < second ? second : first;

        if (drawn_edges[low][high])
            continue;
        drawn_edges[low][high] = true;
        line.p1 = points[first];
        line.p2 = points[second];
        lv_draw_line(layer, &line);
    }
}

static void ui_3d_view_draw(ui_3d_view_t *view, lv_layer_t *layer)
{
    static const ui_3d_vec3_t light_direction = {
        -0.365f, 0.548f, -0.753f,
    };
    ui_3d_vec3_t vertices[UI_3D_VERTEX_COUNT];
    lv_point_precise_t points[UI_3D_VERTEX_COUNT];
    ui_3d_visible_face_t visible_faces[UI_3D_FACE_COUNT];
    int64_t face_areas[UI_3D_FACE_COUNT][4];
    bool drawn_edges[UI_3D_VERTEX_COUNT][UI_3D_VERTEX_COUNT] = {{false}};
    lv_area_t coordinates;
    float sx = sinf(view->angle_x);
    float cx = cosf(view->angle_x);
    float sy = sinf(view->angle_y);
    float cy = cosf(view->angle_y);
    float sz = sinf(view->angle_z);
    float cz = cosf(view->angle_z);
    float width;
    float height;
    float center_x;
    float center_y;
    float focal;
    uint8_t visible_mask = 0U;
    uint8_t diagonal_mask = 0U;
    uint32_t visible_count = 0U;
    uint32_t index;

    if (view->canvas_pixels != RT_NULL)
    {
        width = (float)UI_3D_CANVAS_SIZE;
        height = (float)UI_3D_CANVAS_SIZE;
        center_x = width * 0.5f;
        center_y = height * 0.5f;
        focal = UI_3D_CANVAS_FOCAL;
    }
    else
    {
        lv_obj_get_coords(view->object, &coordinates);
        width = (float)lv_area_get_width(&coordinates);
        height = (float)lv_area_get_height(&coordinates);
        center_x = (float)coordinates.x1 + width * 0.5f;
        center_y = (float)coordinates.y1 + height * 0.5f;
        focal = (width < height ? width : height) * 0.92f;
    }

    for (index = 0U; index < UI_3D_VERTEX_COUNT; index++)
    {
        float denominator;

        vertices[index] = ui_3d_rotate_vertex(
            ui_3d_cube_vertices[index], sx, cx, sy, cy, sz, cz);
        denominator = UI_3D_CAMERA_DISTANCE + vertices[index].z;
        if (denominator < 0.2f)
            denominator = 0.2f;
        points[index].x = center_x +
                          focal * vertices[index].x / denominator;
        points[index].y = center_y -
                          focal * vertices[index].y / denominator;
    }

    for (index = 0U; index < UI_3D_FACE_COUNT; index++)
    {
        const ui_3d_face_t *face = &ui_3d_cube_faces[index];
        ui_3d_vec3_t edge_a = ui_3d_subtract(
            vertices[face->vertex[1]], vertices[face->vertex[0]]);
        ui_3d_vec3_t edge_b = ui_3d_subtract(
            vertices[face->vertex[2]], vertices[face->vertex[0]]);
        ui_3d_vec3_t normal = ui_3d_cross(edge_a, edge_b);
        ui_3d_vec3_t center = {0.0f, 0.0f, 0.0f};
        float normal_length;
        float diffuse;
        uint8_t diagonal = (view->face_diagonal_mask & (1U << index)) != 0U
                               ? 1U : 0U;
        bool keep_previous = view->face_mask_valid &&
                             (view->visible_face_mask & (1U << index)) != 0U;

        for (uint32_t vertex = 0U;
             vertex < UI_3D_FACE_VERTEX_COUNT; vertex++)
        {
            center.x += vertices[face->vertex[vertex]].x * 0.25f;
            center.y += vertices[face->vertex[vertex]].y * 0.25f;
            center.z += vertices[face->vertex[vertex]].z * 0.25f;
        }
        if (!ui_3d_choose_face_diagonal(points, face,
                                        face_areas[index], keep_previous,
                                        &diagonal))
            continue;

        visible_mask |= (uint8_t)(1U << index);
        if (diagonal != 0U)
            diagonal_mask |= (uint8_t)(1U << index);
        normal_length = sqrtf(ui_3d_dot(normal, normal));
        diffuse = normal_length > 0.0f
                      ? ui_3d_dot(normal, light_direction) / normal_length
                      : 0.0f;
        if (diffuse < 0.0f)
            diffuse = 0.0f;
        visible_faces[visible_count].face_index = (uint8_t)index;
        visible_faces[visible_count].diagonal = diagonal;
        visible_faces[visible_count].depth = center.z;
        visible_faces[visible_count].light = 0.38f + diffuse * 0.62f;
        visible_count++;
    }

    ui_3d_log_face_changes(view, points, face_areas,
                           visible_mask, diagonal_mask);
    ui_3d_sort_faces(visible_faces, visible_count);
    if (view->canvas_pixels != RT_NULL)
    {
        lv_color_t background = lv_obj_get_style_bg_color(
            view->object, LV_PART_MAIN);

        lv_draw_buf_clear(view->canvas_draw_buf, RT_NULL);
        lv_draw_buf_set_palette(
            view->canvas_draw_buf, UI_3D_PALETTE_BACKGROUND,
            lv_color_to_32(background, LV_OPA_COVER));
        lv_draw_buf_set_palette(
            view->canvas_draw_buf, UI_3D_PALETTE_EDGE,
            lv_color_to_32(lv_color_hex(UI_COLOR_TEXT), LV_OPA_COVER));
        for (index = 0U; index < visible_count; index++)
        {
            const ui_3d_visible_face_t *visible = &visible_faces[index];
            const ui_3d_face_t *face =
                &ui_3d_cube_faces[visible->face_index];
            uint8_t palette_index = (uint8_t)(
                UI_3D_PALETTE_FACE_BASE + visible->face_index);

            lv_draw_buf_set_palette(
                view->canvas_draw_buf, palette_index,
                lv_color_to_32(ui_3d_shade_color(face->color,
                                                  visible->light),
                               LV_OPA_COVER));
            ui_3d_canvas_draw_face(view, points, face,
                                   visible->diagonal, palette_index);
        }
        for (index = 0U; index < visible_count; index++)
        {
            const ui_3d_face_t *face =
                &ui_3d_cube_faces[visible_faces[index].face_index];

            ui_3d_canvas_draw_face_edges(view, points, face,
                                         drawn_edges);
        }
        lv_draw_buf_flush_cache(view->canvas_draw_buf, RT_NULL);
        return;
    }

    for (index = 0U; index < visible_count; index++)
    {
        const ui_3d_face_t *face =
            &ui_3d_cube_faces[visible_faces[index].face_index];

        ui_3d_draw_face(layer, points, face,
                        visible_faces[index].diagonal,
                        ui_3d_shade_color(face->color,
                                          visible_faces[index].light));
    }
    for (index = 0U; index < visible_count; index++)
    {
        const ui_3d_face_t *face =
            &ui_3d_cube_faces[visible_faces[index].face_index];

        ui_3d_draw_face_edges(layer, points, face, drawn_edges);
    }
}

static void ui_3d_view_timer(lv_timer_t *timer)
{
    ui_3d_view_t *view = (ui_3d_view_t *)lv_timer_get_user_data(timer);
    bool changed = false;

    if (view == RT_NULL || view->dragging)
        return;

    if (fabsf(view->velocity_x) >= UI_3D_INERTIA_MIN ||
            fabsf(view->velocity_y) >= UI_3D_INERTIA_MIN)
    {
        view->angle_x += view->velocity_x;
        view->angle_y += view->velocity_y;
        view->velocity_x *= UI_3D_INERTIA_DECAY;
        view->velocity_y *= UI_3D_INERTIA_DECAY;
        changed = true;
    }
    else if (view->auto_rotate)
    {
        view->velocity_x = 0.0f;
        view->velocity_y = 0.0f;
        view->angle_x += UI_3D_AUTO_X_SPEED;
        view->angle_y += UI_3D_AUTO_Y_SPEED;
        changed = true;
    }

    if (changed)
    {
        view->angle_x = ui_3d_wrap_angle(view->angle_x);
        view->angle_y = ui_3d_wrap_angle(view->angle_y);
        lv_obj_invalidate(view->object);
    }
}

static bool ui_3d_canvas_init(ui_3d_view_t *view)
{
    uint32_t palette_size =
        LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I8) *
        sizeof(lv_color32_t);
    uint32_t stride = lv_draw_buf_width_to_stride(
        UI_3D_CANVAS_SIZE, LV_COLOR_FORMAT_I8);
    uint32_t buffer_size = palette_size +
                           stride * UI_3D_CANVAS_SIZE +
                           LV_DRAW_BUF_ALIGN;
    void *buffer;

    buffer = t_sf32_psram_calloc(1U, buffer_size);
    if (buffer == RT_NULL)
    {
        LOG_W("3D indexed canvas allocation failed: %u bytes",
              (unsigned int)buffer_size);
        return false;
    }
    view->canvas_allocation = buffer;
    buffer = lv_draw_buf_align(buffer, LV_COLOR_FORMAT_I8);
    view->canvas = lv_canvas_create(view->object);
    if (view->canvas == RT_NULL)
    {
        t_sf32_psram_free(view->canvas_allocation);
        view->canvas_allocation = RT_NULL;
        return false;
    }

    lv_canvas_set_buffer(view->canvas, buffer,
                         UI_3D_CANVAS_SIZE, UI_3D_CANVAS_SIZE,
                         LV_COLOR_FORMAT_I8);
    view->canvas_draw_buf = lv_canvas_get_draw_buf(view->canvas);
    view->canvas_pixels = lv_draw_buf_goto_xy(
        view->canvas_draw_buf, 0U, 0U);
    view->canvas_stride = stride;
    lv_obj_remove_flag(view->canvas,
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(view->canvas);
    return true;
}

static void ui_3d_view_delete(ui_3d_view_t *view)
{
    if (view == RT_NULL)
        return;
    if (view->timer != RT_NULL)
    {
        lv_timer_delete(view->timer);
        view->timer = RT_NULL;
    }
    if (view->object != RT_NULL)
        lv_obj_set_user_data(view->object, RT_NULL);
    if (view->canvas_allocation != RT_NULL)
    {
        t_sf32_psram_free(view->canvas_allocation);
        view->canvas_allocation = RT_NULL;
    }
    view->canvas = RT_NULL;
    view->canvas_draw_buf = RT_NULL;
    view->canvas_pixels = RT_NULL;
    view->magic = 0U;
    view->object = RT_NULL;
    lv_free(view);
}

static void ui_3d_view_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_target_obj(event);
    ui_3d_view_t *view =
        (ui_3d_view_t *)lv_event_get_user_data(event);

    if (view == RT_NULL || view->magic != UI_3D_VIEW_MAGIC)
        return;

    if (code == LV_EVENT_DELETE)
    {
        ui_3d_view_delete(view);
    }
    else if (code == LV_EVENT_DRAW_MAIN)
    {
        ui_3d_view_draw(view, lv_event_get_layer(event));
    }
    else if (code == LV_EVENT_SIZE_CHANGED && view->canvas != RT_NULL)
    {
        lv_obj_center(view->canvas);
    }
    else if (code == LV_EVENT_PRESSED)
    {
        view->dragging = true;
        view->velocity_x = 0.0f;
        view->velocity_y = 0.0f;
        lv_event_stop_bubbling(event);
    }
    else if (code == LV_EVENT_PRESSING)
    {
        lv_indev_t *input = lv_event_get_indev(event);
        lv_point_t vector;

        if (input == RT_NULL)
            input = lv_indev_active();
        if (input == RT_NULL)
            return;
        lv_indev_get_vect(input, &vector);
        view->velocity_x = (float)vector.y * UI_3D_DRAG_SCALE;
        view->velocity_y = (float)vector.x * UI_3D_DRAG_SCALE;
        view->angle_x = ui_3d_wrap_angle(
            view->angle_x + view->velocity_x);
        view->angle_y = ui_3d_wrap_angle(
            view->angle_y + view->velocity_y);
        lv_obj_invalidate(object);
        lv_event_stop_bubbling(event);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        view->dragging = false;
        lv_event_stop_bubbling(event);
    }
}

lv_obj_t *ui_3d_view_create(lv_obj_t *parent)
{
    lv_obj_t *object;
    ui_3d_view_t *view;

    object = lv_obj_create(parent);
    if (object == RT_NULL)
        return RT_NULL;
    view = lv_malloc_zeroed(sizeof(*view));
    if (view == RT_NULL)
    {
        lv_obj_delete(object);
        return RT_NULL;
    }
    view->magic = UI_3D_VIEW_MAGIC;
    view->object = object;
    view->angle_x = -0.42f;
    view->angle_y = 0.62f;
    view->auto_rotate = true;
    lv_obj_set_user_data(object, view);
    lv_obj_add_event_cb(object, ui_3d_view_event, LV_EVENT_ALL, view);
    ui_3d_canvas_init(view);
    view->timer = lv_timer_create(ui_3d_view_timer,
                                  UI_3D_FRAME_INTERVAL_MS, view);
    if (view->timer == RT_NULL)
    {
        lv_obj_delete(object);
        return RT_NULL;
    }
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

void ui_3d_view_set_auto_rotate(lv_obj_t *object, bool enabled)
{
    ui_3d_view_t *view = ui_3d_view_get_state(object);

    if (view == RT_NULL)
        return;
    view->auto_rotate = enabled;
    if (enabled)
    {
        view->velocity_x = 0.0f;
        view->velocity_y = 0.0f;
    }
}

bool ui_3d_view_get_auto_rotate(const lv_obj_t *object)
{
    const ui_3d_view_t *view = ui_3d_view_get_state(object);

    if (view == RT_NULL)
        return false;
    return view->auto_rotate;
}

void ui_3d_view_rotate(lv_obj_t *object, float delta_x, float delta_y)
{
    ui_3d_view_t *view = ui_3d_view_get_state(object);

    if (view == RT_NULL)
        return;
    view->angle_x = ui_3d_wrap_angle(view->angle_x + delta_x);
    view->angle_y = ui_3d_wrap_angle(view->angle_y + delta_y);
    view->velocity_x = 0.0f;
    view->velocity_y = 0.0f;
    lv_obj_invalidate(object);
}

void ui_3d_view_reset(lv_obj_t *object)
{
    ui_3d_view_t *view = ui_3d_view_get_state(object);

    if (view == RT_NULL)
        return;
    view->angle_x = -0.42f;
    view->angle_y = 0.62f;
    view->angle_z = 0.0f;
    view->velocity_x = 0.0f;
    view->velocity_y = 0.0f;
    lv_obj_invalidate(object);
}
