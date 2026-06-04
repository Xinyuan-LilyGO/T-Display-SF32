#include "sd.h"
#include "bf0_hal.h"
#include "ulog.h"
#include "string.h"
#include "drv_io.h"
#include "ctype.h"

#if RT_USING_DFS
#include "dfs_file.h"
#include "dfs_posix.h"
#endif
#include "spi_msd.h"
#include "drv_flash.h"

#define SD_BUS_NAME "sd0"

/* 文件扫描保护限制 */
#define MAX_SCAN_FILES  200     /* 单次扫描最大文件数 */
#define SCAN_TIMEOUT_MS 3000    /* 单次扫描最大耗时(ms) */

music_node_t *music_list = NULL; // Pointer to the head of the music list
static int music_count = 0;
FileNode *file_list_head = NULL;
uint16_t file_count = 0;
bool sd_card_ready = false;   /* SD卡文件系统是否已挂载就绪 */

rt_err_t sdcard_init(void)
{
    rt_device_t msd = rt_device_find(SD_BUS_NAME);
    if (msd == NULL)
    {
        LOG_E("Error: the flash device name (sd0) is not found.\n");
        return -RT_ERROR;
    }

    if (dfs_mount("sd0", "/", "elm", 0, "codepage=936,utf8=1") != 0) // fs exist
    {
        LOG_E("mount fs on tf card to root fail\n");
        return -RT_ERROR;
    }

    LOG_I("mount fs on tf card to root success\n");
    DIR *dir = opendir("./");
    if (!dir)
    {
        LOG_E("Failed to open: ./");
        return -RT_ERROR;
    }
    closedir(dir);
    const char *text = "The T-Display-SF32 test file has been written. \
                        If you see the file, it indicates that the writing was successful.";
    int fd = open("test.txt", O_CREAT | O_WRONLY, 0644);
    if (fd >= 0)
    {
        size_t len = strlen(text);
        ssize_t written = write(fd, text, len);
        if (written < 0)
        {
            log_d("Failed write test.txt");
        }
        else if ((size_t)written != len)
        {
            log_d("Incomplete writing processing");
        }
        close(fd);
    }
    else
    {
        log_d("Failed to create test.txt");
    }

    sd_card_ready = true;
    return RT_EOK;
}

bool list_files(char *path, int depth)
{
    file_list_head = NULL;
    file_count = 0;

    if (path == NULL || path[0] == '\0')
    {
        LOG_E("Invalid path");
        return false;
    }

    if (!sd_card_ready)
    {
        LOG_W("SD card not ready, skip scan");
        return false;
    }

#if RT_USING_DFS
    struct stat st;
    if (stat(path, &st) != 0)
    {
        LOG_W("Path unavailable (stat failed): %s", path);
        return false;
    }

    rt_tick_t start_tick = rt_tick_get();

    DIR *dir = opendir(path);
    if (!dir)
    {
        LOG_E("Failed to open: %s", path);
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        /* 超时保护：防止SD卡异常或文件过多导致无限阻塞 */
        if (rt_tick_get() - start_tick > rt_tick_from_millisecond(SCAN_TIMEOUT_MS))
        {
            LOG_W("File scan timeout (>%dms), stopped at %d files", SCAN_TIMEOUT_MS, file_count);
            break;
        }

        /* 文件数量上限保护 */
        if (file_count >= MAX_SCAN_FILES)
        {
            LOG_W("File count limit (%d), stopped at %d files", MAX_SCAN_FILES, file_count);
            break;
        }

        // 跳过所有以 '.' 开头的隐藏文件/目录和空名
        if (entry->d_name[0] == '\0' || entry->d_name[0] == '.')
        {
            continue;
        }

        bool is_valid = true;
        for (int i = 0; entry->d_name[i] != '\0'; i++)
        {
            if (!isprint((unsigned char)entry->d_name[i]))
            {
                is_valid = false;
                break;
            }
        }
        if (!is_valid)
        {
            continue;
        }

        // 分配节点
        FileNode *node = (FileNode *)rt_malloc(sizeof(FileNode));
        if (node == NULL)
        {
            LOG_E("Failed to allocate FileNode");
            continue;
        }
        memset(node, 0, sizeof(FileNode));
        node->depth = 0;

        // 生成完整路径
        if (path[0] == '/' && path[1] == '\0')
            rt_snprintf(node->full_path, NAME_LEN, "/%s", entry->d_name);
        else
            rt_snprintf(node->full_path, NAME_LEN, "%s/%s", path, entry->d_name);
        node->full_path[NAME_LEN - 1] = '\0';

        rt_strncpy(node->name, entry->d_name, NAME_LEN - 1);
        node->name[NAME_LEN - 1] = '\0';
        node->is_dir = (entry->d_type == DT_DIR);

        // 头插法
        node->next = file_list_head;
        file_list_head = node;
        file_count++;
    }

    closedir(dir);
    return true;
#else
    LOG_E("DFS not enabled");
    return false;
#endif
}

void free_file_list(void)
{
    FileNode *current = file_list_head;

    while (current != NULL)
    {
        FileNode *temp = current;
        current = current->next;
        rt_free(temp); // 使用 RT-Thread 的内存释放函数
    }
    file_list_head = NULL; // 清空链表头
}

bool list_music_files(void)
{
    if (!sd_card_ready)
    {
        LOG_W("SD card not ready, skip music scan");
        return false;
    }

    struct dirent *dir;
    DIR *d = opendir("/music");

    if (d == NULL)
    {
        LOG_E("Failed to open directory.\n");
        return false;
    }

    rt_tick_t start_tick = rt_tick_get();
    while ((dir = readdir(d)) != NULL)
    {
        if (rt_tick_get() - start_tick > rt_tick_from_millisecond(SCAN_TIMEOUT_MS))
        {
            LOG_W("Music scan timeout, stopping");
            break;
        }

        if (strncmp(dir->d_name, "._", 2) == 0)
        {
            continue;
        }

        if (strstr(dir->d_name, ".mp3") || strstr(dir->d_name, ".wav"))
        {
            const char *prefix = "/music/";
            music_node_t *node = (music_node_t *)rt_malloc(sizeof(music_node_t) + strlen(dir->d_name) + strlen(prefix) + 1);
            if (node == NULL)
            {
                LOG_E("Memory allocation failed for music node.\n");
                closedir(d);
                return false;
            }
            // Construct the full path for the music file
            sprintf(node->music_path, "%s%s", prefix, dir->d_name);
            LOG_I("Found music file: %s\n", node->music_path);
            node->next = music_list;
            music_list = node;
        }
    }

    closedir(d);
    return true;
}

int get_music_list_length(void)
{
    int count = 0;
    music_node_t *current = music_list;

    while (current != NULL)
    {
        count++;
        current = current->next;
    }
    music_count = count;
    return count;
}

int get_music_count(void)
{
    return music_count;
}

music_node_t *get_music_node(int index)
{
    music_node_t *current = music_list;
    int current_index = 0;

    while (current != NULL && current_index < index)
    {
        current = current->next;
        current_index++;
    }
    return current;
}

char *get_music_name(char *path)
{
    char *name = path;

    while (*path != '\0')
    {
        if (*path == '/')
        {
            name = path + 1;
        }
        path++;
    }

    return name;
}