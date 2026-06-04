#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include <stdlib.h>
#include "sgm41562b.h"
#include "audio_manager_lib.h"

#if RT_USING_DFS
#include "dfs_file.h"
#include "dfs_posix.h"
#endif
#include "spi_msd.h"
#include "drv_flash.h"

#include "audio_server.h"
#include "audio_mp3ctrl.h"

#define DEFAULT_PCM_FILE "/record.pcm"
#define DEFAULT_WAV_FILE "/record.wav"
#define DEFAULT_MUSIC_DIR "/music"

/* ------------------------------------------------------------------------- */
/* SD 卡初始化                                                               */
/* ------------------------------------------------------------------------- */

rt_err_t sdcard_init(void)
{
    rt_device_t msd = rt_device_find("sd0");
    if (msd == NULL)
    {
        rt_kprintf("Error: sd0 not found\n");
        return -RT_ERROR;
    }
    if (dfs_mount("sd0", "/", "elm", 0, "codepage=936,utf8=1") != 0)
    {
        rt_kprintf("mount fs failed\n");
        return -RT_ERROR;
    }
    rt_kprintf("SD card mounted\n");
    return RT_EOK;
}

/* ------------------------------------------------------------------------- */
/* MSH 命令                                                                  */
/* ------------------------------------------------------------------------- */
void record_start(int argc, char *argv[])
{
    audio_record_start(DEFAULT_WAV_FILE);
}
MSH_CMD_EXPORT(record_start, record start);

void record_stop(int argc, char *argv[])
{
    audio_record_stop();
}
MSH_CMD_EXPORT(record_stop, record stop);

void record_pause(int argc, char *argv[])
{
    audio_record_pause();
}
MSH_CMD_EXPORT(record_pause, record pause);

void record_resume(int argc, char *argv[])
{
    audio_record_resume();
}
MSH_CMD_EXPORT(record_resume, record resume);

void play_start(int argc, char *argv[])
{
    audio_play_start(DEFAULT_WAV_FILE);
}
MSH_CMD_EXPORT(play_start, play start);

void play_stop(int argc, char *argv[])
{
    audio_play_stop();
}
MSH_CMD_EXPORT(play_stop, play stop);

void play_pause(int argc, char *argv[])
{
    audio_play_pause();
}
MSH_CMD_EXPORT(play_pause, play pause);

void play_resume(int argc, char *argv[])
{
    audio_play_resume();
}
MSH_CMD_EXPORT(play_resume, play resume);

/* ------------------------------------------------------------------------- */
/* 主函数                                                                    */
/* ------------------------------------------------------------------------- */
int main(void)
{
    sdcard_init();
    sgm41562b_handle_t charger;
    charger = sgm41562b_init(SGM41562B_I2C_BUS_NAME, SGM41562B_IRQ_PIN);
    if (charger == RT_NULL)
    {
        rt_kprintf("Charger init failed\n");
    }

    bool ret = audio_manager_init();
    if (!ret)
    {
        rt_kprintf("init failed: %d\n", ret);
        return ret;
    }

    // mp3_playlist_scan(DEFAULT_MUSIC_DIR);
    // mp3_play_shuffle_toggle();
    // mp3_play_next();

    // mp3_file_play("/music/abc.mp3");

    /* mic recode,if not sd card, audio_record_start(NULL) */
    rt_kprintf("start record\n");
    audio_record_start(DEFAULT_WAV_FILE);
    rt_thread_mdelay(5000);
    audio_record_stop();
    rt_thread_mdelay(200);

    /* mic play,if not sd card, audio_play_start(NULL) */
    rt_kprintf("start tx\n");
    audio_play_start(DEFAULT_WAV_FILE);
    rt_thread_mdelay(5000);
    audio_play_stop();
    while (1)
    {
        // int db = audio_get_decibel();
        // rt_kprintf("current db: %d dBFS\n", db);
        rt_thread_mdelay(50);
    }
}
