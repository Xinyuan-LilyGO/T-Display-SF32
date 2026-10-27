#include "weather_service.h"

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <ulog.h>
#include <webclient.h>

#include "bluetooth_service.h"

#define WEATHER_HEADER_SIZE       1024U
#define WEATHER_HTTP_TIMEOUT_MS  12000
#define WEATHER_THREAD_STACK_SIZE 6144U
#define WEATHER_THREAD_PRIORITY   RT_THREAD_PRIORITY_MIDDLE
#define WEATHER_URL_MAX           320U
#define WEATHER_LOCATION_URL \
    "http://ip-api.com/json/?fields=status,message,city,lat,lon"
#define WEATHER_FORECAST_URL \
    "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&" \
    "daily=weather_code,temperature_2m_max,temperature_2m_min&" \
    "forecast_days=5&timezone=auto"

static rt_mutex_t weather_lock;
static rt_thread_t weather_thread;
static weather_service_snapshot_t weather_snapshot;
static bool weather_initialized;

static bool weather_service_copy_string(char *destination,
                                        size_t destination_size,
                                        const cJSON *item)
{
    if (destination == RT_NULL || destination_size == 0U ||
            item == RT_NULL || !cJSON_IsString(item) ||
            item->valuestring == RT_NULL)
        return false;

    rt_snprintf(destination, destination_size, "%s", item->valuestring);
    return true;
}

static rt_err_t weather_service_http_get(const char *url, char **response)
{
    struct webclient_session *session;
    size_t response_length = 0U;
    int status;
    int length;

    if (url == RT_NULL || response == RT_NULL)
        return -RT_EINVAL;
    *response = RT_NULL;
    session = webclient_session_create(WEATHER_HEADER_SIZE);
    if (session == RT_NULL)
        return -RT_ENOMEM;

    webclient_set_timeout(session, WEATHER_HTTP_TIMEOUT_MS);
    status = webclient_get(session, url);
    if (status != 200)
    {
        LOG_W("Weather HTTP request failed: status=%d", status);
        webclient_close(session);
        return -RT_ERROR;
    }
    length = webclient_response(session, (void **)response,
                                &response_length);
    webclient_close(session);
    if (length <= 0 || *response == RT_NULL)
    {
        LOG_W("Weather HTTP response is empty");
        return -RT_EIO;
    }
    return RT_EOK;
}

static void weather_service_format_coordinate(double value,
                                              char *buffer,
                                              size_t buffer_size)
{
    int32_t scaled = (int32_t)(value * 10000.0 +
                               (value >= 0.0 ? 0.5 : -0.5));
    uint32_t magnitude = scaled < 0 ?
                         (uint32_t)(-scaled) : (uint32_t)scaled;

    rt_snprintf(buffer, buffer_size, "%s%u.%04u",
                scaled < 0 ? "-" : "",
                (unsigned int)(magnitude / 10000U),
                (unsigned int)(magnitude % 10000U));
}

static bool weather_service_parse_location(const char *json,
                                           char *city,
                                           size_t city_size,
                                           char *latitude,
                                           size_t latitude_size,
                                           char *longitude,
                                           size_t longitude_size)
{
    cJSON *root;
    cJSON *status;
    cJSON *lat;
    cJSON *lon;
    bool valid = false;

    root = cJSON_Parse(json);
    if (root == RT_NULL)
        return false;
    status = cJSON_GetObjectItem(root, "status");
    lat = cJSON_GetObjectItem(root, "lat");
    lon = cJSON_GetObjectItem(root, "lon");
    if (cJSON_IsString(status) && status->valuestring != RT_NULL &&
            strcmp(status->valuestring, "success") == 0 &&
            cJSON_IsNumber(lat) && cJSON_IsNumber(lon) &&
            weather_service_copy_string(
                city, city_size, cJSON_GetObjectItem(root, "city")))
    {
        weather_service_format_coordinate(lat->valuedouble, latitude,
                                          latitude_size);
        weather_service_format_coordinate(lon->valuedouble, longitude,
                                          longitude_size);
        valid = true;
    }
    cJSON_Delete(root);
    return valid;
}

static const char *weather_service_condition(int code)
{
    if (code == 0)
        return "Clear";
    if (code == 1)
        return "Mainly clear";
    if (code == 2)
        return "Partly cloudy";
    if (code == 3)
        return "Cloudy";
    if (code == 45 || code == 48)
        return "Fog";
    if (code >= 51 && code <= 57)
        return "Drizzle";
    if (code >= 61 && code <= 67)
        return "Rain";
    if (code >= 71 && code <= 77)
        return "Snow";
    if (code >= 80 && code <= 82)
        return "Rain showers";
    if (code == 85 || code == 86)
        return "Snow showers";
    if (code >= 95 && code <= 99)
        return "Thunderstorm";
    return "Unknown";
}

static int16_t weather_service_round_temperature(double value)
{
    return (int16_t)(value + (value >= 0.0 ? 0.5 : -0.5));
}

static bool weather_service_parse_forecast(
    const char *json, weather_service_snapshot_t *snapshot)
{
    cJSON *root;
    cJSON *daily;
    cJSON *dates;
    cJSON *codes;
    cJSON *highs;
    cJSON *lows;
    uint32_t index;

    if (json == RT_NULL || snapshot == RT_NULL)
        return false;

    root = cJSON_Parse(json);
    if (root == RT_NULL)
        return false;
    daily = cJSON_GetObjectItem(root, "daily");
    dates = cJSON_IsObject(daily) ?
            cJSON_GetObjectItem(daily, "time") : RT_NULL;
    codes = cJSON_IsObject(daily) ?
            cJSON_GetObjectItem(daily, "weather_code") : RT_NULL;
    highs = cJSON_IsObject(daily) ?
            cJSON_GetObjectItem(daily, "temperature_2m_max") : RT_NULL;
    lows = cJSON_IsObject(daily) ?
           cJSON_GetObjectItem(daily, "temperature_2m_min") : RT_NULL;
    if (!cJSON_IsArray(dates) || !cJSON_IsArray(codes) ||
            !cJSON_IsArray(highs) || !cJSON_IsArray(lows))
    {
        cJSON_Delete(root);
        return false;
    }

    snapshot->day_count = 0U;
    for (index = 0U; index < WEATHER_SERVICE_FORECAST_DAYS; index++)
    {
        cJSON *date = cJSON_GetArrayItem(dates, (int)index);
        cJSON *code = cJSON_GetArrayItem(codes, (int)index);
        cJSON *high;
        cJSON *low;
        weather_service_day_t *output;

        high = cJSON_GetArrayItem(highs, (int)index);
        low = cJSON_GetArrayItem(lows, (int)index);
        if (!cJSON_IsNumber(code) || !cJSON_IsNumber(high) ||
                !cJSON_IsNumber(low))
            break;
        output = &snapshot->days[index];
        if (!weather_service_copy_string(
                output->date, sizeof(output->date), date))
            break;
        rt_snprintf(output->condition, sizeof(output->condition), "%s",
                    weather_service_condition(code->valueint));
        output->high_c = weather_service_round_temperature(
            high->valuedouble);
        output->low_c = weather_service_round_temperature(
            low->valuedouble);
        snapshot->day_count++;
    }
    cJSON_Delete(root);
    return snapshot->day_count == WEATHER_SERVICE_FORECAST_DAYS;
}

static void weather_service_thread_entry(void *parameter)
{
    weather_service_snapshot_t result;
    char *location_response = RT_NULL;
    char *forecast_response = RT_NULL;
    char latitude[16];
    char longitude[16];
    char forecast_url[WEATHER_URL_MAX];

    (void)parameter;
    memset(&result, 0, sizeof(result));
    result.state = WEATHER_SERVICE_NETWORK_ERROR;

    if (weather_service_http_get(WEATHER_LOCATION_URL,
                                 &location_response) != RT_EOK)
        goto exit;
    if (!weather_service_parse_location(
            location_response, result.city, sizeof(result.city),
            latitude, sizeof(latitude), longitude, sizeof(longitude)))
    {
        LOG_W("IP weather location parse failed");
        result.state = WEATHER_SERVICE_DATA_ERROR;
        goto exit;
    }
    /* The location body is no longer needed while fetching/parsing forecast. */
    web_free(location_response);
    location_response = RT_NULL;
    rt_snprintf(forecast_url, sizeof(forecast_url), WEATHER_FORECAST_URL,
                latitude, longitude);
    if (weather_service_http_get(forecast_url,
                                 &forecast_response) != RT_EOK)
        goto exit;
    if (!weather_service_parse_forecast(forecast_response, &result))
    {
        LOG_W("Five-day weather response parse failed");
        result.state = WEATHER_SERVICE_DATA_ERROR;
        goto exit;
    }
    result.state = WEATHER_SERVICE_READY;

exit:
    if (location_response != RT_NULL)
        web_free(location_response);
    if (forecast_response != RT_NULL)
        web_free(forecast_response);

    rt_mutex_take(weather_lock, RT_WAITING_FOREVER);
    weather_snapshot = result;
    weather_thread = RT_NULL;
    rt_mutex_release(weather_lock);
    if (result.state == WEATHER_SERVICE_READY)
    {
        LOG_I("Weather updated for %s: %u days", result.city,
              result.day_count);
    }
}

rt_err_t weather_service_init(void)
{
    if (weather_initialized)
        return RT_EOK;

    weather_lock = rt_mutex_create("weather", RT_IPC_FLAG_PRIO);
    if (weather_lock == RT_NULL)
        return -RT_ENOMEM;
    memset(&weather_snapshot, 0, sizeof(weather_snapshot));
    weather_snapshot.state = WEATHER_SERVICE_IDLE;
    weather_initialized = true;
    return RT_EOK;
}

rt_err_t weather_service_request(void)
{
    bluetooth_service_status_t bluetooth;
    rt_thread_t thread;
    rt_err_t result;

    if (!weather_initialized || weather_lock == RT_NULL)
        return -RT_ERROR;
    result = bluetooth_service_get_status(&bluetooth);
    if (result != RT_EOK || !bluetooth.pan_enabled ||
            !bluetooth.pan_connected)
    {
        rt_mutex_take(weather_lock, RT_WAITING_FOREVER);
        if (weather_thread == RT_NULL)
            weather_snapshot.state = WEATHER_SERVICE_NO_PAN;
        rt_mutex_release(weather_lock);
        return -RT_ERROR;
    }

    rt_mutex_take(weather_lock, RT_WAITING_FOREVER);
    if (weather_thread != RT_NULL)
    {
        rt_mutex_release(weather_lock);
        return -RT_EBUSY;
    }
    weather_snapshot.state = WEATHER_SERVICE_LOADING;
    thread = rt_thread_create("weather", weather_service_thread_entry,
                              RT_NULL, WEATHER_THREAD_STACK_SIZE,
                              WEATHER_THREAD_PRIORITY, 10U);
    if (thread == RT_NULL)
    {
        weather_snapshot.state = WEATHER_SERVICE_NETWORK_ERROR;
        rt_mutex_release(weather_lock);
        return -RT_ENOMEM;
    }
    weather_thread = thread;
    rt_mutex_release(weather_lock);

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_mutex_take(weather_lock, RT_WAITING_FOREVER);
        weather_thread = RT_NULL;
        weather_snapshot.state = WEATHER_SERVICE_NETWORK_ERROR;
        rt_mutex_release(weather_lock);
        rt_thread_delete(thread);
    }
    return result;
}

rt_err_t weather_service_get_snapshot(weather_service_snapshot_t *snapshot)
{
    if (snapshot == RT_NULL)
        return -RT_EINVAL;
    if (!weather_initialized || weather_lock == RT_NULL)
        return -RT_ERROR;

    rt_mutex_take(weather_lock, RT_WAITING_FOREVER);
    *snapshot = weather_snapshot;
    rt_mutex_release(weather_lock);
    return RT_EOK;
}
