#ifndef FACTORY_STORAGE_SERVICE_H
#define FACTORY_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool mounted;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t block_size;
} storage_info_t;

#define STORAGE_ENTRY_NAME_MAX 192U
#define STORAGE_ENTRY_PATH_NAME_MAX STORAGE_ENTRY_NAME_MAX
#define STORAGE_PATH_MAX       256U

typedef struct
{
    /* Name returned by the RT-Thread DFS directory API. */
    char name[STORAGE_ENTRY_NAME_MAX];
    /* Filesystem path component; currently identical to name. */
    char path_name[STORAGE_ENTRY_PATH_NAME_MAX];
    uint64_t size_bytes;
    bool is_directory;
} storage_entry_t;

rt_err_t storage_service_init(void);
rt_err_t storage_service_deinit(void);

/* Formatting erases all files on the TF card and must be requested explicitly. */
rt_err_t storage_service_format(void);
rt_err_t storage_service_get_info(storage_info_t *info);
rt_err_t storage_service_list_directory(const char *path,
                                        storage_entry_t *entries,
                                        size_t capacity,
                                        size_t *count,
                                        bool *limited);
rt_err_t storage_service_ensure_directory(const char *path);
rt_err_t storage_service_delete_file(const char *path);

bool storage_service_is_ready(void);
const char *storage_service_get_mount_path(void);
void storage_service_name_to_utf8(const char *source,
                                  char *destination,
                                  size_t destination_size);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_STORAGE_SERVICE_H */
