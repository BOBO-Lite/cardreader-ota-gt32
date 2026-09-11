/**
@file board_gt32.c
@brief GT32 板级 SPI 抽象：默认未就绪桩；HOST_MOCK 下记录 opcode / 用户区镜像 / WIP
@note 引脚已确认（BOARD_GT32_PINS_CONFIRMED=1）：SPI2 PB13/14/15，GT32 CS=PB9；
      屏 CS=PB12 仅文档，Boot 不驱动。目标板寄存器初始化仍为对接项；≠ E3。
*/

#include "board_gt32.h"

#include <string.h>

#include "gt32_raw.h"
#include "ota_layout.h"

#ifdef BOARD_GT32_HOST_MOCK

#define BOARD_GT32_MOCK_OPCODE_CAP (256u)
#define BOARD_GT32_MOCK_ADDR_CAP   (128u)

static int s_mock_spi_fail;
static int s_mock_sticky_wip;
static int s_mock_wip;
static int s_cs_low;
static int s_frame_first;
static uint32_t s_tick_ms;
static uint8_t s_opcodes[BOARD_GT32_MOCK_OPCODE_CAP];
static size_t s_opcode_count;
static uint8_t s_last_cmd;
static size_t s_frame_byte_index;
static uint32_t s_feed_wdt_count;
static uint32_t s_erase_addrs[BOARD_GT32_MOCK_ADDR_CAP];
static size_t s_erase_addr_count;
static uint32_t s_pp_addrs[BOARD_GT32_MOCK_ADDR_CAP];
static size_t s_pp_addr_count;
static uint32_t s_addr_accum;
static int s_addr_bytes_left;
static int s_addr_kind; /* 0=none, 1=erase, 2=pp, 3=read */
static uint32_t s_op_addr;
static int s_data_phase; /* 1 after addr collected for read/pp */
static uint32_t s_event_seq;
static uint8_t s_flash[ota_gt32_user_size];
static int s_fail_pp_range_once;
static uint32_t s_fail_pp_start;
static uint32_t s_fail_pp_end;
static int s_fail_erase_range_once;
static uint32_t s_fail_erase_start;
static uint32_t s_fail_erase_end;

void BoardGt32Mock_Reset(void)
{
    s_mock_spi_fail = 0;
    s_mock_sticky_wip = 0;
    s_mock_wip = 0;
    s_cs_low = 0;
    s_frame_first = 0;
    s_frame_byte_index = 0u;
    s_tick_ms = 0u;
    s_opcode_count = 0u;
    s_last_cmd = 0u;
    s_feed_wdt_count = 0u;
    s_erase_addr_count = 0u;
    s_pp_addr_count = 0u;
    s_addr_accum = 0u;
    s_addr_bytes_left = 0;
    s_addr_kind = 0;
    s_op_addr = 0u;
    s_data_phase = 0;
    s_event_seq = 0u;
    s_fail_pp_range_once = 0;
    s_fail_pp_start = 0u;
    s_fail_pp_end = 0u;
    s_fail_erase_range_once = 0;
    s_fail_erase_start = 0u;
    s_fail_erase_end = 0u;
    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    (void)BOARD_GT32_PINS_CONFIRMED;
}

uint32_t BoardGt32Mock_BumpEventSeq(void)
{
    s_event_seq++;
    return s_event_seq;
}

uint32_t BoardGt32Mock_EventSeq(void)
{
    return s_event_seq;
}

void BoardGt32Mock_LoadFlash(uint32_t addr, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0u) {
        return;
    }
    if (addr >= ota_gt32_user_size) {
        return;
    }
    if (len > (ota_gt32_user_size - addr)) {
        return;
    }
    (void)memcpy(&s_flash[addr], data, len);
}

uint8_t BoardGt32Mock_FlashByte(uint32_t addr)
{
    if (addr >= ota_gt32_user_size) {
        return 0xFFu;
    }
    return s_flash[addr];
}

void BoardGt32Mock_InjectFailPpInRangeOnce(uint32_t start, uint32_t end)
{
    s_fail_pp_range_once = 1;
    s_fail_pp_start = start;
    s_fail_pp_end = end;
}

void BoardGt32Mock_InjectFailEraseInRangeOnce(uint32_t start, uint32_t end)
{
    s_fail_erase_range_once = 1;
    s_fail_erase_start = start;
    s_fail_erase_end = end;
}

void BoardGt32Mock_SetSpiFail(int fail)
{
    s_mock_spi_fail = (fail != 0) ? 1 : 0;
}

void BoardGt32Mock_SetStickyWip(int sticky)
{
    s_mock_sticky_wip = (sticky != 0) ? 1 : 0;
    if (s_mock_sticky_wip != 0) {
        s_mock_wip = 1;
    }
}

void BoardGt32Mock_AdvanceTick(uint32_t ms)
{
    s_tick_ms += ms;
}

size_t BoardGt32Mock_OpcodeCount(void)
{
    return s_opcode_count;
}

uint8_t BoardGt32Mock_OpcodeAt(size_t index)
{
    if (index >= s_opcode_count) {
        return 0xFFu;
    }
    return s_opcodes[index];
}

int BoardGt32Mock_HasOpcode(uint8_t opcode)
{
    size_t i;

    for (i = 0u; i < s_opcode_count; i++) {
        if (s_opcodes[i] == opcode) {
            return 1;
        }
    }
    return 0;
}

size_t BoardGt32Mock_OpcodeOccurrences(uint8_t opcode)
{
    size_t i;
    size_t n;

    n = 0u;
    for (i = 0u; i < s_opcode_count; i++) {
        if (s_opcodes[i] == opcode) {
            n++;
        }
    }
    return n;
}

uint32_t BoardGt32Mock_FeedWdtCount(void)
{
    return s_feed_wdt_count;
}

size_t BoardGt32Mock_EraseAddrCount(void)
{
    return s_erase_addr_count;
}

uint32_t BoardGt32Mock_EraseAddrAt(size_t index)
{
    if (index >= s_erase_addr_count) {
        return 0xFFFFFFFFu;
    }
    return s_erase_addrs[index];
}

size_t BoardGt32Mock_PpAddrCount(void)
{
    return s_pp_addr_count;
}

uint32_t BoardGt32Mock_PpAddrAt(size_t index)
{
    if (index >= s_pp_addr_count) {
        return 0xFFFFFFFFu;
    }
    return s_pp_addrs[index];
}

/**
@brief 记录本 CS 帧 opcode，并准备收地址
*/
static void BoardGt32Mock_OnOpcode(uint8_t opcode)
{
    if (s_opcode_count < BOARD_GT32_MOCK_OPCODE_CAP) {
        s_opcodes[s_opcode_count] = opcode;
        s_opcode_count++;
    }
    s_last_cmd = opcode;
    s_addr_accum = 0u;
    s_addr_bytes_left = 0;
    s_addr_kind = 0;
    s_data_phase = 0;
    s_op_addr = 0u;

    if ((opcode == gt32_cmd_page_program)
        || (opcode == gt32_cmd_sector_erase)
        || (opcode == gt32_cmd_block_erase_64k)
        || (opcode == gt32_cmd_read)) {
        s_addr_bytes_left = 3;
        if (opcode == gt32_cmd_page_program) {
            s_addr_kind = 2;
            s_mock_wip = 1;
        } else if (opcode == gt32_cmd_read) {
            s_addr_kind = 3;
        } else {
            s_addr_kind = 1;
            s_mock_wip = 1;
        }
    }
}

/**
@brief 擦除镜像中一块/扇区
*/
static int BoardGt32Mock_DoErase(uint32_t addr, uint32_t size)
{
    if (s_fail_erase_range_once != 0
        && addr >= s_fail_erase_start && addr < s_fail_erase_end) {
        s_fail_erase_range_once = 0;
        return -1;
    }
    if (addr >= ota_gt32_user_size || size > (ota_gt32_user_size - addr)) {
        return -1;
    }
    (void)memset(&s_flash[addr], 0xFF, (size_t)size);
    return 0;
}

/**
@brief 收齐 24-bit 地址后记入日志并进入数据相 / 执行擦除
*/
static int BoardGt32Mock_CommitAddr(void)
{
    s_op_addr = s_addr_accum;
    if (s_addr_kind == 1) {
        if (s_erase_addr_count < BOARD_GT32_MOCK_ADDR_CAP) {
            s_erase_addrs[s_erase_addr_count] = s_addr_accum;
            s_erase_addr_count++;
        }
        if (s_last_cmd == gt32_cmd_block_erase_64k) {
            if (BoardGt32Mock_DoErase(s_addr_accum, gt32_block64_size) != 0) {
                s_addr_kind = 0;
                return -1;
            }
        } else if (s_last_cmd == gt32_cmd_sector_erase) {
            if (BoardGt32Mock_DoErase(s_addr_accum, gt32_sector_size) != 0) {
                s_addr_kind = 0;
                return -1;
            }
        }
        s_addr_kind = 0;
    } else if (s_addr_kind == 2) {
        if (s_pp_addr_count < BOARD_GT32_MOCK_ADDR_CAP) {
            s_pp_addrs[s_pp_addr_count] = s_addr_accum;
            s_pp_addr_count++;
        }
        if (s_fail_pp_range_once != 0
            && s_addr_accum >= s_fail_pp_start
            && s_addr_accum < s_fail_pp_end) {
            s_fail_pp_range_once = 0;
            s_addr_kind = 0;
            s_data_phase = 0;
            return -1;
        }
        s_data_phase = 1;
        s_addr_kind = 0;
    } else if (s_addr_kind == 3) {
        s_data_phase = 1;
        s_addr_kind = 0;
    }
    return 0;
}

/**
@brief 生成 RDSR 状态字节并按策略清除 WIP
*/
static uint8_t BoardGt32Mock_TakeStatus(void)
{
    uint8_t sr;

    if (s_mock_sticky_wip != 0) {
        return gt32_sr_wip;
    }
    if (s_mock_wip != 0) {
        sr = gt32_sr_wip;
        s_mock_wip = 0;
        return sr;
    }
    return 0x00u;
}

int BoardGt32_SpiInit(void)
{
    if (s_mock_spi_fail != 0) {
        return (int)GT32_ERR_SPI;
    }
    return (int)GT32_OK;
}

int BoardGt32_CsAssert(void)
{
    if (s_mock_spi_fail != 0) {
        return (int)GT32_ERR_SPI;
    }
    s_cs_low = 1;
    s_frame_first = 1;
    s_frame_byte_index = 0u;
    s_data_phase = 0;
    return (int)GT32_OK;
}

int BoardGt32_CsDeassert(void)
{
    if (s_mock_spi_fail != 0) {
        return (int)GT32_ERR_SPI;
    }
    s_cs_low = 0;
    s_frame_first = 0;
    s_frame_byte_index = 0u;
    s_addr_bytes_left = 0;
    s_addr_kind = 0;
    s_data_phase = 0;
    return (int)GT32_OK;
}

int BoardGt32_SpiTransfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    size_t i;
    uint8_t out_b;
    uint8_t in_b;

    if (s_mock_spi_fail != 0) {
        return (int)GT32_ERR_SPI;
    }
    if (len == 0u) {
        return (int)GT32_OK;
    }
    if (s_cs_low == 0) {
        return (int)GT32_ERR_SPI;
    }

    for (i = 0u; i < len; i++) {
        out_b = (tx != NULL) ? tx[i] : 0xFFu;
        in_b = 0xFFu;

        if (s_frame_first != 0) {
            BoardGt32Mock_OnOpcode(out_b);
            s_frame_first = 0;
        } else if (s_addr_bytes_left > 0) {
            s_addr_accum = (s_addr_accum << 8) | (uint32_t)out_b;
            s_addr_bytes_left--;
            if (s_addr_bytes_left == 0) {
                if (BoardGt32Mock_CommitAddr() != 0) {
                    return (int)GT32_ERR_SPI;
                }
            }
        } else if (s_data_phase != 0) {
            if (s_last_cmd == gt32_cmd_read) {
                if (s_op_addr < ota_gt32_user_size) {
                    in_b = s_flash[s_op_addr];
                    s_op_addr++;
                }
            } else if (s_last_cmd == gt32_cmd_page_program) {
                if (s_op_addr < ota_gt32_user_size) {
                    s_flash[s_op_addr] = (uint8_t)(s_flash[s_op_addr] & out_b);
                    s_op_addr++;
                }
            }
        }

        if ((s_last_cmd == gt32_cmd_rdsr) && (s_frame_byte_index >= 1u)) {
            in_b = BoardGt32Mock_TakeStatus();
        }

        if (rx != NULL) {
            rx[i] = in_b;
        }
        s_frame_byte_index++;
    }

    return (int)GT32_OK;
}

uint32_t BoardGt32_GetTickMs(void)
{
    if (s_mock_sticky_wip != 0) {
        s_tick_ms += 50u;
    }
    return s_tick_ms;
}

void BoardGt32_DelayMs(uint32_t ms)
{
    s_tick_ms += ms;
}

void BoardGt32_FeedWdt(void)
{
    s_feed_wdt_count++;
}

#else /* !BOARD_GT32_HOST_MOCK */

int BoardGt32_SpiInit(void)
{
    (void)BOARD_GT32_PINS_CONFIRMED;
    (void)board_gt32_spi_hz_max;
    return (int)GT32_ERR_NOT_READY;
}

int BoardGt32_CsAssert(void)
{
    return (int)GT32_ERR_NOT_READY;
}

int BoardGt32_CsDeassert(void)
{
    return (int)GT32_ERR_NOT_READY;
}

int BoardGt32_SpiTransfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    (void)tx;
    (void)rx;
    (void)len;
    return (int)GT32_ERR_NOT_READY;
}

uint32_t BoardGt32_GetTickMs(void)
{
    return 0u;
}

void BoardGt32_DelayMs(uint32_t ms)
{
    (void)ms;
}

void BoardGt32_FeedWdt(void)
{
}

#endif /* BOARD_GT32_HOST_MOCK */
