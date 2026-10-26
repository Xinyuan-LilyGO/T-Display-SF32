#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "board.h"

#define UART2_NAME "uart2"
#define ONE_DATA_MAXLEN 256

#define DBG_LVL DBG_LOG
#include <rtdbg.h>

static rt_device_t uart2_device;
static struct rt_semaphore rx_sem;
static uint8_t rx_buffer[ONE_DATA_MAXLEN] = {0};

static rt_err_t uart2_rx_callback(rt_device_t dev, rt_size_t size)
{
  if (size > 0)
  {
    rt_sem_release(&rx_sem);
  }
  return RT_EOK;
}

static void uart2_rx_thread_entry(void *parameter)
{
  uint16_t count = 0;
  uint8_t cnt = 0;

  while (1)
  {
    /* code */
    rt_sem_take(&rx_sem, RT_WAITING_FOREVER);
    while (1)
    {
      /* code */
      cnt = rt_device_read(uart2_device, 0, rx_buffer, ONE_DATA_MAXLEN);
      count = count + cnt;
      rt_kprintf("urat2_rx: cnt = %d, count = %d\n", cnt, count);
      if (cnt == 0)
      {
        break;
      }

      rt_kprintf("uart2_rx: ");
      for (int i = 0; i < count; i++)
      {
        rt_kprintf("%c", rx_buffer[i]);
      }
      count = 0;
      rt_kprintf("\n");
    }
  }
}

static void uart2_send(uint8_t *data, uint16_t len)
{
  uint16_t write_len = 0;
  if (uart2_device == RT_NULL)
  {
    return;
  }
  write_len = rt_device_write(uart2_device, 0, data, len);
  rt_kprintf("uart2_send:");
  for (int i = 0; i < write_len; i++)
  {
    rt_kprintf("%c", data[i]);
  }
}

int uart2_init(void)
{
  HAL_PIN_Set(PAD_PA39, USART2_RXD, PIN_PULLUP, 1);
  HAL_PIN_Set(PAD_PA40, USART2_TXD, PIN_PULLUP, 1);

  uart2_device = rt_device_find(UART2_NAME);
  if (uart2_device == RT_NULL)
  {
    rt_kprintf("find %s failed\n", UART2_NAME);
    return RT_ERROR;
  }

  rt_err_t err = RT_EOK;
  struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
  config.baud_rate = 115200;
  err = rt_device_control(uart2_device, RT_DEVICE_CTRL_CONFIG, &config);
  LOG_D("uart2 config: %d", err);

  rt_sem_init(&rx_sem, "rx_sem", 0, RT_IPC_FLAG_FIFO);
  rt_err_t result = rt_device_open(uart2_device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
  if (result != -RT_EIO)
  {
    rt_device_open(uart2_device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
  }

  rt_device_set_rx_indicate(uart2_device, uart2_rx_callback);

  rt_thread_t uart2_thread = rt_thread_create("uart2_rx", uart2_rx_thread_entry, RT_NULL, 3 * 1024, 12, 10);
  if (uart2_thread != RT_NULL)
  {
    rt_thread_startup(uart2_thread);
  }
  else
  {
    rt_kprintf("create uart2_rx thread failed\n");
    err = RT_ERROR;
  }

  return err;
}

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
  /* Output a message on console using printf function */
  uart2_init();
  /* Infinite loop */
  uint8_t tx_data[ONE_DATA_MAXLEN] = {'u', 'a', 'r', 't', '2', ' ', 'd', 'e', 'm', 'o', '\n'};
  uint8_t tx_data_1[ONE_DATA_MAXLEN] = {'$', 'P', 'C', 'A', 'S', '0', '1', ',', '5', '*', '1', '9', '\r', '\n'};
  uint8_t tx_data_2[32] = {'$', 'P', 'C', 'A', 'S', '0', '1', ',', '1', '*', '1', 'D', '\r', '\n'};//9600

  while (1)
  {
    uart2_send(tx_data_2, 14);
    rt_kprintf("Hello world!\n");
    rt_thread_mdelay(3000);
  }
  return 0;
}
