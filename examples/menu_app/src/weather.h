#ifndef __WEATHER_H__
#define __WEATHER_H__

typedef struct
{
    char *txt;//天气现象文字
    char *code;//天气现象代码
    char *temperature;//温度
} user_seniverse_now_config_t;

static uint8_t is_ip_searching;

/**
 * @brief 心知天气（seniverse） 数据结构体
 */
typedef struct
{
    char *id;
    char *name;
    char *country;
    char *path;
    char *timezone;
    char *timezone_offset;
    user_seniverse_now_config_t now_config;
    char *last_update;
} user_seniverse_config_t;


char *get_weather();
int http_weather_data_parse(char *json_data, user_seniverse_config_t *weather_info);


#endif
