/**
@file test_gt32_raw.c
@brief gt32_raw 主机 Mock 单测（Slice 2 + Slice 3）
*/

#include <stdio.h>
#include <string.h>

#include "board_gt32.h"
#include "gt32_raw.h"
#include "ota_layout.h"

#ifdef gt32_cmd_chip_erase
#error "gt32_cmd_chip_erase must not be defined (Chip Erase forbidden)"
#endif

static int g_failures;

/**
@brief 断言辅助
@param cond 条件
@param msg 失败说明
*/
static void Expect(int cond, const char *msg)
{
    if (!cond) {
        (void)printf("FAIL: %s\n", msg);
        g_failures++;
    } else {
        (void)printf("PASS: %s\n", msg);
    }
}

/**
@brief 确认已记录 opcode 序列中不含 CE
*/
static void AssertNoChipEraseOpcodes(void)
{
    size_t i;
    uint8_t op;
    int clean = 1;

    for (i = 0u; i < BoardGt32Mock_OpcodeCount(); i++) {
        op = BoardGt32Mock_OpcodeAt(i);
        if ((op == 0x60u) || (op == 0xC7u)) {
            clean = 0;
            break;
        }
    }
    Expect(clean != 0, "opcode log must not contain CE opcodes");
}

/**
@brief 用例：Mock 上 Probe 成功
*/
static void Test_ProbeOk(void)
{
    GT32_Status_t st;

    BoardGt32Mock_Reset();
    st = GT32_Probe();
    Expect(st == GT32_OK, "probe OK on mock");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_rdsr), "probe issues RDSR 05h");
    AssertNoChipEraseOpcodes();
}

/**
@brief 用例：SPI 失败时 Probe → GT32_ERR_PROBE
*/
static void Test_ProbeFail(void)
{
    GT32_Status_t st;

    BoardGt32Mock_Reset();
    BoardGt32Mock_SetSpiFail(1);
    st = GT32_Probe();
    Expect(st == GT32_ERR_PROBE, "probe fail when SPI fails");
}

/**
@brief 用例：RDSR / Read 命令码
*/
static void Test_ReadAndRdsr(void)
{
    GT32_Status_t st;
    uint8_t sr;
    uint8_t buf[8];

    BoardGt32Mock_Reset();
    st = GT32_Init();
    Expect(st == GT32_OK, "init OK");

    st = GT32_ReadStatus(&sr);
    Expect(st == GT32_OK, "RDSR OK");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_rdsr), "RDSR opcode 05h");

    BoardGt32Mock_Reset();
    (void)GT32_Init();
    (void)memset(buf, 0, sizeof(buf));
    st = GT32_Read(0x00000u, buf, sizeof(buf));
    Expect(st == GT32_OK, "Read OK");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_read), "Read opcode 03h");
    AssertNoChipEraseOpcodes();
}

/**
@brief 用例：页写发出 WREN 再 02h
*/
static void Test_PageProgramOpcodes(void)
{
    GT32_Status_t st;
    uint8_t data[16];
    size_t i;

    BoardGt32Mock_Reset();
    (void)memset(data, 0xA5, sizeof(data));
    st = GT32_PageProgram(0x00010u, data, sizeof(data));
    Expect(st == GT32_OK, "PageProgram OK");
    Expect(BoardGt32Mock_OpcodeCount() >= 2u, "PP has >=2 opcodes");
    Expect(BoardGt32Mock_OpcodeAt(0) == gt32_cmd_wren, "PP first opcode WREN 06h");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_page_program), "PP opcode 02h");
    /* WREN 须先于 PP */
    {
        int saw_wren = 0;
        int order_ok = 0;
        for (i = 0u; i < BoardGt32Mock_OpcodeCount(); i++) {
            if (BoardGt32Mock_OpcodeAt(i) == gt32_cmd_wren) {
                saw_wren = 1;
            }
            if (saw_wren && BoardGt32Mock_OpcodeAt(i) == gt32_cmd_page_program) {
                order_ok = 1;
                break;
            }
        }
        Expect(order_ok, "WREN before PageProgram");
    }
    AssertNoChipEraseOpcodes();
}

/**
@brief 用例：扇区擦 20h / 块擦 D8h
*/
static void Test_EraseOpcodes(void)
{
    GT32_Status_t st;

    BoardGt32Mock_Reset();
    st = GT32_SectorErase4K(0x00000u);
    Expect(st == GT32_OK, "SectorErase4K OK");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_wren), "SE issues WREN");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_sector_erase), "SE opcode 20h");

    BoardGt32Mock_Reset();
    st = GT32_BlockErase64K(0x00000u);
    Expect(st == GT32_OK, "BlockErase64K OK");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_wren), "BE issues WREN");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_block_erase_64k), "BE opcode D8h");
    AssertNoChipEraseOpcodes();
}

/**
@brief 用例：拒绝未对齐擦除与跨页页写
*/
static void Test_RejectAlignAndCrossPage(void)
{
    GT32_Status_t st;
    uint8_t data[4];

    BoardGt32Mock_Reset();
    st = GT32_SectorErase4K(0x00100u);
    Expect(st == GT32_ERR_ALIGN, "reject unaligned sector erase");

    BoardGt32Mock_Reset();
    st = GT32_BlockErase64K(0x01000u);
    Expect(st == GT32_ERR_ALIGN, "reject unaligned block erase");

    BoardGt32Mock_Reset();
    (void)memset(data, 0, sizeof(data));
    /* 0x00FE + 4 字节跨过 0x100 页界 */
    st = GT32_PageProgram(0x00FEu, data, 4u);
    Expect(st == GT32_ERR_ALIGN, "reject page-crossing PageProgram");
}

/**
@brief 用例：越界地址
*/
static void Test_RejectRange(void)
{
    GT32_Status_t st;
    uint8_t buf[4];
    uint8_t data[1];

    BoardGt32Mock_Reset();
    st = GT32_Read(0x80000u, buf, 1u);
    Expect(st == GT32_ERR_RANGE, "reject read past user area");

    BoardGt32Mock_Reset();
    data[0] = 0x11u;
    st = GT32_PageProgram(0x7FFFFu, data, 2u);
    Expect(st == GT32_ERR_RANGE, "reject PP past user end");
}

/**
@brief 用例：粘滞 WIP → TIMEOUT
*/
static void Test_WipTimeout(void)
{
    GT32_Status_t st;
    uint8_t data[1];

    BoardGt32Mock_Reset();
    BoardGt32Mock_SetStickyWip(1);
    data[0] = 0x22u;
    st = GT32_PageProgram(0x00000u, data, 1u);
    Expect(st == GT32_ERR_TIMEOUT, "WIP sticky → GT32_ERR_TIMEOUT");
}

/**
@brief 用例：空指针
*/
static void Test_NullArgs(void)
{
    Expect(GT32_ReadStatus(NULL) == GT32_ERR_NULL, "ReadStatus NULL");
    Expect(GT32_Read(0u, NULL, 1u) == GT32_ERR_NULL, "Read NULL");
    Expect(GT32_PageProgram(0u, NULL, 1u) == GT32_ERR_NULL, "PageProgram NULL");
}

/**
@brief 静态确认：白名单宏存在且无 CE 宏（编译期已 #error）
*/
static void Test_CommandWhitelist(void)
{
    Expect(gt32_cmd_read == 0x03u, "cmd read 03h");
    Expect(gt32_cmd_rdsr == 0x05u, "cmd rdsr 05h");
    Expect(gt32_cmd_wren == 0x06u, "cmd wren 06h");
    Expect(gt32_cmd_page_program == 0x02u, "cmd pp 02h");
    Expect(gt32_cmd_sector_erase == 0x20u, "cmd se 20h");
    Expect(gt32_cmd_block_erase_64k == 0xD8u, "cmd be D8h");
    Expect(BOARD_GT32_PINS_CONFIRMED == 1, "pins confirmed");
    Expect(BOARD_GT32_SPI_SCK_PIN == 13u, "SCK PB13");
    Expect(BOARD_GT32_SPI_MISO_PIN == 14u, "MISO PB14");
    Expect(BOARD_GT32_SPI_MOSI_PIN == 15u, "MOSI PB15");
    Expect(BOARD_GT32_CS_PIN == 9u, "GT32 CS PB9");
    Expect(BOARD_DISPLAY_CS_PIN == 12u, "display CS PB12");
    Expect(board_gt32_spi_hz_max == 9000000u, "SPI max 9 MHz");
}

/* ------------------------------------------------------------------------- */
/* Slice 3                                                                   */
/* ------------------------------------------------------------------------- */

/**
@brief 用例：EraseSecondarySlot 恰两次 D8h，地址 Secondary 基址与 +64K，喂狗，不碰 LastGood，无 CE
*/
static void Test_EraseSecondarySlot(void)
{
    GT32_Status_t st;
    size_t i;
    int touched_lastgood;
    uint32_t feeds_before;
    uint32_t feeds_after;

    BoardGt32Mock_Reset();
    feeds_before = BoardGt32Mock_FeedWdtCount();
    st = GT32_EraseSecondarySlot();
    feeds_after = BoardGt32Mock_FeedWdtCount();

    Expect(st == GT32_OK, "EraseSecondarySlot OK");
    Expect(BoardGt32Mock_OpcodeOccurrences(gt32_cmd_block_erase_64k) == 2u,
           "EraseSecondarySlot issues exactly two D8h");
    Expect(BoardGt32Mock_OpcodeOccurrences(gt32_cmd_sector_erase) == 0u,
           "EraseSecondarySlot issues zero 20h (prefer 2xD8h)");
    Expect(BoardGt32Mock_HasOpcode(0x60u) == 0, "no CE 60h");
    Expect(BoardGt32Mock_HasOpcode(0xC7u) == 0, "no CE C7h");
    AssertNoChipEraseOpcodes();

    Expect(BoardGt32Mock_EraseAddrCount() == 2u, "two erase addresses logged");
    Expect(BoardGt32Mock_EraseAddrAt(0) == ota_gt32_secondary_off,
           "first D8h at Secondary base 0x00000");
    Expect(BoardGt32Mock_EraseAddrAt(1) == (ota_gt32_secondary_off + gt32_block64_size),
           "second D8h at Secondary base+64K 0x10000");

    touched_lastgood = 0;
    for (i = 0u; i < BoardGt32Mock_EraseAddrCount(); i++) {
        if (BoardGt32Mock_EraseAddrAt(i) == ota_gt32_lastgood_off) {
            touched_lastgood = 1;
        }
        if (BoardGt32Mock_EraseAddrAt(i) >= ota_gt32_lastgood_off) {
            touched_lastgood = 1;
        }
    }
    Expect(touched_lastgood == 0, "EraseSecondarySlot does not touch LastGood (no erase at 0x20000+)");

    Expect(feeds_after > feeds_before,
           "FeedWdt called during EraseSecondarySlot (between blocks / WIP poll)");
}

/**
@brief 用例：WriteSecondary 合法范围内成功且无 CE
*/
static void Test_WriteSecondaryOk(void)
{
    GT32_Status_t st;
    uint8_t data[32];

    BoardGt32Mock_Reset();
    (void)memset(data, 0x5A, sizeof(data));
    st = GT32_WriteSecondary(0u, data, sizeof(data), 1024u);
    Expect(st == GT32_OK, "WriteSecondary OK within declared_size");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_page_program), "WriteSecondary emits PP 02h");
    Expect(BoardGt32Mock_PpAddrCount() >= 1u, "WriteSecondary logs PP address");
    Expect(BoardGt32Mock_PpAddrAt(0) == ota_gt32_secondary_off,
           "WriteSecondary PP at Secondary base");
    AssertNoChipEraseOpcodes();
}

/**
@brief 用例：WriteSecondary 拒绝超出 declared_size / 槽外 / 非法 declared
*/
static void Test_WriteSecondaryReject(void)
{
    GT32_Status_t st;
    uint8_t data[16];

    (void)memset(data, 0x11, sizeof(data));

    BoardGt32Mock_Reset();
    /* offset+len 超出 declared_size */
    st = GT32_WriteSecondary(100u, data, 16u, 110u);
    Expect(st == GT32_ERR_RANGE, "reject write that would exceed declared_size");
    Expect(BoardGt32Mock_HasOpcode(gt32_cmd_page_program) == 0,
           "no PP when exceeding declared_size");

    BoardGt32Mock_Reset();
    /* offset 已超出 declared */
    st = GT32_WriteSecondary(110u, data, 1u, 110u);
    Expect(st == GT32_ERR_RANGE, "reject offset beyond declared_size");

    BoardGt32Mock_Reset();
    /* offset 超出 Secondary 容量（即使 declared 合法也不应到达此处；用非法大 offset） */
    st = GT32_WriteSecondary(ota_gt32_secondary_size, data, 1u, ota_app_image_min);
    Expect(st == GT32_ERR_RANGE, "reject offset beyond Secondary slot");

    BoardGt32Mock_Reset();
    st = GT32_WriteSecondary(0u, data, 1u, 0u);
    Expect(st == GT32_ERR_PARAM, "reject declared_size below min");

    BoardGt32Mock_Reset();
    st = GT32_WriteSecondary(0u, data, 1u, ota_app_image_max + 1u);
    Expect(st == GT32_ERR_PARAM, "reject declared_size above max");

    BoardGt32Mock_Reset();
    st = GT32_WriteSecondary(0u, NULL, 1u, 64u);
    Expect(st == GT32_ERR_NULL, "WriteSecondary NULL data");

    BoardGt32Mock_Reset();
    st = GT32_WriteSecondary(0u, data, 0u, 64u);
    Expect(st == GT32_ERR_LEN, "WriteSecondary zero len");

    AssertNoChipEraseOpcodes();
}

/**
@brief 用例：WriteSecondary 跨页拆分为多次 PageProgram
*/
static void Test_WriteSecondaryMultiPage(void)
{
    GT32_Status_t st;
    uint8_t data[356];
    size_t pp_n;

    BoardGt32Mock_Reset();
    (void)memset(data, 0xAB, sizeof(data));
    /* 从页内偏移 200 起写 356 字节 → 分 3 段：56 + 256 + 44 */
    st = GT32_WriteSecondary(200u, data, sizeof(data), 4096u);
    Expect(st == GT32_OK, "WriteSecondary multi-page OK");
    pp_n = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_page_program);
    Expect(pp_n == 3u, "multi-page write splits into 3 PagePrograms");
    Expect(BoardGt32Mock_PpAddrCount() == 3u, "three PP addresses logged");
    Expect(BoardGt32Mock_PpAddrAt(0) == (ota_gt32_secondary_off + 200u),
           "first chunk at offset 200");
    Expect(BoardGt32Mock_PpAddrAt(1) == (ota_gt32_secondary_off + 256u),
           "second chunk at page boundary 256");
    Expect(BoardGt32Mock_PpAddrAt(2) == (ota_gt32_secondary_off + 512u),
           "third chunk at 512");
    AssertNoChipEraseOpcodes();
}

/**
@brief 用例：WriteSecondary 不发出 CE
*/
static void Test_WriteSecondaryNoCe(void)
{
    GT32_Status_t st;
    uint8_t data[8];

    BoardGt32Mock_Reset();
    (void)memset(data, 0xCD, sizeof(data));
    st = GT32_WriteSecondary(0u, data, sizeof(data), 256u);
    Expect(st == GT32_OK, "WriteSecondary for CE check OK");
    Expect(BoardGt32Mock_HasOpcode(0x60u) == 0, "WriteSecondary no CE 60h");
    Expect(BoardGt32Mock_HasOpcode(0xC7u) == 0, "WriteSecondary no CE C7h");
    AssertNoChipEraseOpcodes();
}

int main(void)
{
    g_failures = 0;

    Test_CommandWhitelist();
    Test_ProbeOk();
    Test_ProbeFail();
    Test_ReadAndRdsr();
    Test_PageProgramOpcodes();
    Test_EraseOpcodes();
    Test_RejectAlignAndCrossPage();
    Test_RejectRange();
    Test_WipTimeout();
    Test_NullArgs();

    /* Slice 3 */
    Test_EraseSecondarySlot();
    Test_WriteSecondaryOk();
    Test_WriteSecondaryReject();
    Test_WriteSecondaryMultiPage();
    Test_WriteSecondaryNoCe();

    if (g_failures != 0) {
        (void)printf("\n%d failure(s)\n", g_failures);
        return 1;
    }
    (void)printf("\nAll gt32_raw tests passed\n");
    return 0;
}
