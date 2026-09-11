/**
@file drv_usart.h
@brief USART/DMA 主机抽象：静态 2048 RX、Peek/Release、ISR 仅冻标志
@note 语义参考 ref-v031 drv_usart；主机默认 HOST_MOCK，无 GD32 寄存器位操作。
*/

#ifndef CARDREADER_DRV_USART_H
#define CARDREADER_DRV_USART_H

#include <stddef.h>
#include <stdint.h>

/** 与 R02 §3.2 / BOOT_DMA_RX_STATIC_SIZE 对齐的静态 RX 容量 */
#define DRV_USART_RX_STATIC_SIZE (2048u)

/**
@brief 初始化 USART 抽象（清冻结标志；主机不碰硬件寄存器）
*/
void DrvUsart_Init(void);

/**
@brief 窥视本轮冻结 RX（就绪时返回缓冲指针与长度；未就绪 *out_len=0 且返回 NULL）
@param out_len 输出实收长度；可为 NULL
@return 缓冲指针或 NULL
*/
const uint8_t *DrvUsart_RxPeek(size_t *out_len);

/**
@brief 释放本轮：清就绪标志；不清零 2048 字节内容（R02）
*/
void DrvUsart_RxRelease(void);

/**
@brief ISR 桩：仅冻结本轮标志（就绪/长度/停 DMA 语义）
@note ISR 禁止：Flash 擦写/编程、YMODEM 解析、GT32 SPI、阻塞发送、Meta、printf、长循环。
      主机由 DrvUsartMock_FreezeRound 模拟「ISR 已冻轮」。
*/
void DrvUsart_IsrStub(void);

#ifdef BOARD_GT32_HOST_MOCK
/**
@brief 主机 Mock：向静态 RX 写入本轮数据并置冻结就绪
@param data 输入；NULL 且 len>0 非法
@param len 长度；0..DRV_USART_RX_STATIC_SIZE
@return 0 成功；负值失败
*/
int DrvUsartMock_FreezeRound(const uint8_t *data, size_t len);

/**
@brief 主机 Mock：是否已冻结就绪
@return 非 0 就绪
*/
int DrvUsartMock_IsFrozen(void);

/**
@brief 主机 Mock：ISR 桩调用次数
@return 次数
*/
uint32_t DrvUsartMock_IsrStubCount(void);
#endif /* BOARD_GT32_HOST_MOCK */

#if defined(BOARD_GT32_TARGET) && !defined(BOARD_GT32_HOST_MOCK)
/**
@brief 目标板 USART/DMA 初始化（引脚/时钟待确认；本切片留空 TODO）
@note 禁止在此编造 GPIO 号；实机 RS485 未验证。
*/
void DrvUsart_TargetInitTodo(void);
#endif

#endif /* CARDREADER_DRV_USART_H */
