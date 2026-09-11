/**
@file drv_usart.c
@brief USART/DMA 主机抽象实现（静态 2048 RX；默认无 GD32 寄存器）
@note 语义参考 ref-v031；禁止把 GD32 位操作拷入主机路径。
*/

#include "drv_usart.h"

#include <string.h>

/** 静态 DMA RX 缓冲（禁止栈上等价大数组） */
static uint8_t s_rx_buf[DRV_USART_RX_STATIC_SIZE];
static size_t s_rx_len;
static int s_rx_ready;
static uint32_t s_isr_stub_count;

void DrvUsart_Init(void)
{
    s_rx_len = 0u;
    s_rx_ready = 0;
    s_isr_stub_count = 0u;
    /* Init 允许清零以便主机可重复；RxRelease 仍不清零内容 */
    (void)memset(s_rx_buf, 0, sizeof(s_rx_buf));
}

const uint8_t *DrvUsart_RxPeek(size_t *out_len)
{
    if (!s_rx_ready) {
        if (out_len != NULL) {
            *out_len = 0u;
        }
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = s_rx_len;
    }
    return s_rx_buf;
}

void DrvUsart_RxRelease(void)
{
    /*
     * 清就绪标志；重装地址/计数语义由目标板实现补齐。
     * 不清零 2048 字节缓冲（R02 §3.2）。
     */
    s_rx_ready = 0;
    s_rx_len = 0u;
}

void DrvUsart_IsrStub(void)
{
    /*
     * ISR 仅冻结本轮标志（就绪/长度/停 DMA）。
     * 禁止：Flash 擦写/编程、YMODEM 解析、GT32 SPI、阻塞发送、Meta、printf、长循环。
     */
    s_isr_stub_count++;
    if (s_rx_len > 0u && !s_rx_ready) {
        s_rx_ready = 1;
    }
}

#ifdef BOARD_GT32_HOST_MOCK
int DrvUsartMock_FreezeRound(const uint8_t *data, size_t len)
{
    if (len > DRV_USART_RX_STATIC_SIZE) {
        return -2;
    }
    if (len > 0u && data == NULL) {
        return -1;
    }
    if (s_rx_ready) {
        /* 已冻结轮次：不覆盖（与 R02 一致） */
        return -3;
    }
    if (len > 0u) {
        (void)memcpy(s_rx_buf, data, len);
    }
    s_rx_len = len;
    DrvUsart_IsrStub();
    return 0;
}

int DrvUsartMock_IsFrozen(void)
{
    return s_rx_ready;
}

uint32_t DrvUsartMock_IsrStubCount(void)
{
    return s_isr_stub_count;
}
#endif /* BOARD_GT32_HOST_MOCK */

#if defined(BOARD_GT32_TARGET) && !defined(BOARD_GT32_HOST_MOCK)
void DrvUsart_TargetInitTodo(void)
{
    /* TODO: 引脚/USART/DMA 待原理图确认后实现；禁止编造 GPIO 号 */
}
#endif
