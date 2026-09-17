#include "ui_keypad.h"

#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)
#if defined(FACTORY_HW_USING_KEYBOARD)
#include "tca8418.h"
#include <string.h>
#else
#include "input_service.h"
#endif

#define UI_KEYPAD_MULTITAP_TIMEOUT_MS 800U

static lv_indev_t *keypad_indev;
static lv_group_t *keypad_group;
static uint32_t keypad_key;
static lv_indev_state_t keypad_state = LV_INDEV_STATE_RELEASED;

#if defined(FACTORY_HW_USING_KEYBOARD)
static lv_obj_t *keypad_last_textarea;
static rt_tick_t keypad_last_tick;
static KeyCode keypad_last_key;
static uint8_t keypad_cycle_index;
static bool keypad_last_uppercase;
static bool keypad_uppercase;
static bool keypad_numeric;

static void ui_keypad_reset_multitap(void)
{
    keypad_last_textarea = RT_NULL;
    keypad_last_tick = 0U;
    keypad_last_key = (KeyCode)0;
    keypad_cycle_index = 0U;
}

static lv_obj_t *ui_keypad_get_textarea(lv_obj_t **keyboard)
{
    lv_obj_t *focused;

    if (keyboard != RT_NULL)
        *keyboard = RT_NULL;
    if (keypad_group == RT_NULL)
        return RT_NULL;

    focused = lv_group_get_focused(keypad_group);
    if (focused == RT_NULL)
        return RT_NULL;
    if (lv_obj_check_type(focused, &lv_textarea_class))
        return focused;
    if (lv_obj_check_type(focused, &lv_keyboard_class))
    {
        if (keyboard != RT_NULL)
            *keyboard = focused;
        return lv_keyboard_get_textarea(focused);
    }
    return RT_NULL;
}

static const char *ui_keypad_get_letters(KeyCode key, char *digit)
{
    switch (key)
    {
    case KEY_2: *digit = '2'; return "abc";
    case KEY_3: *digit = '3'; return "def";
    case KEY_4: *digit = '4'; return "ghi";
    case KEY_5: *digit = '5'; return "jkl";
    case KEY_6: *digit = '6'; return "mno";
    case KEY_7: *digit = '7'; return "pqrs";
    case KEY_8: *digit = '8'; return "tuv";
    case KEY_9: *digit = '9'; return "wxyz";
    default:    *digit = '\0'; return RT_NULL;
    }
}
#endif

static void ui_keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->key = keypad_key;
    data->state = keypad_state;
    data->continue_reading = false;
}

uint32_t ui_keypad_translate_key(uint32_t key_code)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    switch ((KeyCode)key_code)
    {
    case KEY_PREV:
        return LV_KEY_PREV;
    case KEY_NEXT:
        return LV_KEY_NEXT;
    case KEY_ENTER:
        return LV_KEY_ENTER;
    case KEY_ESC:
        return LV_KEY_ESC;
    case KEY_HOME:
        return LV_KEY_HOME;
    case KEY_CLOSE:
        return LV_KEY_BACKSPACE;
    case KEY_0:
        return '0';
    case KEY_1:
        return '1';
    case KEY_2:
        return '2';
    case KEY_3:
        return '3';
    case KEY_4:
        return '4';
    case KEY_5:
        return '5';
    case KEY_6:
        return '6';
    case KEY_7:
        return '7';
    case KEY_8:
        return '8';
    case KEY_9:
        return '9';
    case KEY_MUL:
        return '*';
    case KEY_WELL:
        return '#';
    default:
        return 0U;
    }
#else
    switch (key_code)
    {
    case INPUT_JOYPAD_ESC_PIN:
        return LV_KEY_ESC;
    case INPUT_JOYPAD_HOME_PIN:
        return LV_KEY_HOME;
    case INPUT_JOYPAD_ENTER_PIN:
        return LV_KEY_ENTER;
    case INPUT_JOYPAD_CODE_UP:
        return LV_KEY_UP;
    case INPUT_JOYPAD_CODE_DOWN:
        return LV_KEY_DOWN;
    case INPUT_JOYPAD_CODE_LEFT:
        return LV_KEY_LEFT;
    case INPUT_JOYPAD_CODE_RIGHT:
        return LV_KEY_RIGHT;
    default:
        return 0U;
    }
#endif
}
#else
uint32_t ui_keypad_translate_key(uint32_t key_code)
{
    (void)key_code;
    return 0U;
}
#endif

rt_err_t ui_keypad_init(void)
{
#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)
    if (keypad_indev != RT_NULL)
        return RT_EOK;

    keypad_indev = lv_indev_create();
    if (keypad_indev == RT_NULL)
        return -RT_ENOMEM;

    lv_indev_set_type(keypad_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_mode(keypad_indev, LV_INDEV_MODE_EVENT);
    lv_indev_set_read_cb(keypad_indev, ui_keypad_read);
#endif
    return RT_EOK;
}

void ui_keypad_set_group(lv_group_t *group)
{
#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)
    keypad_group = group;
#if defined(FACTORY_HW_USING_KEYBOARD)
    ui_keypad_reset_multitap();
#endif
    if (keypad_indev != RT_NULL)
        lv_indev_set_group(keypad_indev, group);
#else
    (void)group;
#endif
}

bool ui_keypad_handle_text_key(uint32_t key_code, bool long_pressed)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    KeyCode key = (KeyCode)key_code;
    lv_obj_t *keyboard;
    lv_obj_t *textarea = ui_keypad_get_textarea(&keyboard);
    const char *letters;
    char digit;
    bool uppercase = keypad_uppercase;
    rt_tick_t now;

    (void)long_pressed;
    if (textarea == RT_NULL)
    {
        ui_keypad_reset_multitap();
        return false;
    }

    if (key == KEY_ENTER)
    {
        ui_keypad_reset_multitap();
        if (keyboard != RT_NULL)
        {
            lv_obj_send_event(keyboard, LV_EVENT_READY, RT_NULL);
            return true;
        }
        return false;
    }

    if (key == KEY_CLOSE)
    {
        ui_keypad_reset_multitap();
        lv_textarea_delete_char(textarea);
        return true;
    }

    if (keyboard != RT_NULL)
    {
        lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);

        if (mode == LV_KEYBOARD_MODE_NUMBER)
            keypad_numeric = true;
        else if (mode == LV_KEYBOARD_MODE_TEXT_UPPER)
        {
            keypad_numeric = false;
            uppercase = true;
        }
        else if (mode == LV_KEYBOARD_MODE_TEXT_LOWER)
        {
            keypad_numeric = false;
            uppercase = false;
        }
        keypad_uppercase = uppercase;
    }

    if (key == KEY_MAIL)
    {
        ui_keypad_reset_multitap();
        if (keypad_numeric)
        {
            keypad_numeric = false;
            keypad_uppercase = false;
        }
        else if (!keypad_uppercase)
        {
            keypad_uppercase = true;
        }
        else
        {
            keypad_numeric = true;
        }
        if (keyboard != RT_NULL)
        {
            lv_keyboard_set_mode(
                keyboard, keypad_numeric ? LV_KEYBOARD_MODE_NUMBER :
                (keypad_uppercase ? LV_KEYBOARD_MODE_TEXT_UPPER :
                                    LV_KEYBOARD_MODE_TEXT_LOWER));
        }
        return true;
    }

    if (keypad_numeric)
    {
        char number = '\0';

        switch (key)
        {
        case KEY_0: number = '0'; break;
        case KEY_1: number = '1'; break;
        case KEY_2: number = '2'; break;
        case KEY_3: number = '3'; break;
        case KEY_4: number = '4'; break;
        case KEY_5: number = '5'; break;
        case KEY_6: number = '6'; break;
        case KEY_7: number = '7'; break;
        case KEY_8: number = '8'; break;
        case KEY_9: number = '9'; break;
        case KEY_MUL: number = '*'; break;
        case KEY_WELL: number = '#'; break;
        default: break;
        }
        if (number == '\0')
            return false;
        ui_keypad_reset_multitap();
        lv_textarea_add_char(textarea, (uint32_t)number);
        return true;
    }

    if (key == KEY_1 || key == KEY_MUL || key == KEY_WELL)
    {
        ui_keypad_reset_multitap();
        return true;
    }

    if (key == KEY_0)
    {
        ui_keypad_reset_multitap();
        lv_textarea_add_char(textarea, ' ');
        return true;
    }

    letters = ui_keypad_get_letters(key, &digit);
    if (letters == RT_NULL)
    {
        ui_keypad_reset_multitap();
        return false;
    }
    now = rt_tick_get();
    if (textarea == keypad_last_textarea && key == keypad_last_key &&
        uppercase == keypad_last_uppercase &&
        (now - keypad_last_tick) <=
            rt_tick_from_millisecond(UI_KEYPAD_MULTITAP_TIMEOUT_MS))
    {
        keypad_cycle_index = (uint8_t)((keypad_cycle_index + 1U) %
                                       strlen(letters));
        lv_textarea_delete_char(textarea);
    }
    else
    {
        keypad_cycle_index = 0U;
    }

    digit = letters[keypad_cycle_index];
    if (uppercase)
        digit = (char)(digit - 'a' + 'A');
    lv_textarea_add_char(textarea, (uint32_t)digit);
    keypad_last_textarea = textarea;
    keypad_last_key = key;
    keypad_last_tick = now;
    keypad_last_uppercase = uppercase;
    return true;
#else
    (void)key_code;
    (void)long_pressed;
    return false;
#endif
}

uint32_t ui_keypad_handle_key(uint32_t key_code)
{
#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)
    uint32_t mapped_key = ui_keypad_translate_key(key_code);

    return ui_keypad_handle_mapped_key(mapped_key);
#else
    (void)key_code;
    return 0U;
#endif
}

uint32_t ui_keypad_handle_mapped_key(uint32_t mapped_key)
{
#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)

    if (keypad_indev == RT_NULL || mapped_key == 0U)
        return 0U;

    keypad_key = mapped_key;
    keypad_state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(keypad_indev);
    keypad_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(keypad_indev);
    return mapped_key;
#else
    (void)mapped_key;
    return 0U;
#endif
}
