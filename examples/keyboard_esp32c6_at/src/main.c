#include "bf0_hal.h"
#include "drv_io.h"
#include "rtthread.h"
#include "uart_mux.h"
#include "xl9555.h"

#define POWER_HOLD_PIN 41
#define ESP32C6_BAUDRATE 115200U
#define COMMAND_RESPONSE_WAIT_MS 1000U
#define TEST_PERIOD_MS 5000U
#define RX_LINE_SIZE 256U

static char rx_line[RX_LINE_SIZE];
static rt_size_t rx_line_length;

static void board_power_hold(void)
{
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1);
    rt_pin_mode(POWER_HOLD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(POWER_HOLD_PIN, PIN_HIGH);
}

static void esp32c6_power_on_and_reset(void)
{
    xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 1);
    rt_thread_mdelay(100);

    xl9555_pin_mode(XL9555_WIFI_RST_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_RST_PIN, 0);
    rt_thread_mdelay(500);
    xl9555_digital_write(XL9555_WIFI_RST_PIN, 1);
}

static void esp32c6_rx_callback(rt_uint8_t data)
{
    if (data == '\r')
        return;

    if (data == '\n')
    {
        if (rx_line_length > 0)
        {
            rx_line[rx_line_length] = '\0';
            rt_kprintf("[ESP32C6 RX] %s\n", rx_line);
            rx_line_length = 0;
        }
        return;
    }

    if (rx_line_length < sizeof(rx_line) - 1U)
    {
        rx_line[rx_line_length++] = (char)data;
    }
    else
    {
        rx_line[rx_line_length] = '\0';
        rt_kprintf("[ESP32C6 RX] %s [truncated]\n", rx_line);
        rx_line_length = 0;
    }
}

static rt_err_t esp32c6_send_command(const char *command)
{
    char tx_buffer[64];
    rt_size_t length;
    int command_length;
    int sent;
    rt_err_t result;

    result = uart_mux_switch_to(UART_MUX_DEVICE_ESP32C6,
                                ESP32C6_BAUDRATE);
    if (result != RT_EOK)
    {
        rt_kprintf("[ESP32C6] UART mux switch failed: %d\n", result);
        return result;
    }

    command_length = rt_snprintf(tx_buffer, sizeof(tx_buffer), "%s\r\n",
                                 command);
    if (command_length <= 0 || command_length >= (int)sizeof(tx_buffer))
    {
        rt_kprintf("[ESP32C6] command is too long\n");
        return -RT_EINVAL;
    }
    length = (rt_size_t)command_length;

    rt_kprintf("[ESP32C6 TX] %s\n", command);
    sent = uart_mux_send((const rt_uint8_t *)tx_buffer, length);
    if (sent != (int)length)
    {
        rt_kprintf("[ESP32C6] send failed: %d/%u bytes\n", sent,
                   (unsigned int)length);
        return -RT_EIO;
    }

    return RT_EOK;
}

int main(void)
{
    rt_uint32_t test_count = 0;
    rt_err_t result;

    board_power_hold();

    result = xl9555_init();
    if (result != RT_EOK)
    {
        rt_kprintf("[ESP32C6] XL9555 initialization failed: %d\n", result);
        return result;
    }

    result = uart_mux_init(UART_BUS_NAME);
    if (result != RT_EOK)
    {
        rt_kprintf("[ESP32C6] UART mux initialization failed: %d\n",
                   result);
        return result;
    }

    result = uart_mux_register_rx_callback(UART_MUX_DEVICE_ESP32C6,
                                           esp32c6_rx_callback);
    if (result != RT_EOK)
    {
        rt_kprintf("[ESP32C6] RX callback registration failed: %d\n",
                   result);
        return result;
    }
    result = uart_mux_switch_to(UART_MUX_DEVICE_ESP32C6,
                                ESP32C6_BAUDRATE);
    if (result != RT_EOK)
    {
        rt_kprintf("[ESP32C6] initial UART mux switch failed: %d\n",
                   result);
        return result;
    }

    /* Open RX before reset so ESP32C6 boot and AT firmware logs are visible. */
    esp32c6_power_on_and_reset();
    rt_thread_mdelay(2000);

    while (1)
    {
        test_count++;
        rt_kprintf("\n[ESP32C6] AT firmware test #%u\n",
                   (unsigned int)test_count);

        esp32c6_send_command("AT");
        rt_thread_mdelay(COMMAND_RESPONSE_WAIT_MS);

        esp32c6_send_command("AT+GMR");
        rt_thread_mdelay(TEST_PERIOD_MS);
    }
}
