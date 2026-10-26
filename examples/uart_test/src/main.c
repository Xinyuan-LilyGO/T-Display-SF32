#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "dma_config.h"

#define UART2_BAUD_RATE       921600U
#define UART_FRAME_SIZE       8U
#define UART_RETRY_TIMEOUT_MS 1000U
#define UART_LOG_INTERVAL     1000U

static UART_HandleTypeDef uart2_handle;
static DMA_HandleTypeDef uart2_tx_dma;
static DMA_HandleTypeDef uart2_rx_dma;
static struct rt_semaphore uart2_tx_done;
static struct rt_semaphore uart2_rx_done;
static volatile uint32_t uart2_error_code;
static uint8_t uart2_tx_data[UART_FRAME_SIZE];
static uint8_t uart2_rx_data[UART_FRAME_SIZE];

#ifndef DMA_SUPPORT_DYN_CHANNEL_ALLOC
void UART2_DMA_TX_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_DMA_IRQHandler(&uart2_tx_dma);
    rt_interrupt_leave();
}

void UART2_DMA_RX_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_DMA_IRQHandler(&uart2_rx_dma);
    rt_interrupt_leave();
}
#endif

void USART2_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_UART_IRQHandler(&uart2_handle);
    rt_interrupt_leave();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &uart2_handle)
    {
        rt_sem_release(&uart2_tx_done);
    }
}

static void uart2_dma_rx_complete(DMA_HandleTypeDef *hdma)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)hdma->Parent;

    huart->RxXferCount = 0U;
    CLEAR_BIT(huart->Instance->CR1, USART_CR1_PEIE);
    CLEAR_BIT(huart->Instance->CR3, USART_CR3_EIE | USART_CR3_DMAR);
    huart->RxState = HAL_UART_STATE_READY;
    rt_sem_release(&uart2_rx_done);
}

static void uart2_dma_error(DMA_HandleTypeDef *hdma)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)hdma->Parent;

    huart->ErrorCode |= HAL_UART_ERROR_DMA;
    uart2_error_code = huart->ErrorCode;
    if (hdma == huart->hdmatx)
    {
        CLEAR_BIT(huart->Instance->CR3, USART_CR3_DMAT);
        huart->gState = HAL_UART_STATE_READY;
        rt_sem_release(&uart2_tx_done);
    }
    else
    {
        CLEAR_BIT(huart->Instance->CR3, USART_CR3_DMAR);
        huart->RxState = HAL_UART_STATE_READY;
        rt_sem_release(&uart2_rx_done);
    }
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

static HAL_StatusTypeDef uart2_dma_init(void)
{
    HAL_PIN_Set(PAD_PA39, USART2_RXD, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA40, USART2_TXD, PIN_PULLUP, 1);

    HAL_RCC_EnableModule(RCC_MOD_USART2);
    HAL_RCC_EnableModule(RCC_MOD_DMAC1);

    uart2_handle.Instance = hwp_usart2;
    uart2_handle.Init.BaudRate = UART2_BAUD_RATE;
    uart2_handle.Init.WordLength = UART_WORDLENGTH_8B;
    uart2_handle.Init.StopBits = UART_STOPBITS_1;
    uart2_handle.Init.Parity = UART_PARITY_NONE;
    uart2_handle.Init.Mode = UART_MODE_TX_RX;
    uart2_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart2_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&uart2_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }

    uart2_tx_dma.Instance = UART2_TX_DMA_INSTANCE;
    uart2_tx_dma.Init.Request = UART2_TX_DMA_REQUEST;
    uart2_tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    uart2_tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    uart2_tx_dma.Init.MemInc = DMA_MINC_ENABLE;
    uart2_tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    uart2_tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    uart2_tx_dma.Init.Mode = DMA_NORMAL;
    uart2_tx_dma.Init.Priority = DMA_PRIORITY_LOW;
    __HAL_LINKDMA(&uart2_handle, hdmatx, uart2_tx_dma);
    if (HAL_DMA_Init(&uart2_tx_dma) != HAL_OK)
    {
        return HAL_ERROR;
    }

    uart2_rx_dma.Instance = UART2_RX_DMA_INSTANCE;
    uart2_rx_dma.Init.Request = UART2_RX_DMA_REQUEST;
    uart2_rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    uart2_rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    uart2_rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    uart2_rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    uart2_rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    uart2_rx_dma.Init.Mode = DMA_NORMAL;
    uart2_rx_dma.Init.Priority = DMA_PRIORITY_MEDIUM;
    __HAL_LINKDMA(&uart2_handle, hdmarx, uart2_rx_dma);
    if (HAL_DMA_Init(&uart2_rx_dma) != HAL_OK)
    {
        return HAL_ERROR;
    }

#ifndef DMA_SUPPORT_DYN_CHANNEL_ALLOC
    HAL_NVIC_SetPriority(UART2_TX_DMA_IRQ, UART2_TX_DMA_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(UART2_TX_DMA_IRQ);
    HAL_NVIC_SetPriority(UART2_RX_DMA_IRQ, UART2_RX_DMA_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(UART2_RX_DMA_IRQ);
#endif

    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    return HAL_OK;
}

static HAL_StatusTypeDef uart2_receive_start(void)
{
    HAL_StatusTypeDef status;

    status = HAL_UART_Receive_DMA(&uart2_handle, uart2_rx_data,
                                  sizeof(uart2_rx_data));
    if (status == HAL_OK)
    {
        uart2_rx_dma.XferCpltCallback = uart2_dma_rx_complete;
        uart2_rx_dma.XferHalfCpltCallback = RT_NULL;
        uart2_rx_dma.XferErrorCallback = uart2_dma_error;
        __HAL_DMA_DISABLE_IT(&uart2_rx_dma, DMA_IT_HT);
        CLEAR_BIT(uart2_handle.Instance->CR1, USART_CR1_PEIE);
        CLEAR_BIT(uart2_handle.Instance->CR3, USART_CR3_EIE);
    }
    return status;
}

static HAL_StatusTypeDef uart2_transmit(const uint8_t *data)
{
    HAL_StatusTypeDef status;

    status = HAL_UART_Transmit_DMA(&uart2_handle, (uint8_t *)data,
                                   UART_FRAME_SIZE);
    if (status != HAL_OK)
    {
        return status;
    }

    uart2_tx_dma.XferHalfCpltCallback = RT_NULL;
    uart2_tx_dma.XferErrorCallback = uart2_dma_error;
    __HAL_DMA_DISABLE_IT(&uart2_tx_dma, DMA_IT_HT);
    if (rt_sem_take(&uart2_tx_done,
                    rt_tick_from_millisecond(UART_RETRY_TIMEOUT_MS)) != RT_EOK)
    {
        return HAL_TIMEOUT;
    }
    return (uart2_error_code == HAL_UART_ERROR_NONE) ? HAL_OK : HAL_ERROR;
}

int main(void)
{
    uint32_t sequence = 0;
    uint32_t transfer_count = 0;
    uint32_t mismatch_count = 0;
    rt_err_t result;

    rt_sem_init(&uart2_tx_done, "uart2_tx", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&uart2_rx_done, "uart2_rx", 0, RT_IPC_FLAG_FIFO);

    if (uart2_dma_init() != HAL_OK || uart2_receive_start() != HAL_OK)
    {
        rt_kprintf("UART2 DMA initialization failed\n");
        return -RT_ERROR;
    }

    uart_frame_build(uart2_tx_data, sequence);
#ifdef UART_PINGPONG_INITIATOR
    rt_kprintf("UART2 DMA ping-pong initiator, 921600 baud, 8-byte frame\n");
    if (uart2_transmit(uart2_tx_data) != HAL_OK)
    {
        rt_kprintf("UART2 initial DMA transmit failed\n");
        return -RT_ERROR;
    }
#else
    rt_kprintf("UART2 DMA ping-pong responder, 921600 baud, 8-byte frame\n");
#endif

    while (1)
    {
#ifdef UART_PINGPONG_INITIATOR
        result = rt_sem_take(&uart2_rx_done,
                             rt_tick_from_millisecond(UART_RETRY_TIMEOUT_MS));
        if (result != RT_EOK)
        {
            if (uart2_transmit(uart2_tx_data) != HAL_OK)
            {
                rt_kprintf("UART2 DMA retry transmit failed\n");
                return -RT_ERROR;
            }
            continue;
        }
#else
        result = rt_sem_take(&uart2_rx_done, RT_WAITING_FOREVER);
#endif

        if (result != RT_EOK || uart2_error_code != HAL_UART_ERROR_NONE)
        {
            rt_kprintf("UART2 DMA receive error: 0x%08x\n",
                       (unsigned int)uart2_error_code);
            return -RT_ERROR;
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

        /* Arm RX before replying so the peer can answer immediately. */
        if (uart2_receive_start() != HAL_OK ||
                uart2_transmit(uart2_tx_data) != HAL_OK)
        {
            rt_kprintf("UART2 DMA restart failed\n");
            return -RT_ERROR;
        }

        transfer_count++;
        if ((transfer_count % UART_LOG_INTERVAL) == 0U)
        {
            rt_kprintf("UART2 ping-pong: frames=%u, mismatches=%u\n",
                       (unsigned int)transfer_count,
                       (unsigned int)mismatch_count);
        }
    }
}
