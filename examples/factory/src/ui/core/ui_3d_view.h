#ifndef FACTORY_UI_3D_VIEW_H
#define FACTORY_UI_3D_VIEW_H

#include <stdbool.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_3d_view_create(lv_obj_t *parent);
void ui_3d_view_set_auto_rotate(lv_obj_t *view, bool enabled);
bool ui_3d_view_get_auto_rotate(const lv_obj_t *view);
void ui_3d_view_rotate(lv_obj_t *view, float delta_x, float delta_y);
void ui_3d_view_reset(lv_obj_t *view);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_3D_VIEW_H */
