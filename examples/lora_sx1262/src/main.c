#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"

#include "littlevgl2rtt.h"
#include "lvgl.h"
#include "ulog.h"
#include "drv_spi.h"

#include "lora_app.h"

lv_timer_t *send_timer;
lv_obj_t *tx_tile;
lv_obj_t *rx_tile;
lv_obj_t *lora_send_recevice_msg_cont;
lv_obj_t *rx_msg_label;
lv_obj_t *tx_msg_label;
lv_obj_t *rx_rssi_label;
lv_obj_t *rx_snr_label;
static bool lora_send_message = false;
static bool lora_receive_message = false;
lora_rx_info_t rx_info;

static radio_config_t radio_paras =
    {
        .tx_frequency = RF_FREQUENCY,
        .trx_frequency_offset = TX_RX_FREQUENCE_OFFSET,
        .rx_frequency = RF_FREQUENCY + TX_RX_FREQUENCE_OFFSET,

        .txpower = TX_OUTPUT_POWER,

        // lora
        .modem = MODEM_LORA,
        .sf = LORA_SPREADING_FACTOR,
        .bw = LORA_BANDWIDTH,
        .cr = LORA_CODINGRATE,
        .iq_inversion = LORA_IQ_INVERSION_ON_DISABLE,
        .public_network = false,
};

enum
{
    LORA_EVENT_MODE = 0,
    LORA_EVENT_FREQ,
    LORA_EVENT_POWER,
    LORA_EVENT_BW,
    LORA_EVENT_SF,
    LORA_EVENT_CR,
    LORA_EVENT_SYNC,
    LORA_EVENT_SEND_MESSAGE,
    LORA_EVENT_RECEIVE_MESSAGE,
};

static rt_thread_t lvgl_task = RT_NULL;
void lvgl_task_entry(void *parameter);
void lora_test_ui(void);
static void event_cb(lv_event_t *e);
static void send_timer_callback(lv_timer_t *timer);
void radio_rx_callback(lora_rx_info_t *info);

#define IS_DISPLAY 1
#define IS_SEND 1
/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_thread_mdelay(500);
    rt_err_t ret = RT_EOK;
#if IS_DISPLAY
    ret = littlevgl2rtt_init("lcd");
    if (ret != RT_EOK)
    {
        return ret;
    }

    LOG_D("T-Display-SF32!");

    lora_test_ui();

    lvgl_task = rt_thread_create("lvgl_task", lvgl_task_entry, RT_NULL, 8 * 1024, 25, 10);
    if (lvgl_task != RT_NULL)
    {
        rt_thread_startup(lvgl_task);
    }
#endif

    if (lora_app_init() == RT_EOK)
    {
        log_d("lora app init success");
        radio_set_freq(868000000);
        radio_set_outpower(22);
        radio_set_lora_bandwidth(LORA_BANDWIDTH);
        radio_set_lora_sf(LORA_SPREADING_FACTOR_5);
        radio_set_lora_cr(LORA_CODINGRATE_4_5);
        radio_set_lora_preamble(12);
        radio_set_lora_crc(false);
        radio_set_lora_iq(false);
        radio_set_lora_sync_word(false);
        radio_set_rx_boost(true);
        radio_set_rx_callback(radio_rx_callback);
    }

    static int tx_conut = 0;
    while (1)
    {
#if IS_DISPLAY == 0
        lora_rx_info_t rx_info;
        static uint8_t rx_use = 1;
        if (IS_SEND)
        {
            tx_conut++;
            char send_buffer[32];
            snprintf(send_buffer, sizeof(send_buffer), "T-Display-SF32!#%d", tx_conut);
            log_d("send buffer:%s", send_buffer);
            radio_tx((uint8_t *)send_buffer, strlen(send_buffer));
        }
        else if (rx_use == 1)
        {
            radio_rx();
            rx_use = 0;
        }
#endif
        rt_thread_mdelay(1000);
    }
}

void radio_rx_callback(lora_rx_info_t *info)
{
    if (info->data > 0)
    {
        rx_info = *info;
        log_d("rx message(%d bytes): %s, rssi: %d, snr: %d", info->data_len, info->data, info->rssi, info->snr);
    }
    else
    {
        log_d("info message data len error");
    }
    radio_rx();
}

void lora_test_ui(void)
{
    lv_obj_t *src = lv_scr_act();

    lv_obj_t *page_tile = lv_label_create(src);
    lv_obj_set_size(page_tile, LV_PCT(50), 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Lora Config");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_style_t config_cont_style;
    lv_style_init(&config_cont_style);
    lv_style_set_border_width(&config_cont_style, 0);
    lv_style_set_bg_color(&config_cont_style, lv_color_hex(0xc9c9c9));
    lv_style_set_bg_opa(&config_cont_style, LV_OPA_100);
    lv_style_set_pad_all(&config_cont_style, 0);
    lv_style_set_pad_bottom(&config_cont_style, 40);
    lv_style_set_radius(&config_cont_style, 50);

    lv_obj_t *config_cont = lv_obj_create(src);
    lv_obj_set_size(config_cont, 440, 340);
    lv_obj_align(config_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(config_cont, &config_cont_style, 0);

    char *lora_config_label_text[7] = {"Mode", "Freq", "Power", "BW", "SF", "CR", "SyncWord"};
    char *lora_config_ddlist_text[7] = {"LORA",
                                        "868Mhz\n915Mhz",
                                        "22dBm\n15dBm\n10dBm\n5dBm\n0dBm\n-5dBm\n-9dBm",
                                        "62khz\n125kHz\n250kHz\n500kHz",
                                        "5\n6\n7\n8\n9\n10\n11\n12",
                                        "5\n6\n7\n8",
                                        "0x1424"};
    for (int i = 0; i < sizeof(lora_config_label_text) / sizeof(lora_config_label_text[0]); i++)
    {
        lv_obj_t *label = lv_label_create(config_cont);
        lv_obj_set_size(label, 200, 36);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 50 + i * 80);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(label, lora_config_label_text[i]);

        lv_obj_t *lora_mdoe_ddlist = lv_dropdown_create(config_cont);
        lv_obj_set_size(lora_mdoe_ddlist, 200, 50);
        lv_obj_align(lora_mdoe_ddlist, LV_ALIGN_TOP_RIGHT, -40, 40 + i * 80);
        lv_dropdown_set_options(lora_mdoe_ddlist, lora_config_ddlist_text[i]);

        if (i == 3) // 如果是带宽(BW)选项
        {
            lv_dropdown_set_selected(lora_mdoe_ddlist, 1); // 1 对应 "125kHz"
        }

        lv_obj_set_style_text_font(lora_mdoe_ddlist, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(lora_mdoe_ddlist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(lora_mdoe_ddlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(lora_mdoe_ddlist, lv_color_hex(0xe1e6ee), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_side(lora_mdoe_ddlist, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(lora_mdoe_ddlist, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(lora_mdoe_ddlist, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(lora_mdoe_ddlist, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(lora_mdoe_ddlist, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(lora_mdoe_ddlist, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(lora_mdoe_ddlist, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(lora_mdoe_ddlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(lora_mdoe_ddlist, event_cb, LV_EVENT_VALUE_CHANGED, (void *)(LORA_EVENT_MODE + i));
    }

    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
    lv_style_set_radius(&btn_style, 25);
    lv_style_set_pad_all(&btn_style, 0);
    lv_style_set_border_width(&btn_style, 0);
    lv_style_set_shadow_width(&btn_style, 0);

    lv_obj_t *lora_send_btn = lv_btn_create(src);
    lv_obj_set_size(lora_send_btn, 150, 50);
    lv_obj_add_style(lora_send_btn, &btn_style, 0);
    lv_obj_align(lora_send_btn, LV_ALIGN_BOTTOM_LEFT, 50, -15);
    lv_obj_set_style_bg_color(lora_send_btn, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(lora_send_btn, event_cb, LV_EVENT_CLICKED, (void *)(LORA_EVENT_SEND_MESSAGE));
    lv_obj_t *lora_send_label = lv_label_create(lora_send_btn);
    lv_label_set_text(lora_send_label, "Send");
    lv_obj_align(lora_send_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lora_send_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lora_send_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_send_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lora_receive_btn = lv_btn_create(src);
    lv_obj_set_size(lora_receive_btn, 150, 50);
    lv_obj_align(lora_receive_btn, LV_ALIGN_BOTTOM_RIGHT, -50, -15);
    lv_obj_add_style(lora_receive_btn, &btn_style, 0);
    lv_obj_set_style_bg_color(lora_receive_btn, lv_color_hex(0xff9d00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(lora_receive_btn, event_cb, LV_EVENT_CLICKED, (void *)(LORA_EVENT_RECEIVE_MESSAGE));
    lv_obj_t *lora_receive_label = lv_label_create(lora_receive_btn);
    lv_obj_align(lora_receive_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lora_receive_label, "Receive");
    lv_obj_set_style_text_color(lora_receive_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lora_receive_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_receive_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    send_timer = lv_timer_create(send_timer_callback, 1000, NULL); // Trigger every 2000ms
    lv_timer_pause(send_timer);

    lora_send_recevice_msg_cont = lv_obj_create(src);
    lv_obj_set_size(lora_send_recevice_msg_cont, 440, 340);
    lv_obj_align(lora_send_recevice_msg_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(lora_send_recevice_msg_cont, &config_cont_style, 0);
    lv_obj_add_flag(lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(lora_send_recevice_msg_cont, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);

    tx_tile = lv_label_create(lora_send_recevice_msg_cont);
    lv_obj_set_height(tx_tile, 30);
    lv_obj_align(tx_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(tx_tile, "TX");
    lv_obj_set_style_text_font(tx_tile, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(tx_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *tx_time_tile = lv_label_create(lora_send_recevice_msg_cont);
    lv_obj_set_height(tx_time_tile, 30);
    lv_obj_align(tx_time_tile, LV_ALIGN_TOP_RIGHT, -30, 10);
    lv_label_set_text(tx_time_tile, "1s send");
    lv_obj_set_style_text_font(tx_time_tile, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(tx_time_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    tx_msg_label = lv_label_create(lora_send_recevice_msg_cont);
    lv_obj_set_height(tx_msg_label, 25);
    lv_obj_align(tx_msg_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(tx_msg_label, "TX Message:");
    lv_obj_set_style_text_font(tx_msg_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(tx_msg_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    rx_tile = lv_label_create(lora_send_recevice_msg_cont);
    lv_obj_set_height(rx_tile, 25);
    lv_obj_align(rx_tile, LV_ALIGN_TOP_MID, 0, 150);
    lv_label_set_text(rx_tile, "RX");
    lv_obj_set_style_text_font(rx_tile, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(rx_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    rx_msg_label = lv_label_create(lora_send_recevice_msg_cont);
    lv_obj_set_height(rx_msg_label, 25);
    lv_obj_align(rx_msg_label, LV_ALIGN_TOP_MID, 0, 200);
    lv_label_set_text(rx_msg_label, "RX Message: null");
    lv_obj_set_style_text_font(rx_msg_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(rx_msg_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    rx_rssi_label = lv_label_create(lora_send_recevice_msg_cont);
    lv_obj_set_height(rx_rssi_label, 25);
    lv_obj_align(rx_rssi_label, LV_ALIGN_TOP_MID, 0, 230);
    lv_label_set_text(rx_rssi_label, "RX RSSI: null");
    lv_obj_set_style_text_font(rx_rssi_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(rx_rssi_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    rx_snr_label = lv_label_create(lora_send_recevice_msg_cont);
    lv_obj_set_height(rx_snr_label, 25);
    lv_obj_align(rx_snr_label, LV_ALIGN_TOP_MID, 0, 260);
    lv_label_set_text(rx_snr_label, "RX SNR: null");
    lv_obj_set_style_text_font(rx_snr_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(rx_snr_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void lvgl_task_entry(void *parameter)
{
    rt_uint32_t ms;
    rt_uint32_t time;
    while (1)
    {
        ms = lv_task_handler();
        rt_thread_mdelay(ms);
    }
}

static void send_timer_callback(lv_timer_t *timer)
{
    if (lora_send_message)
    {
        static int tx_conut = 0;
        tx_conut++;
        char send_buffer[32];
        snprintf(send_buffer, sizeof(send_buffer), "T-Display-SF32!#%d", tx_conut);
        lv_label_set_text_fmt(tx_msg_label, "TX Message: %s", send_buffer);
        radio_tx((uint8_t *)send_buffer, strlen(send_buffer));
    }

    if (lora_receive_message)
    {
        log_d("rx message: %s, rssi: %d, snr: %d", rx_info.data, rx_info.rssi, rx_info.snr);
        lv_label_set_text_fmt(rx_msg_label, "RX Message: %s", rx_info.data);
        lv_label_set_text_fmt(rx_rssi_label, "RX RSSI: %d dBm", rx_info.rssi);
        lv_label_set_text_fmt(rx_snr_label, "RX SNR: %d dB", rx_info.snr);
    }
}

static void event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int event_id = (int)lv_event_get_user_data(e);

    int index = lv_dropdown_get_selected(target_obj);
    switch (event_id)
    {
    case LORA_EVENT_MODE:
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            switch (index)
            {
            case 0:
                radio_set_mode(MODEM_LORA);
                break;
            }
        }
        break;
    case LORA_EVENT_FREQ:
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            switch (index)
            {
            case 0:
                radio_paras.tx_frequency = 868000000;
                radio_paras.rx_frequency = 868000000 + radio_paras.trx_frequency_offset;
                break;
            case 1:
                radio_paras.tx_frequency = 915000000;
                radio_paras.rx_frequency = 915000000 + radio_paras.trx_frequency_offset;
                break;
            }
            radio_set_freq(radio_paras.tx_frequency);
            radio_set_outpower(22);
            radio_set_rx_boost(true);
        }
        break;
    case LORA_EVENT_POWER:
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            switch (index)
            {
            case 0:
                radio_paras.txpower = 22;
                break;
            case 1:
                radio_paras.txpower = 15;
                break;
            case 2:
                radio_paras.txpower = 10;
                break;
            case 3:
                radio_paras.txpower = 5;
                break;
            case 4:
                radio_paras.txpower = 0;
                break;
            case 5:
                radio_paras.txpower = -5;
                break;
            case 6:
                radio_paras.txpower = -9;
                break;
            }
            radio_set_outpower(radio_paras.txpower);
        }
        break;
    case LORA_EVENT_BW:
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            switch (index)
            {
            case 0:
                radio_paras.bw = LORA_BW_62KHZ;
                break;
            case 1:
                radio_paras.bw = LORA_BW_125KHZ;
                break;
            case 2:
                radio_paras.bw = LORA_BW_250KHZ;
                break;
            case 3:
                radio_paras.bw = LORA_BW_500KHZ;
                break;
            }
            radio_set_lora_bandwidth(radio_paras.bw);
        }
        break;
    case LORA_EVENT_SF:
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            switch (index)
            {
            case 0:
                radio_paras.sf = LORA_SPREADING_FACTOR_5;
                break;
            case 1:
                radio_paras.sf = LORA_SPREADING_FACTOR_6;
                break;
            case 2:
                radio_paras.sf = LORA_SPREADING_FACTOR_7;
                break;
            case 3:
                radio_paras.sf = LORA_SPREADING_FACTOR_8;
                break;
            case 4:
                radio_paras.sf = LORA_SPREADING_FACTOR_9;
                break;
            case 5:
                radio_paras.sf = LORA_SPREADING_FACTOR_10;
                break;
            case 6:
                radio_paras.sf = LORA_SPREADING_FACTOR_11;
                break;
            case 7:
                radio_paras.sf = LORA_SPREADING_FACTOR_12;
                break;
            }
            radio_set_lora_sf(radio_paras.sf);
        }
        break;
    case LORA_EVENT_CR:
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            switch (index)
            {
            case 0:
                radio_paras.cr = 1;
                break;
            case 1:
                radio_paras.cr = 2;
                break;
            case 2:
                radio_paras.cr = 3;
                break;
            case 3:
                radio_paras.cr = 4;
                break;
            }
            radio_set_lora_cr(radio_paras.cr);
        }
        break;
    case LORA_EVENT_SEND_MESSAGE:
        if (code == LV_EVENT_CLICKED)
        {
            lora_send_message = !lora_send_message;

            if (lora_send_message)
            {
                lv_timer_resume(send_timer); // Start the timer immediately
                lv_obj_set_style_text_color(tx_tile, lv_color_hex(0xff9d00), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_color(lora_send_recevice_msg_cont, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_clear_flag(lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_set_style_text_color(tx_tile, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_add_flag(lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
                lv_timer_pause(send_timer);
                radio_sleep();
            }
        }
        break;
    case LORA_EVENT_RECEIVE_MESSAGE:
        if (code == LV_EVENT_CLICKED)
        {
            lora_receive_message = !lora_receive_message;

            if (lora_receive_message)
            {
                radio_rx();
                lv_timer_resume(send_timer); // Start the timer immediately
                lv_obj_set_style_text_color(rx_tile, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_bg_color(lora_send_recevice_msg_cont, lv_color_hex(0xff9d00), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_clear_flag(lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
                rt_kprintf("receive message\n");
            }
            else
            {
                lv_timer_pause(send_timer);
                lv_obj_add_flag(lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_text_color(rx_tile, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(rx_msg_label, "RX Message: null");
                lv_label_set_text(rx_rssi_label, "RX RSSI: null");
                lv_label_set_text(rx_snr_label, "RX SNR: null");
                radio_sleep();
            }
        }
        break;
    }
}
