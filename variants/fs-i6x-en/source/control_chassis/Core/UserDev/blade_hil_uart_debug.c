#include "blade_hil_uart_debug.h"

#if BLADE_HIL_UART_DEBUG_ENABLED

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "usart.h"
#define BLADE_HIL_UART_BUFFER_SIZE 256U
#define BLADE_HIL_UART_QUEUE_DEPTH 8U

static char g_tx_buffers[BLADE_HIL_UART_QUEUE_DEPTH][BLADE_HIL_UART_BUFFER_SIZE];
static uint16_t g_tx_lengths[BLADE_HIL_UART_QUEUE_DEPTH];
static volatile uint8_t g_queue_head;
static volatile uint8_t g_queue_tail;
static volatile bool g_tx_busy;

static void BladeHilDebug_StartNext(void)
{
    uint8_t tail;

    if (g_tx_busy || (g_queue_tail == g_queue_head))
    {
        return;
    }
    tail = g_queue_tail;
    g_tx_busy = true;
    if (HAL_UART_Transmit_IT(&huart2,
                             (uint8_t *)g_tx_buffers[tail],
                             g_tx_lengths[tail]) != HAL_OK)
    {
        g_tx_busy = false;
    }
}
void BladeHilDebug_Init(void)
{
    g_queue_head = 0U;
    g_queue_tail = 0U;
    g_tx_busy = false;
}

void BladeHilDebug_Printf(const char *format, ...)
{
    uint8_t head;
    uint8_t next_head;
    uint32_t interrupt_mask;
    va_list args;
    int length;

    if (format == NULL)
    {
        return;
    }

    head = g_queue_head;
    next_head = (uint8_t)((head + 1U) % BLADE_HIL_UART_QUEUE_DEPTH);
    if (next_head == g_queue_tail)
    {
        return;
    }

    va_start(args, format);
    length = vsnprintf(g_tx_buffers[head],
                       BLADE_HIL_UART_BUFFER_SIZE, format, args);
    va_end(args);
    if (length <= 0)
    {
        return;
    }
    if (length >= (int)BLADE_HIL_UART_BUFFER_SIZE)
    {
        length = (int)BLADE_HIL_UART_BUFFER_SIZE - 1;
    }
    g_tx_lengths[head] = (uint16_t)length;

    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    g_queue_head = next_head;
    BladeHilDebug_StartNext();
    if (interrupt_mask == 0U)
    {
        __enable_irq();
    }
}
void BladeHilDebug_OnTxComplete(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART2))
    {
        g_queue_tail = (uint8_t)((g_queue_tail + 1U) % BLADE_HIL_UART_QUEUE_DEPTH);
        g_tx_busy = false;
        BladeHilDebug_StartNext();
    }
}

#endif
