#ifndef FACTORY_UI_KEYPAD_H
#define FACTORY_UI_KEYPAD_H

#include <stdint.h>

#include <lvgl.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t ui_keypad_init(void);
void ui_keypad_set_group(lv_group_t *group);
uint32_t ui_keypad_translate_key(uint32_t key_code);
bool ui_keypad_handle_text_key(uint32_t key_code, bool long_pressed);
uint32_t ui_keypad_handle_mapped_key(uint32_t mapped_key);
uint32_t ui_keypad_handle_key(uint32_t key_code);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_KEYPAD_H */
