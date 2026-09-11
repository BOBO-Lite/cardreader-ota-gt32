/**
@file test_boot_select.c
@brief Slice 7 主机 E2：上电路由表 + BKP1 消费 + BKP2 门限 + LOAD_A 清单（非 E3）
@note 实机未验证；v1 无 LastGood 自动恢复。
*/

#include <stdio.h>
#include <string.h>

#include "board_gt32.h"
#include "boot.h"
#include "boot_bkp.h"
#include "fmc.h"
#include "gt32_raw.h"
#include "ota_image.h"
#include "ota_layout.h"
#include "ota_meta.h"
#include "ota_meta_codec.h"

static int g_failures;

static void Expect(int cond, const char *msg)
{
    if (!cond) {
        (void)printf("FAIL: %s\n", msg);
        g_failures++;
    } else {
        (void)printf("PASS: %s\n", msg);
    }
}

static void PutLe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void ResetAll(void)
{
    BoardGt32Mock_Reset();
    FmcMock_Reset();
    GT32Mock_ResetLastGoodStats();
    BootBkpMock_Reset();
    BootMock_ClearJumpToAppFlag();
    BootMock_ClearResetRequestFlag();
    BootMock_ClearInstallFailCount();
    BootMock_ClearHandoffChecklist();
    Boot_OtaClearCommitImageReadyOkFlag();
    Boot_OtaClearCommitFailedStayOtaFlag();
    (void)BootBkp_Set(0u);
    Expect(GT32_Init() == GT32_OK, "ResetAll: GT32_Init");
}

/**
@brief 写三旗标到 Meta 页（不经 Commit；仅选路测试）
*/
static void SeedFlags(uint32_t ready, uint32_t valid, uint32_t fail)
{
    uint8_t words[12];
    PutLe32(words + 0, ready);
    PutLe32(words + 4, valid);
    PutLe32(words + 8, fail);
    FmcMock_LoadBytes(ota_meta_off_image_ready, words, sizeof(words));
}

/**
@brief 提交完整 READY+VALID（经正常安装路径的最小种子）
*/
static void SeedAppValidViaInstall(void)
{
    uint8_t img[256];
    uint32_t crc;
    const char *name = "cardreader-v1.2.3.bin";

    (void)memset(img, 0xA5, sizeof(img));
    PutLe32(img, 0x20005000u);
    PutLe32(img + 4u, (ota_flash_app_base + 0x40u) | 1u);
    crc = OtaImage_Crc32Finalize(
        OtaImage_Crc32Update(OTA_IMAGE_CRC32_INITIAL, img, sizeof(img)));
    BoardGt32Mock_LoadFlash(ota_gt32_secondary_off, img, sizeof(img));
    /* 向量 NOT OK on App → skip LastGood */
    Expect(OtaMeta_CommitImageReady(name, (uint8_t)strlen(name),
                                    (uint32_t)sizeof(img), crc) == OTA_META_OK,
           "seed: CommitImageReady");
    Expect(Boot_InstallOnce() == 1, "seed: InstallOnce → VALID");
    Expect(OtaMeta_IsAppValid() != 0, "seed: APP_VALID");
    BootMock_ClearJumpToAppFlag();
    BootMock_ClearHandoffChecklist();
    (void)BootBkp_Set2(0u);
}

/* ---- 路由矩阵 ---- */

static void Test_ReadyUnset_Ota(void)
{
    ResetAll();
    SeedFlags(ota_meta_flag_erased, ota_meta_flag_erased, ota_meta_flag_erased);
    Expect(Boot_SelectPath() == BOOT_PATH_OTA, "READY unset → OTA");
    Expect(Boot_MainOnce() == (int)BOOT_PATH_OTA, "MainOnce: READY unset → OTA");
}

static void Test_InstallPending(void)
{
    ResetAll();
    SeedFlags(ota_meta_flag_set, ota_meta_flag_erased, ota_meta_flag_erased);
    Expect(OtaMeta_IsInstallPending() != 0, "pending helper");
    Expect(Boot_SelectPath() == BOOT_PATH_INSTALL,
           "READY set, VALID unset, FAIL unset → INSTALL");
}

static void Test_ReadyFail_Ota(void)
{
    ResetAll();
    SeedFlags(ota_meta_flag_set, ota_meta_flag_erased, ota_meta_flag_set);
    Expect(Boot_SelectPath() == BOOT_PATH_OTA, "READY+FAIL → OTA");
}

static void Test_ValidAndFail_Ota(void)
{
    ResetAll();
    SeedFlags(ota_meta_flag_set, ota_meta_flag_set, ota_meta_flag_set);
    Expect(OtaMeta_IsAppValidFlagSet() != 0, "VALID flag set");
    Expect(OtaMeta_IsOtaFailSet() != 0, "FAIL set");
    Expect(Boot_SelectPath() == BOOT_PATH_OTA, "VALID&FAIL → OTA");
    Expect(Boot_MainOnce() == (int)BOOT_PATH_OTA, "MainOnce: VALID&FAIL → OTA");
}

static void Test_InstallPriorityOverBkp1(void)
{
    ResetAll();
    SeedFlags(ota_meta_flag_set, ota_meta_flag_erased, ota_meta_flag_erased);
    Expect(BootBkp_Set1(BOOT_BKP1_OTA_REQUEST) == 1, "set BKP1 request");
    Expect(Boot_SelectPath() == BOOT_PATH_INSTALL,
           "install-pending priority over BKP1");
    Expect(BootBkp_Get1() == BOOT_BKP1_OTA_REQUEST, "BKP1 not consumed on install path");
}

static void Test_Bkp1Consume_Ota(void)
{
    ResetAll();
    SeedAppValidViaInstall();
    Expect(BootBkp_Set1(BOOT_BKP1_OTA_REQUEST) == 1, "BKP1=0x3344");
    Expect(BootBkp_Set2(0u) == 1, "BKP2=0");
    Expect(Boot_SelectPath() == BOOT_PATH_OTA, "BKP1 request → OTA");
    Expect(BootBkp_Get1() == BOOT_BKP1_CONSUMED, "BKP1 consumed → 0xAABB");
}

static void Test_Bkp2Ge5_Ota_NoLastGood(void)
{
    size_t lg_erase_before;
    size_t lg_bytes_before;

    ResetAll();
    SeedAppValidViaInstall();
    Expect(BootBkp_Set2(5u) == 1, "BKP2=5");
    lg_erase_before = GT32Mock_LastGoodEraseCount();
    lg_bytes_before = GT32Mock_LastGoodWriteBytes();
    Expect(Boot_SelectPath() == BOOT_PATH_OTA, "BKP2>=5 → OTA");
    Expect(GT32Mock_LastGoodEraseCount() == lg_erase_before,
           "no LastGood auto-restore erase");
    Expect(GT32Mock_LastGoodWriteBytes() == lg_bytes_before,
           "no LastGood auto-restore write");
    Expect(BootBkp_Get2() == 5u, "BKP2 unchanged at threshold");

    Expect(BootBkp_Set2(0xFFFFu) == 1, "BKP2=0xFFFF");
    Expect(Boot_SelectPath() == BOOT_PATH_OTA, "BKP2=0xFFFF → OTA (no wrap)");
}

static void Test_Bkp2Inc_JumpChecklist(void)
{
    ResetAll();
    SeedAppValidViaInstall();
    Expect(BootBkp_Set1(BOOT_BKP1_CONSUMED) == 1, "BKP1 not request");
    Expect(BootBkp_Set2(2u) == 1, "BKP2=2");
    Expect(Boot_SelectPath() == BOOT_PATH_JUMP, "BKP2<5 → JUMP");
    Expect(BootBkp_Get2() == 3u, "BKP2++ → 3");

    BootMock_ClearJumpToAppFlag();
    BootMock_ClearHandoffChecklist();
    /* 重新：MainOnce 全路径 */
    Expect(BootBkp_Set2(1u) == 1, "BKP2=1 for MainOnce");
    Expect(Boot_MainOnce() == (int)BOOT_PATH_JUMP, "MainOnce → JUMP");
    Expect(BootBkp_Get2() == 2u, "MainOnce BKP2++");
    Expect(BootMock_JumpToAppFlag() != 0, "jump flag set");
    Expect((BootMock_HandoffChecklist() & BOOT_HANDOFF_ALL) == BOOT_HANDOFF_ALL,
           "handoff checklist all bits");
    Expect(BootMock_HandoffVtor() == ota_flash_app_base, "VTOR=app_base recorded");
    Expect((BootMock_HandoffChecklist() & BOOT_HANDOFF_RS485_RELEASED) != 0u,
           "checklist: RS485 released");
    Expect((BootMock_HandoffChecklist() & BOOT_HANDOFF_DMA_USART_OFF) != 0u,
           "checklist: DMA/USART off");
    Expect((BootMock_HandoffChecklist() & BOOT_HANDOFF_SYSTICK_OFF) != 0u,
           "checklist: SysTick off");
    Expect((BootMock_HandoffChecklist() & BOOT_HANDOFF_NVIC_CLEARED) != 0u,
           "checklist: NVIC pending cleared");
    Expect((BootMock_HandoffChecklist() & BOOT_HANDOFF_MSP_SWITCHED) != 0u,
           "checklist: MSP switched");
    Expect((BootMock_HandoffChecklist() & BOOT_HANDOFF_WDT_FED) != 0u,
           "checklist: WDT fed/kept");
}

static void Test_ColdBootDecideAlias(void)
{
    ResetAll();
    SeedFlags(ota_meta_flag_erased, ota_meta_flag_erased, ota_meta_flag_erased);
    Expect(Boot_ColdBootDecide() == BOOT_PATH_OTA, "ColdBootDecide alias → OTA");
}

static void Test_FailHalfWrite_Ota(void)
{
    ResetAll();
    SeedFlags(ota_meta_flag_set, ota_meta_flag_erased, 0xFFFF0000u);
    Expect(OtaMeta_IsOtaFailSet() != 0, "half-write FAIL is set");
    Expect(Boot_SelectPath() == BOOT_PATH_OTA, "half-write FAIL → OTA");
}

int main(void)
{
    g_failures = 0;
    (void)printf("=== Slice 7 test_boot_select (host E2) ===\n");

    Test_ReadyUnset_Ota();
    Test_InstallPending();
    Test_ReadyFail_Ota();
    Test_ValidAndFail_Ota();
    Test_InstallPriorityOverBkp1();
    Test_Bkp1Consume_Ota();
    Test_Bkp2Ge5_Ota_NoLastGood();
    Test_Bkp2Inc_JumpChecklist();
    Test_ColdBootDecideAlias();
    Test_FailHalfWrite_Ota();

    if (g_failures != 0) {
        (void)printf("RESULT: %d failure(s)\n", g_failures);
        return 1;
    }
    (void)printf("RESULT: ALL PASS\n");
    return 0;
}
