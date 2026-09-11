/**
@file gt32_raw.h
@brief GT32L32S0140 薄 SPI 驱动（读/页写/扇区擦/块擦/RDSR/Secondary 擦写；禁止 Chip Erase）
*/

#ifndef CARDREADER_GT32_RAW_H
#define CARDREADER_GT32_RAW_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* 白名单命令（R02 §3.8）；禁止定义 Chip Erase（整片擦除）操作码宏              */
/* ------------------------------------------------------------------------- */

#define gt32_cmd_read             (0x03u)
#define gt32_cmd_wren             (0x06u)
#define gt32_cmd_wrdi             (0x04u)
#define gt32_cmd_page_program     (0x02u)
#define gt32_cmd_sector_erase     (0x20u)
#define gt32_cmd_block_erase_64k  (0xD8u)
#define gt32_cmd_rdsr             (0x05u)

/* Fast Read 可选加速读本切片省略；Chip Erase 宏不得存在（见主机测试） */

/* ------------------------------------------------------------------------- */
/* 几何与 WIP 超时（MCU 轮询硬限）                                            */
/* ------------------------------------------------------------------------- */

#define gt32_page_size            (256u)
#define gt32_sector_size          (4096u)
#define gt32_block64_size         (65536u)

#define gt32_sr_wip               (0x01u)

#define gt32_wip_timeout_pp_ms    (100u)
#define gt32_wip_timeout_se_ms    (500u)
#define gt32_wip_timeout_be_ms    (2000u)

/**
@brief 驱动状态：0 成功，负值失败（API-004）
*/
typedef enum {
    GT32_OK = 0,
    GT32_ERR_NULL = -1,
    GT32_ERR_PARAM = -2,
    GT32_ERR_SPI = -3,
    GT32_ERR_TIMEOUT = -4,
    GT32_ERR_PROBE = -5,
    GT32_ERR_NOT_READY = -6,
    GT32_ERR_RANGE = -7,
    GT32_ERR_LEN = -8,
    GT32_ERR_ALIGN = -9
} GT32_Status_t;

/**
@brief 初始化底层 SPI（经 board_gt32）
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_Init(void);

/**
@brief 探测 GT32：Init + RDSR 可读；失败返回 GT32_ERR_PROBE（AC-YM-22 准备）
@note 不触碰片内 App；不改写外挂用户区
@return GT32_OK 或 GT32_ERR_PROBE / 其它负码
*/
GT32_Status_t GT32_Probe(void);

/**
@brief 读状态寄存器（RDSR 05h）
@param out_sr 输出状态字节
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_ReadStatus(uint8_t *out_sr);

/**
@brief 读数据（03h + 24-bit 地址）
@param addr 用户区起始地址
@param buf 输出缓冲
@param len 字节数（>0）
@return GT32_OK 或负错误码（越界 RANGE）
*/
GT32_Status_t GT32_Read(uint32_t addr, uint8_t *buf, size_t len);

/**
@brief 页编程（WREN + 02h）；len 1..256 且不得跨 256B 页边界；WIP≤100ms
@param addr 起始地址
@param data 输入
@param len 长度
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_PageProgram(uint32_t addr, const uint8_t *data, size_t len);

/**
@brief 扇区擦除 4 KiB（WREN + 20h）；addr 须 4K 对齐；WIP≤500ms
@param addr 扇区起始
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_SectorErase4K(uint32_t addr);

/**
@brief 块擦除 64 KiB（WREN + D8h）；addr 须 64K 对齐；WIP≤2000ms
@param addr 块起始
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_BlockErase64K(uint32_t addr);

/**
@brief 擦除 Secondary 全槽 128 KiB（固定全槽；不以 declared_size 截短）
@note 策略：两次 BlockErase64K（D8h）于 ota_gt32_secondary_off 与 +64KiB；
      段间调用 BoardGt32_FeedWdt()。禁止 Chip Erase。实板擦除时长「待 AC-YM-05」。
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_EraseSecondarySlot(void);

/**
@brief 向 Secondary 槽按页写入镜像片段（AC-YM-02 子集）
@param offset 相对 Secondary 基址的偏移
@param data 输入缓冲
@param len 字节数（>0）
@param declared_size 声明镜像长度；须 ∈ [ota_app_image_min, ota_app_image_max]
                      且 ≤ ota_gt32_secondary_size
@note 拒绝 offset+len 超出 declared_size 或 Secondary 容量；按页拆分调用 GT32_PageProgram。
@return GT32_OK 或负错误码（NULL/PARAM/LEN/RANGE/…）
*/
GT32_Status_t GT32_WriteSecondary(uint32_t offset, const uint8_t *data, size_t len,
                                  uint32_t declared_size);


/**
@brief 擦除 LastGood 全槽 128 KiB（两次 D8h 于 0x20000 / 0x30000）
@note 段间 FeedWdt；禁止 Chip Erase。不触碰 Secondary。
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_EraseLastGoodSlot(void);

/**
@brief 向 LastGood 槽按页写入（相对 LastGood 基址）
@param offset 相对 ota_gt32_lastgood_off
@param data 输入
@param len 长度（>0）
@note 拒绝写出 ota_app_image_max 或 LastGood 槽外；按页拆分 PageProgram。
@return GT32_OK 或负错误码
*/
GT32_Status_t GT32_WriteLastGood(uint32_t offset, const uint8_t *data, size_t len);

#ifdef BOARD_GT32_HOST_MOCK
/**
@brief 主机：LastGood 擦除调用次数
*/
size_t GT32Mock_LastGoodEraseCount(void);

/**
@brief 主机：LastGood 写入累计字节
*/
size_t GT32Mock_LastGoodWriteBytes(void);

/**
@brief 主机：最近一次 LastGood 写完成事件序号
*/
uint32_t GT32Mock_LastGoodWriteSeq(void);

/**
@brief 主机：复位 LastGood 计数（不清 Flash 镜像）
*/
void GT32Mock_ResetLastGoodStats(void);
#endif

#endif /* CARDREADER_GT32_RAW_H */
