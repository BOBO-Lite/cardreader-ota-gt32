/**
@file board_gt32.h
@brief GT32 SPI 板级抽象（时钟初期 ≤9 MHz）
@note 引脚已由用户确认（2026-09-12）：共用 SPI2 总线 PB13/PB14/PB15；
      GT32 CS=PB9；屏同总线 CS=PB12（Boot 不驱动屏 CS）。实机尚未验证（非 E3）。
*/

#ifndef CARDREADER_BOARD_GT32_H
#define CARDREADER_BOARD_GT32_H

#include <stddef.h>
#include <stdint.h>

/**
@brief 引脚是否已由用户确认：1=已确认（GPIO 号可用；不等于 E3 通过）
*/
#define BOARD_GT32_PINS_CONFIRMED 1

/**
@brief SPI 总线：GD32 SPI2（SCK/MISO/MOSI = PB13/PB14/PB15），与屏共用
*/
#define BOARD_GT32_SPI_BUS_SPI2 1

/** @brief SCK：GPIOB pin 13 */
#define BOARD_GT32_SPI_SCK_PORT_LETTER 'B'
#define BOARD_GT32_SPI_SCK_PIN 13u

/** @brief MISO：GPIOB pin 14 */
#define BOARD_GT32_SPI_MISO_PORT_LETTER 'B'
#define BOARD_GT32_SPI_MISO_PIN 14u

/** @brief MOSI：GPIOB pin 15 */
#define BOARD_GT32_SPI_MOSI_PORT_LETTER 'B'
#define BOARD_GT32_SPI_MOSI_PIN 15u

/** @brief GT32 片选：GPIOB pin 9（Boot/OTA 驱动） */
#define BOARD_GT32_CS_PORT_LETTER 'B'
#define BOARD_GT32_CS_PIN 9u

/**
@brief 屏片选（同 SPI 总线）：GPIOB pin 12
@note Boot 路径不得断言/操作此脚；仅文档与互斥说明。
*/
#define BOARD_DISPLAY_CS_PORT_LETTER 'B'
#define BOARD_DISPLAY_CS_PIN 12u

/**
@brief SPI 初期最大时钟（Hz）；R02 §3.8 要求 ≤9 MHz
*/
#define board_gt32_spi_hz_max (9000000u)

/**
@brief 初始化板级 SPI（时钟 ≤ board_gt32_spi_hz_max）
@return 0 成功；负值失败（与 GT32_Status_t 对齐，如 GT32_ERR_NOT_READY）
@note 非 HOST_MOCK 默认桩返回未就绪；引脚未确认前不得假定真实硬件可用
*/
int BoardGt32_SpiInit(void);

/**
@brief 拉低片选（CS assert）
@return 0 成功；负值失败
*/
int BoardGt32_CsAssert(void);

/**
@brief 拉高片选（CS deassert）
@return 0 成功；负值失败
*/
int BoardGt32_CsDeassert(void);

/**
@brief 全双工 SPI 传输
@param tx 发送缓冲（可为 NULL 表示发 0xFF）
@param rx 接收缓冲（可为 NULL 表示丢弃）
@param len 字节数
@return 0 成功；负值失败
*/
int BoardGt32_SpiTransfer(const uint8_t *tx, uint8_t *rx, size_t len);

/**
@brief 取得单调毫秒时基（用于 WIP 超时）
@return 毫秒计数
*/
uint32_t BoardGt32_GetTickMs(void);

/**
@brief 粗延时（可选）
@param ms 毫秒
*/
void BoardGt32_DelayMs(uint32_t ms);

/**
@brief 喂狗桩（WIP 轮询/长擦除段间调用）
*/
void BoardGt32_FeedWdt(void);

#ifdef BOARD_GT32_HOST_MOCK
/**
@brief 主机 Mock：复位记录与 WIP 状态
*/
void BoardGt32Mock_Reset(void);

/**
@brief 主机 Mock：强制 SPI 失败（非 0 则 SpiTransfer/Init 失败）
@param fail 非 0 使能失败
*/
void BoardGt32Mock_SetSpiFail(int fail);

/**
@brief 主机 Mock：粘滞 WIP（非 0 则 RDSR 始终回报 WIP=1）
@param sticky 非 0 粘滞
*/
void BoardGt32Mock_SetStickyWip(int sticky);

/**
@brief 主机 Mock：推进时基
@param ms 增加的毫秒数
*/
void BoardGt32Mock_AdvanceTick(uint32_t ms);

/**
@brief 主机 Mock：已记录的 TX 首字节（每帧 CS 后第一字节视为 opcode）个数
@return 个数
*/
size_t BoardGt32Mock_OpcodeCount(void);

/**
@brief 主机 Mock：取第 index 个已记录 opcode
@param index 下标
@return opcode；越界返回 0xFF
*/
uint8_t BoardGt32Mock_OpcodeAt(size_t index);

/**
@brief 主机 Mock：是否曾记录过指定 opcode
@param opcode 命令字节
@return 非 0 表示出现过
*/
int BoardGt32Mock_HasOpcode(uint8_t opcode);

/**
@brief 主机 Mock：统计指定 opcode 出现次数
@param opcode 命令字节
@return 次数
*/
size_t BoardGt32Mock_OpcodeOccurrences(uint8_t opcode);

/**
@brief 主机 Mock：FeedWdt 调用次数
@return 次数
*/
uint32_t BoardGt32Mock_FeedWdtCount(void);

/**
@brief 主机 Mock：已记录的擦除命令地址个数（20h/D8h 的 24-bit 地址）
@return 个数
*/
size_t BoardGt32Mock_EraseAddrCount(void);

/**
@brief 主机 Mock：取第 index 个擦除地址
@param index 下标
@return 地址；越界返回 0xFFFFFFFFu
*/
uint32_t BoardGt32Mock_EraseAddrAt(size_t index);

/**
@brief 主机 Mock：已记录的页编程地址个数（02h 的 24-bit 地址）
@return 个数
*/
size_t BoardGt32Mock_PpAddrCount(void);

/**
@brief 主机 Mock：取第 index 个页编程地址
@param index 下标
@return 地址；越界返回 0xFFFFFFFFu
*/
uint32_t BoardGt32Mock_PpAddrAt(size_t index);

/**
@brief 主机 Mock：单调事件序号 +1 并返回新值（LastGood/App 先后证据）
@return 新序号
*/
uint32_t BoardGt32Mock_BumpEventSeq(void);

/**
@brief 主机 Mock：当前事件序号
@return 序号
*/
uint32_t BoardGt32Mock_EventSeq(void);

/**
@brief 主机 Mock：向用户区镜像装载字节（测试预置 Secondary 等）
@param addr 用户区绝对偏移
@param data 数据
@param len 长度
*/
void BoardGt32Mock_LoadFlash(uint32_t addr, const uint8_t *data, size_t len);

/**
@brief 主机 Mock：读用户区镜像一字节（越界 0xFF）
@param addr 地址
@return 字节
*/
uint8_t BoardGt32Mock_FlashByte(uint32_t addr);

/**
@brief 主机 Mock：下一次落在 [start,end) 的页编程失败一次
@param start 含
@param end 不含
*/
void BoardGt32Mock_InjectFailPpInRangeOnce(uint32_t start, uint32_t end);

/**
@brief 主机 Mock：下一次落在 [start,end) 的块/扇区擦失败一次
@param start 含
@param end 不含
*/
void BoardGt32Mock_InjectFailEraseInRangeOnce(uint32_t start, uint32_t end);
#endif /* BOARD_GT32_HOST_MOCK */

#endif /* CARDREADER_BOARD_GT32_H */
