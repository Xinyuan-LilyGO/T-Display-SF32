#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"
#include "drv_io.h"

#define UART2_DEVICE_NAME     "uart2"
#define UART2_BAUD_RATE       921600U
#define UART2_RX_BUFFER_SIZE  64U
#define UART_FRAME_SIZE       8U
#define UART_RETRY_TIMEOUT_MS 1000U
#define UART_LOG_INTERVAL     1000U

static const uint8_t frame_marker[4] = {'P', 'I', 'N', 'G'};
static rt_device_t uart2_device;
static struct rt_semaphore uart2_rx_ready;
static struct rt_semaphore uart2_tx_done;
static uint8_t uart2_tx_data[UART_FRAME_SIZE];
static uint8_t uart2_rx_data[UART_FRAME_SIZE];
static rt_size_t uart2_rx_fill;

static rt_err_t uart2_rx_indicate(rt_device_t dev, rt_size_t size)
{
    if (dev == uart2_device && size > 0U)
    {
        rt_sem_release(&uart2_rx_ready);
    }
    return RT_EOK;
}

static rt_err_t uart2_tx_complete(rt_device_t dev, void *buffer)
{
    if (dev == uart2_device && buffer == uart2_tx_data)
    {
        rt_sem_release(&uart2_tx_done);
    }
    return RT_EOK;
}

static void uart_frame_build(uint8_t *frame, uint32_t sequence)
{
    frame[0] = 'P';
    frame[1] = 'I';
    frame[2] = 'N';
    frame[3] = 'G';
    frame[4] = (uint8_t)sequence;
    frame[5] = (uint8_t)(sequence >> 8);
    frame[6] = (uint8_t)(sequence >> 16);
    frame[7] = (uint8_t)(sequence >> 24);
}

static rt_bool_t uart_frame_parse(uint8_t byte)
{
    if (uart2_rx_fill < sizeof(frame_marker))
    {
        if (byte == frame_marker[uart2_rx_fill])
        {
            uart2_rx_data[uart2_rx_fill++] = byte;
        }
        else
        {
            uart2_rx_fill = (byte == frame_marker[0]) ? 1U : 0U;
            if (uart2_rx_fill != 0U)
            {
                uart2_rx_data[0] = byte;
            }
        }
        return RT_FALSE;
    }

    uart2_rx_data[uart2_rx_fill++] = byte;
    if (uart2_rx_fill == UART_FRAME_SIZE)
    {
        uart2_rx_fill = 0U;
        return RT_TRUE;
    }
    return RT_FALSE;
}

static rt_err_t uart2_receive_frame(rt_int32_t timeout)
{
    uint8_t buffer[UART2_RX_BUFFER_SIZE];

    while (1)
    {
        rt_size_t count;
        rt_size_t index;

        count = rt_device_read(uart2_device, 0, buffer, sizeof(buffer));
        for (index = 0; index < count; index++)
        {
            if (uart_frame_parse(buffer[index]))
            {
                return RT_EOK;
            }
        }

        if (rt_sem_take(&uart2_rx_ready, timeout) != RT_EOK)
        {
            return -RT_ETIMEOUT;
        }
    }
}

static rt_err_t uart2_transmit(void)
{
    rt_size_t written;

    written = rt_device_write(uart2_device, 0, uart2_tx_data,
                              sizeof(uart2_tx_data));
    if (written != sizeof(uart2_tx_data))
    {
        return -RT_EIO;
    }

    if (rt_sem_take(&uart2_tx_done,
                    rt_tick_from_millisecond(UART_RETRY_TIMEOUT_MS)) != RT_EOK)
    {
        return -RT_ETIMEOUT;
    }
    return RT_EOK;
}

static rt_err_t uart2_init(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t result;

    HAL_PIN_Set(PAD_PA39, USART2_RXD, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA40, USART2_TXD, PIN_PULLUP, 1);

    uart2_device = rt_device_find(UART2_DEVICE_NAME);
    if (uart2_device == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    config.baud_rate = UART2_BAUD_RATE;
    config.bufsz = UART2_RX_BUFFER_SIZE;
    result = rt_device_control(uart2_device, RT_DEVICE_CTRL_CONFIG, &config);
    if (result != RT_EOK)
    {
        return result;
    }

    rt_device_set_rx_indicate(uart2_device, uart2_rx_indicate);
    rt_device_set_tx_complete(uart2_device, uart2_tx_complete);
    return rt_device_open(uart2_device,
                          RT_DEVICE_OFLAG_RDWR |
                          RT_DEVICE_FLAG_DMA_RX |
                          RT_DEVICE_FLAG_DMA_TX);
}

int main(void)
{
    uint32_t sequence = 0;
    uint32_t transfer_count = 0;
    uint32_t mismatch_count = 0;
    rt_err_t result;

    rt_sem_init(&uart2_rx_ready, "uart2_rx", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&uart2_tx_done, "uart2_tx", 0, RT_IPC_FLAG_FIFO);

    result = uart2_init();
    if (result != RT_EOK)
    {
        rt_kprintf("open UART2 RX/TX DMA failed: %d\n", result);
        return result;
    }

    uart_frame_build(uart2_tx_data, sequence);
#ifdef UART_PINGPONG_INITIATOR
    rt_kprintf("RT UART2 DMA ping-pong initiator, 921600 baud\n");
    result = uart2_transmit();
    if (result != RT_EOK)
    {
        rt_kprintf("UART2 initial transmit failed: %d\n", result);
        return result;
    }
#else
    rt_kprintf("RT UART2 DMA ping-pong responder, 921600 baud\n");
#endif

    while (1)
    {
#ifdef UART_PINGPONG_INITIATOR
        result = uart2_receive_frame(
                     rt_tick_from_millisecond(UART_RETRY_TIMEOUT_MS));
        if (result == -RT_ETIMEOUT)
        {
            result = uart2_transmit();
            if (result != RT_EOK)
            {
                rt_kprintf("UART2 retry transmit failed: %d\n", result);
                return result;
            }
            continue;
        }
#else
        result = uart2_receive_frame(RT_WAITING_FOREVER);
#endif

        if (result != RT_EOK)
        {
            rt_kprintf("UART2 receive failed: %d\n", result);
            return result;
        }

#ifdef UART_PINGPONG_INITIATOR
        if (rt_memcmp(uart2_rx_data, uart2_tx_data, UART_FRAME_SIZE) != 0)
        {
            mismatch_count++;
        }
        uart_frame_build(uart2_tx_data, ++sequence);
#else
        rt_memcpy(uart2_tx_data, uart2_rx_data, UART_FRAME_SIZE);
#endif

        result = uart2_transmit();
        if (result != RT_EOK)
        {
            rt_kprintf("UART2 transmit failed: %d\n", result);
            return result;
        }

        transfer_count++;
        if ((transfer_count % UART_LOG_INTERVAL) == 0U)
        {
            rt_kprintf("RT UART2 ping-pong: frames=%u, mismatches=%u\n",
                       (unsigned int)transfer_count,
                       (unsigned int)mismatch_count);
        }
    }
}
