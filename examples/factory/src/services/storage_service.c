#include "storage_service.h"

#include <dfs_fs.h>
#include <dfs_posix.h>
#include <rtdevice.h>
#include <string.h>
#include <ulog.h>

#define STORAGE_DEVICE_NAME    "sd0"
#define STORAGE_MOUNT_PATH     "/"
#define STORAGE_FS_TYPE        "elm"
#define STORAGE_MOUNT_OPTIONS  "codepage=936,utf8=1"
#define STORAGE_DEVICE_WAIT_MS 3000U
#define STORAGE_DEVICE_POLL_MS 50U
#define STORAGE_SCAN_TIMEOUT_MS 800U

static rt_mutex_t storage_lock;
static bool storage_mounted;

static rt_err_t storage_lock_create(void)
{
    if (storage_lock != RT_NULL)
        return RT_EOK;

    storage_lock = rt_mutex_create("storage", RT_IPC_FLAG_PRIO);
    if (storage_lock == RT_NULL)
    {
        LOG_E("Failed to create storage mutex");
        return -RT_ENOMEM;
    }

    return RT_EOK;
}

static rt_device_t storage_wait_for_device(void)
{
    rt_tick_t start_tick = rt_tick_get();
    rt_device_t device;

    do
    {
        device = rt_device_find(STORAGE_DEVICE_NAME);
        if (device != RT_NULL)
            return device;

        rt_thread_mdelay(STORAGE_DEVICE_POLL_MS);
    }
    while ((rt_tick_get() - start_tick) <
           rt_tick_from_millisecond(STORAGE_DEVICE_WAIT_MS));

    return RT_NULL;
}

static rt_err_t storage_mount_locked(void)
{
    if (storage_mounted)
        return RT_EOK;

    if (storage_wait_for_device() == RT_NULL)
    {
        LOG_E("TF block device '%s' not found; check card and SPI MSD config",
              STORAGE_DEVICE_NAME);
        return -RT_ERROR;
    }

    if (dfs_mount(STORAGE_DEVICE_NAME, STORAGE_MOUNT_PATH,
                  STORAGE_FS_TYPE, 0, STORAGE_MOUNT_OPTIONS) != 0)
    {
        LOG_E("Mount '%s' at '%s' failed, errno=%d; card was not formatted",
              STORAGE_DEVICE_NAME, STORAGE_MOUNT_PATH, rt_get_errno());
        return -RT_ERROR;
    }

    storage_mounted = true;
    LOG_I("TF card mounted: %s -> %s (%s)", STORAGE_DEVICE_NAME,
          STORAGE_MOUNT_PATH, STORAGE_FS_TYPE);
    return RT_EOK;
}

rt_err_t storage_service_init(void)
{
    rt_err_t result;

    result = storage_lock_create();
    if (result != RT_EOK)
        return result;

    result = rt_mutex_take(storage_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    result = storage_mount_locked();
    rt_mutex_release(storage_lock);
    return result;
}

rt_err_t storage_service_deinit(void)
{
    rt_err_t result;
    rt_mutex_t lock;

    if (storage_lock == RT_NULL)
        return RT_EOK;

    lock = storage_lock;
    result = rt_mutex_take(lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    if (storage_mounted && dfs_unmount(STORAGE_MOUNT_PATH) != 0)
    {
        LOG_E("Unmount TF card from '%s' failed, errno=%d",
              STORAGE_MOUNT_PATH, rt_get_errno());
        rt_mutex_release(lock);
        return -RT_ERROR;
    }

    storage_mounted = false;
    storage_lock = RT_NULL;
    rt_mutex_release(lock);
    rt_mutex_delete(lock);
    LOG_I("TF card unmounted");
    return RT_EOK;
}

rt_err_t storage_service_format(void)
{
    rt_err_t result;

    result = storage_lock_create();
    if (result != RT_EOK)
        return result;

    result = rt_mutex_take(storage_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    if (storage_wait_for_device() == RT_NULL)
    {
        LOG_E("Cannot format: TF block device '%s' not found",
              STORAGE_DEVICE_NAME);
        result = -RT_ERROR;
        goto exit;
    }

    if (storage_mounted)
    {
        if (dfs_unmount(STORAGE_MOUNT_PATH) != 0)
        {
            LOG_E("Cannot format: unmount '%s' failed, errno=%d",
                  STORAGE_MOUNT_PATH, rt_get_errno());
            result = -RT_ERROR;
            goto exit;
        }
        storage_mounted = false;
    }

    LOG_W("Formatting TF card '%s'; all files will be erased",
          STORAGE_DEVICE_NAME);
    if (dfs_mkfs(STORAGE_FS_TYPE, STORAGE_DEVICE_NAME) != 0)
    {
        LOG_E("Format TF card failed, errno=%d", rt_get_errno());
        result = -RT_ERROR;
        goto exit;
    }

    result = storage_mount_locked();
    if (result == RT_EOK)
        LOG_I("TF card formatted and remounted successfully");

exit:
    rt_mutex_release(storage_lock);
    return result;
}

rt_err_t storage_service_get_info(storage_info_t *info)
{
    struct statfs fs_info;
    rt_err_t result;

    if (info == RT_NULL)
        return -RT_EINVAL;

    memset(info, 0, sizeof(*info));
    if (storage_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(storage_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    if (!storage_mounted)
    {
        result = -RT_ERROR;
        goto exit;
    }

    memset(&fs_info, 0, sizeof(fs_info));
    if (dfs_statfs(STORAGE_MOUNT_PATH, &fs_info) != 0)
    {
        LOG_E("Read TF card filesystem information failed");
        result = -RT_ERROR;
        goto exit;
    }

    info->mounted = true;
    info->block_size = (uint32_t)fs_info.f_bsize;
    info->total_bytes =
        (uint64_t)fs_info.f_blocks * (uint64_t)fs_info.f_bsize;
    info->free_bytes =
        (uint64_t)fs_info.f_bfree * (uint64_t)fs_info.f_bsize;
    result = RT_EOK;

exit:
    rt_mutex_release(storage_lock);
    return result;
}

bool storage_service_is_ready(void)
{
    bool ready;

    if (storage_lock == RT_NULL)
        return false;

    if (rt_mutex_take(storage_lock, RT_WAITING_FOREVER) != RT_EOK)
        return false;

    ready = storage_mounted;
    rt_mutex_release(storage_lock);
    return ready;
}

const char *storage_service_get_mount_path(void)
{
    return STORAGE_MOUNT_PATH;
}

void storage_service_name_to_utf8(const char *source,
                                  char *destination,
                                  size_t destination_size)
{
    if (destination == RT_NULL || destination_size == 0U)
        return;
    if (source == RT_NULL)
    {
        destination[0] = '\0';
        return;
    }
    rt_strncpy(destination, source, destination_size - 1U);
    destination[destination_size - 1U] = '\0';
}

static bool storage_entry_precedes(const storage_entry_t *left,
                                   const storage_entry_t *right)
{
    if (left->is_directory != right->is_directory)
        return left->is_directory;
    return strcmp(left->name, right->name) < 0;
}

static void storage_sort_entries(storage_entry_t *entries, size_t count)
{
    size_t index;

    for (index = 1U; index < count; index++)
    {
        storage_entry_t value = entries[index];
        size_t position = index;

        while (position > 0U &&
                storage_entry_precedes(&value, &entries[position - 1U]))
        {
            entries[position] = entries[position - 1U];
            position--;
        }
        entries[position] = value;
    }
}

static bool storage_path_is_valid(const char *path)
{
    size_t length;

    if (path == RT_NULL || path[0] != '/')
        return false;
    length = strlen(path);
    if (length == 0U || length >= STORAGE_PATH_MAX)
        return false;
    if (strstr(path, "/../") != RT_NULL ||
            (length >= 3U && strcmp(path + length - 3U, "/..") == 0))
    {
        return false;
    }
    return true;
}

rt_err_t storage_service_list_directory(const char *path,
                                        storage_entry_t *entries,
                                        size_t capacity,
                                        size_t *count,
                                        bool *limited)
{
    DIR *directory;
    struct dirent *item;
    rt_tick_t start_tick;
    rt_err_t result;

    if (!storage_path_is_valid(path) || entries == RT_NULL ||
            capacity == 0U || count == RT_NULL || limited == RT_NULL)
    {
        return -RT_EINVAL;
    }

    *count = 0U;
    *limited = false;
    memset(entries, 0, sizeof(*entries) * capacity);
    if (storage_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(storage_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    if (!storage_mounted)
    {
        result = -RT_ERROR;
        goto exit_unlock;
    }

    directory = opendir(path);
    if (directory == RT_NULL)
    {
        LOG_E("Open storage directory '%s' failed, errno=%d",
              path, rt_get_errno());
        result = -RT_ERROR;
        goto exit_unlock;
    }

    start_tick = rt_tick_get();
    while ((item = readdir(directory)) != RT_NULL)
    {
        storage_entry_t *entry;
        char child_path[STORAGE_PATH_MAX];
        struct stat file_info;
        int child_length;

        if ((rt_tick_get() - start_tick) >
                rt_tick_from_millisecond(STORAGE_SCAN_TIMEOUT_MS))
        {
            *limited = true;
            break;
        }
        if (item->d_name[0] == '\0' ||
                strcmp(item->d_name, ".") == 0 ||
                strcmp(item->d_name, "..") == 0)
        {
            continue;
        }
        if (strlen(item->d_name) >= STORAGE_ENTRY_NAME_MAX)
        {
            *limited = true;
            continue;
        }
        if (*count >= capacity)
        {
            *limited = true;
            break;
        }

        entry = &entries[*count];
        rt_strncpy(entry->name, item->d_name,
                   sizeof(entry->name) - 1U);
        entry->name[sizeof(entry->name) - 1U] = '\0';
        rt_strncpy(entry->path_name, item->d_name,
                   sizeof(entry->path_name) - 1U);
        entry->path_name[sizeof(entry->path_name) - 1U] = '\0';
        entry->is_directory = item->d_type == DT_DIR;

        if (strcmp(path, "/") == 0)
            child_length = rt_snprintf(child_path, sizeof(child_path),
                                       "/%s", item->d_name);
        else
            child_length = rt_snprintf(child_path, sizeof(child_path),
                                       "%s/%s", path, item->d_name);
        if (child_length >= 0 &&
                (size_t)child_length < sizeof(child_path) &&
                stat(child_path, &file_info) == 0)
        {
            entry->is_directory = S_ISDIR(file_info.st_mode);
            if (!entry->is_directory && file_info.st_size > 0)
                entry->size_bytes = (uint64_t)file_info.st_size;
        }
        (*count)++;
    }

    closedir(directory);
    storage_sort_entries(entries, *count);
    result = RT_EOK;

exit_unlock:
    rt_mutex_release(storage_lock);
    return result;
}

rt_err_t storage_service_ensure_directory(const char *path)
{
    struct stat file_info;
    rt_err_t result;

    if (!storage_path_is_valid(path) || strcmp(path, "/") == 0)
        return -RT_EINVAL;
    if (storage_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(storage_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    if (!storage_mounted)
    {
        result = -RT_ERROR;
        goto exit_unlock_directory;
    }

    if (stat(path, &file_info) == 0)
    {
        result = S_ISDIR(file_info.st_mode) ? RT_EOK : -RT_ERROR;
        goto exit_unlock_directory;
    }
    if (mkdir(path, 0) != 0)
    {
        LOG_E("Create storage directory '%s' failed, errno=%d",
              path, rt_get_errno());
        result = -RT_ERROR;
        goto exit_unlock_directory;
    }
    result = RT_EOK;

exit_unlock_directory:
    rt_mutex_release(storage_lock);
    return result;
}

rt_err_t storage_service_delete_file(const char *path)
{
    struct stat file_info;
    rt_err_t result;

    if (!storage_path_is_valid(path) || strcmp(path, "/") == 0)
        return -RT_EINVAL;
    if (storage_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(storage_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    if (!storage_mounted || stat(path, &file_info) != 0 ||
            S_ISDIR(file_info.st_mode))
    {
        result = -RT_ERROR;
        goto exit_unlock_delete;
    }
    if (unlink(path) != 0)
    {
        LOG_E("Delete storage file '%s' failed, errno=%d",
              path, rt_get_errno());
        result = -RT_ERROR;
        goto exit_unlock_delete;
    }
    result = RT_EOK;

exit_unlock_delete:
    rt_mutex_release(storage_lock);
    return result;
}
