#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "littlevgl2rtt.h"
#include "lvgl.h"
#include "lv_theme_private.h"
#include <stdio.h>

// theme_manager.h

#define LV_COLOR_WHITE LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)
#define LV_COLOR_BLACK LV_COLOR_MAKE(0x00, 0x00, 0x00)
#define LV_COLOR_GREEN LV_COLOR_MAKE(0x26, 0xF2, 0x00)
#define LV_COLOR_GREEN_1 LV_COLOR_MAKE(0x1b, 0xFF, 0x00)
#define LV_COLOR_GARY_1 LV_COLOR_MAKE(0xC9, 0xC9, 0xC9)
#define LV_COLOR_GARY_2 LV_COLOR_MAKE(0x9E, 0x9E, 0x9E)
#define LV_COLOR_TORYBLUE LV_COLOR_MAKE(0x00, 0x55, 0xFF)
#define LV_COLOR_BIGSTONE LV_COLOR_MAKE(0x72, 0x8C, 0x9F)
#define LV_COLOR_MERCURY LV_COLOR_MAKE(0x60, 0x60, 0x68)
#define LV_COLOR_MIRAGE LV_COLOR_MAKE(0x42, 0x42, 0x4E)
#define LV_COLOR_BLUE LV_COLOR_MAKE(0x00, 0x16, 0xFF)


typedef enum
{
    THEME_LIGHT,
    THEME_DARK,
    // THEME_CUSTOM,
    THEME_SIZE
} ThemeType;

typedef struct
{
    uint8_t num_themes;
    lv_color_t primary_color;
    lv_color_t secondary_color;
    lv_color_t bg_color;
    lv_color_t font_color;
    const lv_font_t *font_large;
    const lv_font_t *font_normal;
    const lv_font_t *font_small;
} ThemeConfig;
extern ThemeConfig themes[];

void theme_init(void);
ThemeConfig* theme_get_current(void);
uint8_t theme_get_current_id(void);
void new_theme_init_and_set(ThemeConfig themeconfig);

/********FONTS*********/
LV_FONT_DECLARE(lv_font_Alatsi_Regular_150);
LV_FONT_DECLARE(lv_font_Acme_Regular_50);
LV_FONT_DECLARE(lv_font_Acme_Regular_30);
LV_FONT_DECLARE(lv_font_AguafinaScript_Regular_30);

/********IMAGES*********/
LV_IMG_DECLARE(_werther_bg1_RGB565A8_460x400);
LV_IMG_DECLARE(_weather1_RGB565A8_120x120);
LV_IMG_DECLARE(_hua_RGB565A8_120x120);
LV_IMG_DECLARE(_hua_RGB565A8_100x100);
LV_IMG_DECLARE(_data_RGB565A8_100x100);
LV_IMG_DECLARE(_info_RGB565A8_100x100);
LV_IMG_DECLARE(_music_RGB565A8_100x100);
LV_IMG_DECLARE(_sdcard_RGB565A8_100x100);
LV_IMG_DECLARE(_werther_RGB565A8_100x100);
LV_IMG_DECLARE(_radio_RGB565A8_100x100);
LV_IMG_DECLARE(_hua_RGB565A8_60x60);
LV_IMG_DECLARE(_set_RGB565A8_60x60);
LV_IMG_DECLARE(_blue_RGB565A8_60x60);
LV_IMG_DECLARE(_humidity_RGB565A8_50x50);
LV_IMG_DECLARE(_rain_RGB565A8_50x50);
LV_IMG_DECLARE(_wind_RGB565A8_50x40);
LV_IMG_DECLARE(_hua_RGB565A8_40x40);
LV_IMG_DECLARE(_24gl_pause_RGB565A8_40x40);
LV_IMG_DECLARE(_24gl_play_RGB565A8_40x40);
LV_IMG_DECLARE(_next_music_RGB565A8_40x40);
LV_IMG_DECLARE(_last_music_RGB565A8_40x40);
LV_IMG_DECLARE(_falsh_RGB565A8_20x20);


#ifdef __cplusplus
}
#endif

#endif