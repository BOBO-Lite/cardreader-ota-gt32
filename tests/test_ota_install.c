/**
@file test_ota_install.c
@brief Slice 6 主机 E2：InstallOnce / LastGood / ×3 FAIL（非 E3；实机/掉电未验证）
*/

#include <stdio.h>
#include <string.h>

#include "board_gt32.h"
#include "boot.h"
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

static uint32_t ReadLe32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

/**
@brief 构造合法镜像：向量 + 填充；返回 CRC32
*/
static uint32_t BuildValidImage(uint8_t *img, uint32_t size, uint32_t reset_off)
{

    (void)memset(img, 0x5A, (size_t)size);
    PutLe32(img, 0x20005000u); /* MSP */
    PutLe32(img + 4u, (ota_flash_app_base + reset_off) | 1u);
    return OtaImage_Crc32Finalize(
        OtaImage_Crc32Update(OTA_IMAGE_CRC32_INITIAL, img, size));
}

/**
@brief 将 Secondary 预置为镜像并提交 IMAGE_READY（待安装）
*/
static void SeedPendingInstall(const uint8_t *img, uint32_t size, uint32_t crc)
{
    const char *name = "cardreader-v1.2.3.bin";
    OtaMetaStatus_t st;

    BoardGt32Mock_LoadFlash(ota_gt32_secondary_off, img, (size_t)size);
    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), size, crc);
    Expect(st == OTA_META_OK, "seed: CommitImageReady");
    Expect(OtaMeta_IsInstallPending() != 0, "seed: install pending");
}

static void ResetAll(void)
{
    BoardGt32Mock_Reset();
    FmcMock_Reset();
    GT32Mock_ResetLastGoodStats();
    BootMock_ClearJumpToAppFlag();
    BootMock_ClearResetRequestFlag();
    BootMock_ClearInstallFailCount();
    Boot_OtaClearCommitImageReadyOkFlag();
    Boot_OtaClearCommitFailedStayOtaFlag();
    (void)BootBkp_Set(0xABCDu);
    Expect(GT32_Init() == GT32_OK, "ResetAll: GT32_Init");
}

/** E2-1 Happy：向量 OK → LastGood 先于 App erase → VALID */
static void Test_E2_HappyLastGoodBeforeErase(void)
{
    uint8_t img[256];
    uint8_t old_app[256];
    uint32_t crc;
    uint32_t size = 256u;
    uint32_t lg_seq;
    uint32_t app_seq;
    size_t app_erase_before;
    size_t lg_erase_before;

    ResetAll();
    /* 当前 App 向量 OK（异于 Secondary），证明会走 LastGood */
    (void)BuildValidImage(old_app, size, 0x80u);
    FmcMock_LoadAppBytes(0u, old_app, (size_t)size);
    crc = BuildValidImage(img, size, 0x40u);
    SeedPendingInstall(img, size, crc);

    Expect(OtaImage_CheckAppVectors() != 0u, "E2-1: App vectors OK");
    app_erase_before = FmcMock_AppEraseCount();
    lg_erase_before = GT32Mock_LastGoodEraseCount();

    Expect(Boot_InstallOnce() == 1, "E2-1: InstallOnce OK");
    Expect(OtaMeta_IsAppValid() != 0, "E2-1: APP_VALID");
    Expect(BootBkp_Get() == 0u, "E2-1: BKP2 cleared");
    Expect(GT32Mock_LastGoodEraseCount() > lg_erase_before, "E2-1: LastGood erased");
    Expect(GT32Mock_LastGoodWriteBytes() >= ota_app_image_max,
           "E2-1: LastGood full 112640 written");
    Expect(FmcMock_AppEraseCount() == app_erase_before + 1u, "E2-1: App erased once");

    lg_seq = GT32Mock_LastGoodWriteSeq();
    app_seq = FmcMock_AppEraseSeq();
    Expect(lg_seq != 0u && app_seq != 0u, "E2-1: seq stamps set");
    Expect(lg_seq < app_seq, "E2-1: LastGood write seq before App erase seq");

    /* LastGood 前 8 字节应等于旧 App 向量 */
    Expect(BoardGt32Mock_FlashByte(ota_gt32_lastgood_off + 0u) == old_app[0],
           "E2-1: LastGood[0] matches old App");
    Expect(BoardGt32Mock_FlashByte(ota_gt32_lastgood_off + 4u) == old_app[4],
           "E2-1: LastGood[4] matches old App");
}

/** E2-2 Backup fail：GT32 LastGood 写失败 → 不擦 App */
static void Test_E2_BackupFailNoAppErase(void)
{
    uint8_t img[128];
    uint8_t old_app[128];
    uint32_t crc;
    uint32_t size = 128u;
    size_t app_erase_before;

    ResetAll();
    (void)BuildValidImage(old_app, size, 0x20u);
    FmcMock_LoadAppBytes(0u, old_app, (size_t)size);
    crc = BuildValidImage(img, size, 0x30u);
    SeedPendingInstall(img, size, crc);

    Expect(OtaImage_CheckAppVectors() != 0u, "E2-2: vectors OK (will backup)");
    app_erase_before = FmcMock_AppEraseCount();

    /* 注入：LastGood 区页编程失败一次即可让备份失败 */
    BoardGt32Mock_InjectFailPpInRangeOnce(ota_gt32_lastgood_off,
                                          ota_gt32_lastgood_off + ota_gt32_lastgood_size);

    Expect(Boot_InstallOnce() == 0, "E2-2: InstallOnce fails on LastGood");
    Expect(FmcMock_AppEraseCount() == app_erase_before,
           "E2-2: App erase count unchanged");
    Expect(OtaMeta_IsInstallPending() != 0, "E2-2: still pending");
    Expect(OtaMeta_IsAppValid() == 0, "E2-2: not VALID");
}

/** E2-3 向量 NOT OK：跳过 LastGood，仍可擦 App 安装 */
static void Test_E2_VectorNotOkSkipLastGood(void)
{
    uint8_t img[128];
    uint32_t crc;
    uint32_t size = 128u;
    size_t lg_erase_before;
    size_t lg_bytes_before;

    ResetAll();
    /* App 保持 0xFF → 向量 NOT OK */
    crc = BuildValidImage(img, size, 0x28u);
    SeedPendingInstall(img, size, crc);

    Expect(OtaImage_CheckAppVectors() == 0u, "E2-3: App vectors NOT OK");
    lg_erase_before = GT32Mock_LastGoodEraseCount();
    lg_bytes_before = GT32Mock_LastGoodWriteBytes();

    Expect(Boot_InstallOnce() == 1, "E2-3: InstallOnce OK without LastGood");
    Expect(GT32Mock_LastGoodEraseCount() == lg_erase_before,
           "E2-3: no LastGood erase");
    Expect(GT32Mock_LastGoodWriteBytes() == lg_bytes_before,
           "E2-3: no LastGood write");
    Expect(FmcMock_AppEraseCount() >= 1u, "E2-3: App erased");
    Expect(OtaMeta_IsAppValid() != 0, "E2-3: APP_VALID");
}

/** E2-4 安装 ×3 → CommitOtaFail */
static void Test_E2_InstallTimes3Fail(void)
{
    uint8_t junk[64];
    const char *name = "cardreader-v0.1.0.bin";
    uint32_t bogus_crc = 0xDEADBEEFu;

    ResetAll();
    (void)memset(junk, 0x00, sizeof(junk)); /* 坏向量 */
    BoardGt32Mock_LoadFlash(ota_gt32_secondary_off, junk, sizeof(junk));
    Expect(OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), 64u, bogus_crc)
               == OTA_META_OK,
           "E2-4: READY");
    Expect(OtaMeta_IsInstallPending() != 0, "E2-4: pending");

    Boot_RunInstallStage();

    Expect(BootMock_InstallFailCount() == BOOT_INSTALL_ATTEMPT_MAX,
           "E2-4: fail count == 3");
    Expect(OtaMeta_IsInstallPending() == 0, "E2-4: no longer pending");
    Expect(ReadLe32(FmcMock_PagePtr() + ota_meta_off_mate_ota_fail) == 0u,
           "E2-4: FAIL=0");
    Expect(BootMock_ResetRequestFlag() != 0, "E2-4: reset stub after FAIL");
    Expect(FmcMock_AppEraseCount() == 0u, "E2-4: never erased App (Secondary bad)");
}

/** 已 VALID：InstallOnce 成功且不擦 App */
static void Test_AlreadyValidSkipsErase(void)
{
    uint8_t img[64];
    uint32_t crc;
    size_t app_erase_before;

    ResetAll();
    crc = BuildValidImage(img, 64u, 0x10u);
    SeedPendingInstall(img, 64u, crc);
    Expect(Boot_InstallOnce() == 1, "valid: first install");
    app_erase_before = FmcMock_AppEraseCount();
    Expect(Boot_InstallOnce() == 1, "valid: second call OK");
    Expect(FmcMock_AppEraseCount() == app_erase_before,
           "valid: no extra App erase");
}

/** CommitOtaFail 半写 FAIL 视为成功 */
static void Test_CommitOtaFailHalfWrite(void)
{
    const char *name = "cardreader-v0.2.0.bin";

    ResetAll();
    Expect(OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), 32u, 0x1u)
               == OTA_META_OK,
           "half: READY");
    FmcMock_InjectFailFlag(0xFFFF0000u); /* 半写 */
    Expect(OtaMeta_CommitOtaFail() == OTA_META_OK,
           "half: CommitOtaFail treats half-write as set");
}

/** 无 CE opcodes */
static void Test_NoChipErase(void)
{
    size_t i;
    uint8_t op;
    int clean = 1;

    ResetAll();
    (void)GT32_EraseLastGoodSlot();
    for (i = 0u; i < BoardGt32Mock_OpcodeCount(); i++) {
        op = BoardGt32Mock_OpcodeAt(i);
        if (op == 0x60u || op == 0xC7u) {
            clean = 0;
            break;
        }
    }
    Expect(clean != 0, "E2-5: no CE opcodes in LastGood erase path");
    Expect(BoardGt32Mock_EraseAddrCount() >= 2u, "LastGood 2x D8h");
    Expect(BoardGt32Mock_EraseAddrAt(0) == ota_gt32_lastgood_off, "D8h @ 0x20000");
    Expect(BoardGt32Mock_EraseAddrAt(1) == ota_gt32_lastgood_off + 0x10000u,
           "D8h @ 0x30000");
}

int main(void)
{
    g_failures = 0;
    (void)printf("=== test_ota_install (Slice 6 host E2; not E3) ===\n");
    Test_E2_HappyLastGoodBeforeErase();
    Test_E2_BackupFailNoAppErase();
    Test_E2_VectorNotOkSkipLastGood();
    Test_E2_InstallTimes3Fail();
    Test_AlreadyValidSkipsErase();
    Test_CommitOtaFailHalfWrite();
    Test_NoChipErase();
    if (g_failures != 0) {
        (void)printf("RESULT: %d failure(s)\n", g_failures);
        return 1;
    }
    (void)printf("RESULT: ALL PASS\n");
    return 0;
}
