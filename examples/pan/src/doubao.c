#include <rtthread.h>
#include <webclient.h>  // 包含 webclient 头文件
#include <cJSON.h>
#include <string.h>
#define API_URL "https://api.doubao.com/v1/chat/completions"  // 豆包 API 地址
#define API_KEY "your_api_key_here"  // 替换为你的 API Key


// 发送 HTTP POST 请求
void webclient_post_request(void)
{
    char *request_data = NULL;
    char *response_data = NULL;
    size_t response_length = 0;

    // 构造请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "doubao-chat");  // 模型名称
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", "你好，请介绍一下你自己。");
    cJSON_AddItemToArray(messages, message);
    request_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    // 设置请求头
    const char *header =
    {
        "Content-Type:application/json\r\n" \
        "Authorization" API_KEY "\r\n"
    };


    // 发送 POST 请求
    if (webclient_request(API_URL, header, request_data, strlen(request_data), (void **)&response_data, &response_length) == 0)
    {
        rt_kprintf("Response: %s\n", response_data);

        // 解析响应
        cJSON *response_json = cJSON_Parse(response_data);
        if (response_json != NULL)
        {
            cJSON *choices = cJSON_GetObjectItem(response_json, "choices");
            if (choices != NULL)
            {
                cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
                if (first_choice != NULL)
                {
                    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
                    if (message != NULL)
                    {
                        rt_kprintf("AI 回复: %s\n", cJSON_GetObjectItem(message, "content")->valuestring);
                    }
                }
            }
            cJSON_Delete(response_json);
        }
    }
    else
    {
        rt_kprintf("请求失败！\n");
    }

    // 释放内存
    if (request_data)
    {
        cJSON_free(request_data);
    }
    if (response_data)
    {
        web_free(response_data);
    }
}

__ROM_USED void doubao(int argc, char **argv)
{
    webclient_post_request();

}
MSH_CMD_EXPORT(doubao, doubao AI application)

