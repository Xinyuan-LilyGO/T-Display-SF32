#ifndef __SD_H__
#define __SD_H__
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include "rtthread.h"
#include "rtconfig.h"

#define MAX_FILES 50
#define NAME_LEN 255

typedef struct MusicNode
{
    struct MusicNode *next; // Pointer to the next music node
    char music_path[]; // Music path
} music_node_t;

typedef struct FileNode {
    char name[NAME_LEN];
    char full_path[NAME_LEN]; 
    int depth;                // 目录深度（根目录为0）
    uint8_t is_dir;
    struct FileNode *next;
} FileNode;

rt_err_t sdcard_init(void);
bool list_files(char *path, int depth);
bool list_music_files(void);
void free_file_list(void);
int get_music_list_length(void);
music_node_t* get_music_node(int index);
char* get_music_name(char* path);
int get_music_count(void);

extern music_node_t *music_list;
extern FileNode *file_list_head;
extern uint16_t file_count;
extern bool sd_card_ready;
#endif