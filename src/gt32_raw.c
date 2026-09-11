/**
@file gt32_raw.c
@brief GT32 薄驱动实现：RDSR/读/页写/4K 扇区擦/64K 块擦/Secondary 全槽擦与页写；无 Chip Erase
*/

#include "gt32_raw.h"

#include "board_gt32.h"
#include "ota_layout.h"

/**
@brief 将板级返回码映射为 GT32_Status_t
@param rc BoardGt32_* 返回值
@return GT32_Status_t
*/
static GT32_Status_t GT32_MapBoard(int rc)
{
    if (rc == 0) {
        return GT32_OK;
    }
    if (rc == (int)GT32_ERR_NOT_READY) {
        return GT32_ERR_NOT_READY;
    }
    if (rc == (int)GT32_ERR_SPI) {
        return GT32_ERR_SPI;
    }
    return GT32_ERR_SPI;
}

/**
@brief 校验 [addr, addr+len) 落在 GT32 用户区
@param addr 起始
@param len 长度
@return GT32_OK 或 GT32_ERR_RANGE / GT32_ERR_LEN
*/
static GT32_Status_t GT32_CheckUserRange(uint32_t addr, size_t len)
{
    uint32_t end;

    if (len == 0u) {
        return GT32_ERR_LEN;
    }
    if (addr > ota_gt32_user_end) {
        return GT32_ERR_RANGE;
    }
    if (len > (size_t)(ota_gt32_user_end - addr + 1u)) {
        return GT32_ERR_RANGE;
    }
    end = addr + (uint32_t)len - 1u;
    if (end < addr) {
        return GT32_ERR_RANGE;
    }
    if (end > ota_gt32_user_end) {
        return GT32_ERR_RANGE;
    }
    return GT32_OK;
}

/**
@brief CS 帧内传输；失败时尽量拉高 CS
@param tx 发送
@param rx 接收
@param len 长度
@return GT32_Status_t
*/
static GT32_Status_t GT32_TransferFramed(const uint8_t *tx, uint8_t *rx, size_t len)
{
    GT32_Status_t st;
    int rc;

    rc = BoardGt32_CsAssert();
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        return st;
    }

    rc = BoardGt32_SpiTransfer(tx, rx, len);
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        (void)BoardGt32_CsDeassert();
        return st;
    }

    rc = BoardGt32_CsDeassert();
    return GT32_MapBoard(rc);
}

/**
@brief 发送 Write Enable（06h）
@return GT32_Status_t
*/
static GT32_Status_t GT32_WriteEnable(void)
{
    uint8_t cmd;

    cmd = gt32_cmd_wren;
    return GT32_TransferFramed(&cmd, NULL, 1u);
}

/**
@brief 轮询 RDSR 直至 WIP 清或超时
@param timeout_ms 超时毫秒
@return GT32_OK / GT32_ERR_TIMEOUT / 其它
*/
static GT32_Status_t GT32_WaitWipClear(uint32_t timeout_ms)
{
    uint32_t start;
    uint8_t sr;
    GT32_Status_t st;

    start = BoardGt32_GetTickMs();
    for (;;) {
        BoardGt32_FeedWdt();
        st = GT32_ReadStatus(&sr);
        if (st != GT32_OK) {
            return st;
        }
        if ((sr & gt32_sr_wip) == 0u) {
            return GT32_OK;
        }
        if ((BoardGt32_GetTickMs() - start) >= timeout_ms) {
            return GT32_ERR_TIMEOUT;
        }
    }
}

/**
@brief 发送 1 字节命令 + 24-bit 地址（大端地址字节序，SPI Flash 惯例）
@param cmd 命令
@param addr 24-bit 地址
@return GT32_Status_t
*/
static GT32_Status_t GT32_CmdAddr(uint8_t cmd, uint32_t addr)
{
    uint8_t buf[4];

    buf[0] = cmd;
    buf[1] = (uint8_t)((addr >> 16) & 0xFFu);
    buf[2] = (uint8_t)((addr >> 8) & 0xFFu);
    buf[3] = (uint8_t)(addr & 0xFFu);
    return GT32_TransferFramed(buf, NULL, 4u);
}

GT32_Status_t GT32_Init(void)
{
    return GT32_MapBoard(BoardGt32_SpiInit());
}

GT32_Status_t GT32_ReadStatus(uint8_t *out_sr)
{
    uint8_t tx[2];
    uint8_t rx[2];
    GT32_Status_t st;

    if (out_sr == NULL) {
        return GT32_ERR_NULL;
    }

    tx[0] = gt32_cmd_rdsr;
    tx[1] = 0xFFu;
    rx[0] = 0xFFu;
    rx[1] = 0xFFu;

    st = GT32_TransferFramed(tx, rx, 2u);
    if (st != GT32_OK) {
        return st;
    }
    *out_sr = rx[1];
    return GT32_OK;
}

GT32_Status_t GT32_Probe(void)
{
    GT32_Status_t st;
    uint8_t sr;

    st = GT32_Init();
    if (st != GT32_OK) {
        return GT32_ERR_PROBE;
    }

    st = GT32_ReadStatus(&sr);
    if (st != GT32_OK) {
        return GT32_ERR_PROBE;
    }

    (void)sr;
    return GT32_OK;
}

GT32_Status_t GT32_Read(uint32_t addr, uint8_t *buf, size_t len)
{
    uint8_t hdr[4];
    GT32_Status_t st;
    int rc;

    if (buf == NULL) {
        return GT32_ERR_NULL;
    }
    st = GT32_CheckUserRange(addr, len);
    if (st != GT32_OK) {
        return st;
    }

    hdr[0] = gt32_cmd_read;
    hdr[1] = (uint8_t)((addr >> 16) & 0xFFu);
    hdr[2] = (uint8_t)((addr >> 8) & 0xFFu);
    hdr[3] = (uint8_t)(addr & 0xFFu);

    rc = BoardGt32_CsAssert();
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        return st;
    }

    rc = BoardGt32_SpiTransfer(hdr, NULL, 4u);
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        (void)BoardGt32_CsDeassert();
        return st;
    }

    rc = BoardGt32_SpiTransfer(NULL, buf, len);
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        (void)BoardGt32_CsDeassert();
        return st;
    }

    return GT32_MapBoard(BoardGt32_CsDeassert());
}

GT32_Status_t GT32_PageProgram(uint32_t addr, const uint8_t *data, size_t len)
{
    uint8_t hdr[4];
    GT32_Status_t st;
    int rc;
    uint32_t page_off;
    uint32_t room;

    if (data == NULL) {
        return GT32_ERR_NULL;
    }
    if ((len == 0u) || (len > gt32_page_size)) {
        return GT32_ERR_LEN;
    }
    st = GT32_CheckUserRange(addr, len);
    if (st != GT32_OK) {
        return st;
    }

    page_off = addr & (gt32_page_size - 1u);
    room = gt32_page_size - page_off;
    if (len > (size_t)room) {
        return GT32_ERR_ALIGN;
    }

    st = GT32_WriteEnable();
    if (st != GT32_OK) {
        return st;
    }

    hdr[0] = gt32_cmd_page_program;
    hdr[1] = (uint8_t)((addr >> 16) & 0xFFu);
    hdr[2] = (uint8_t)((addr >> 8) & 0xFFu);
    hdr[3] = (uint8_t)(addr & 0xFFu);

    rc = BoardGt32_CsAssert();
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        return st;
    }

    rc = BoardGt32_SpiTransfer(hdr, NULL, 4u);
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        (void)BoardGt32_CsDeassert();
        return st;
    }

    rc = BoardGt32_SpiTransfer(data, NULL, len);
    st = GT32_MapBoard(rc);
    if (st != GT32_OK) {
        (void)BoardGt32_CsDeassert();
        return st;
    }

    st = GT32_MapBoard(BoardGt32_CsDeassert());
    if (st != GT32_OK) {
        return st;
    }

    return GT32_WaitWipClear(gt32_wip_timeout_pp_ms);
}

GT32_Status_t GT32_SectorErase4K(uint32_t addr)
{
    GT32_Status_t st;

    if ((addr & (gt32_sector_size - 1u)) != 0u) {
        return GT32_ERR_ALIGN;
    }
    st = GT32_CheckUserRange(addr, gt32_sector_size);
    if (st != GT32_OK) {
        return st;
    }

    st = GT32_WriteEnable();
    if (st != GT32_OK) {
        return st;
    }

    st = GT32_CmdAddr(gt32_cmd_sector_erase, addr);
    if (st != GT32_OK) {
        return st;
    }

    return GT32_WaitWipClear(gt32_wip_timeout_se_ms);
}

GT32_Status_t GT32_BlockErase64K(uint32_t addr)
{
    GT32_Status_t st;

    if ((addr & (gt32_block64_size - 1u)) != 0u) {
        return GT32_ERR_ALIGN;
    }
    st = GT32_CheckUserRange(addr, gt32_block64_size);
    if (st != GT32_OK) {
        return st;
    }

    st = GT32_WriteEnable();
    if (st != GT32_OK) {
        return st;
    }

    st = GT32_CmdAddr(gt32_cmd_block_erase_64k, addr);
    if (st != GT32_OK) {
        return st;
    }

    return GT32_WaitWipClear(gt32_wip_timeout_be_ms);
}

GT32_Status_t GT32_EraseSecondarySlot(void)
{
    GT32_Status_t st;
    uint32_t base;

    /* 固定擦满 Secondary 128 KiB：两段 D8h（0x00000 / 0x10000）；禁止 CE。
       实板擦除时长「待 AC-YM-05」。 */
    base = ota_gt32_secondary_off;

    st = GT32_BlockErase64K(base);
    if (st != GT32_OK) {
        return st;
    }

    /* 块擦段间喂狗，避免长擦除路径触发看门狗 */
    BoardGt32_FeedWdt();

    st = GT32_BlockErase64K(base + gt32_block64_size);
    return st;
}

GT32_Status_t GT32_WriteSecondary(uint32_t offset, const uint8_t *data, size_t len,
                                  uint32_t declared_size)
{
    GT32_Status_t st;
    uint32_t abs_addr;
    size_t done;
    size_t chunk;
    uint32_t page_off;
    uint32_t room;

    if (data == NULL) {
        return GT32_ERR_NULL;
    }
    if (len == 0u) {
        return GT32_ERR_LEN;
    }

    /* declared_size ∈ [min, max] 且不超过 Secondary 容量 */
    if ((declared_size < ota_app_image_min)
        || (declared_size > ota_app_image_max)
        || (declared_size > ota_gt32_secondary_size)) {
        return GT32_ERR_PARAM;
    }

    /* 不得写出声明镜像长度 */
    if (offset >= declared_size) {
        return GT32_ERR_RANGE;
    }
    if (len > (size_t)(declared_size - offset)) {
        return GT32_ERR_RANGE;
    }

    /* 不得写出 Secondary 槽 */
    if (offset >= ota_gt32_secondary_size) {
        return GT32_ERR_RANGE;
    }
    if (len > (size_t)(ota_gt32_secondary_size - offset)) {
        return GT32_ERR_RANGE;
    }

    abs_addr = ota_gt32_secondary_off + offset;
    done = 0u;
    while (done < len) {
        page_off = (abs_addr + (uint32_t)done) & (gt32_page_size - 1u);
        room = gt32_page_size - page_off;
        chunk = len - done;
        if (chunk > (size_t)room) {
            chunk = (size_t)room;
        }
        st = GT32_PageProgram(abs_addr + (uint32_t)done, data + done, chunk);
        if (st != GT32_OK) {
            return st;
        }
        done += chunk;
    }

    return GT32_OK;
}

#ifdef BOARD_GT32_HOST_MOCK
static size_t s_lg_erase_count;
static size_t s_lg_write_bytes;
static uint32_t s_lg_write_seq;

size_t GT32Mock_LastGoodEraseCount(void)
{
    return s_lg_erase_count;
}

size_t GT32Mock_LastGoodWriteBytes(void)
{
    return s_lg_write_bytes;
}

uint32_t GT32Mock_LastGoodWriteSeq(void)
{
    return s_lg_write_seq;
}

void GT32Mock_ResetLastGoodStats(void)
{
    s_lg_erase_count = 0u;
    s_lg_write_bytes = 0u;
    s_lg_write_seq = 0u;
}
#endif

GT32_Status_t GT32_EraseLastGoodSlot(void)
{
    GT32_Status_t st;
    uint32_t base;

    /* 固定擦满 LastGood 128 KiB：两段 D8h（0x20000 / 0x30000）；禁止 CE。 */
    base = ota_gt32_lastgood_off;

    st = GT32_BlockErase64K(base);
    if (st != GT32_OK) {
        return st;
    }
    BoardGt32_FeedWdt();
    st = GT32_BlockErase64K(base + gt32_block64_size);
#ifdef BOARD_GT32_HOST_MOCK
    if (st == GT32_OK) {
        s_lg_erase_count++;
    }
#endif
    return st;
}

GT32_Status_t GT32_WriteLastGood(uint32_t offset, const uint8_t *data, size_t len)
{
    GT32_Status_t st;
    uint32_t abs_addr;
    size_t done;
    size_t chunk;
    uint32_t page_off;
    uint32_t room;

    if (data == NULL) {
        return GT32_ERR_NULL;
    }
    if (len == 0u) {
        return GT32_ERR_LEN;
    }

    /* 整槽备份上限 = App 镜像最大；亦不得超过 LastGood 槽 */
    if (offset >= ota_app_image_max) {
        return GT32_ERR_RANGE;
    }
    if (len > (size_t)(ota_app_image_max - offset)) {
        return GT32_ERR_RANGE;
    }
    if (offset >= ota_gt32_lastgood_size) {
        return GT32_ERR_RANGE;
    }
    if (len > (size_t)(ota_gt32_lastgood_size - offset)) {
        return GT32_ERR_RANGE;
    }

    abs_addr = ota_gt32_lastgood_off + offset;
    done = 0u;
    while (done < len) {
        page_off = (abs_addr + (uint32_t)done) & (gt32_page_size - 1u);
        room = gt32_page_size - page_off;
        chunk = len - done;
        if (chunk > (size_t)room) {
            chunk = (size_t)room;
        }
        st = GT32_PageProgram(abs_addr + (uint32_t)done, data + done, chunk);
        if (st != GT32_OK) {
            return st;
        }
        done += chunk;
    }

#ifdef BOARD_GT32_HOST_MOCK
    s_lg_write_bytes += len;
    s_lg_write_seq = BoardGt32Mock_BumpEventSeq();
#endif
    return GT32_OK;
}
