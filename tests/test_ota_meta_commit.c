/**
@file test_ota_meta_commit.c
@brief Slice 5 主机 E2：CommitImageReady 整页事务（非 E3；实机未验证）
*/

#include <stdio.h>
#include <string.h>

#include "board_gt32.h"
#include "boot.h"
#include "drv_usart.h"
#include "drv_ymodem.h"
#include "fmc.h"
#include "gt32_raw.h"
#include "ota_layout.h"
#include "ota_image.h"
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

static uint32_t ReadLe32(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static size_t BuildDataFrame(uint8_t *out, size_t cap, uint8_t kind,
                             uint8_t seq, const uint8_t *payload, size_t plen)
{
    size_t frame_len;
    size_t data_len;
    uint16_t crc;

    if (kind == DRV_YM_SOH) {
        data_len = DRV_YM_SOH_PAYLOAD_LEN;
        frame_len = DRV_YM_SOH_FRAME_LEN;
    } else {
        return 0u;
    }
    if (cap < frame_len || plen > data_len) {
        return 0u;
    }
    (void)memset(out, 0x1A, frame_len);
    out[0] = kind;
    out[1] = seq;
    out[2] = (uint8_t)(0xFFu - seq);
    if (plen > 0u && payload != NULL) {
        (void)memcpy(&out[3], payload, plen);
    }
    crc = DrvYmodem_Crc16Ccitt(&out[3], data_len);
    out[frame_len - 2u] = (uint8_t)((crc >> 8) & 0xFFu);
    out[frame_len - 1u] = (uint8_t)(crc & 0xFFu);
    return frame_len;
}

static size_t BuildHeaderFrame(uint8_t *out, size_t cap, const char *name,
                               uint32_t size)
{
    uint8_t payload[128];
    size_t nlen;
    char size_ascii[16];
    int n;
    size_t o;

    (void)memset(payload, 0, sizeof(payload));
    nlen = strlen(name);
    (void)memcpy(payload, name, nlen);
    payload[nlen] = 0u;
    n = snprintf(size_ascii, sizeof(size_ascii), "%lu", (unsigned long)size);
    o = nlen + 1u;
    (void)memcpy(payload + o, size_ascii, (size_t)n);
    (void)n;
    return BuildDataFrame(out, cap, DRV_YM_SOH, 0u, payload, sizeof(payload));
}

/** E1 Happy commit */

/** R02 §3.4.2：cardreader-v0.3.1.bin → Meta 版本字段 v0.3.1 */
static void Test_VersionField_v031(void)
{
    const char *name = "cardreader-v0.3.1.bin";
    OtaMetaStatus_t st;
    const uint8_t *page;

    FmcMock_Reset();
    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), 1024u, 0xA1B2C3D4u);
    Expect(st == OTA_META_OK, "v031: Commit OK");
    page = FmcMock_PagePtr();
    Expect(memcmp(page + ota_meta_off_filename, "cardreader-v0.3.1.bin", 22) == 0,
           "v031: filename stored");
    Expect(memcmp(page + ota_meta_off_version, "v0.3.1", 7) == 0,
           "v031: version field is v0.3.1 (leading v retained)");
    Expect(page[ota_meta_off_version + 6] == 0u, "v031: version NUL padded");
}

static void Test_E1_HappyCommit(void)
{
    const char *name = "cardreader-v1.0.0.bin";
    uint32_t size = 64u;
    uint32_t fw_crc = 0xA5A5A5A5u;
    OtaMetaStatus_t st;
    const uint8_t *page;
    uint32_t ready;
    uint32_t board;
    uint32_t fail;
    uint32_t calc;
    size_t erase_n;
    uint8_t expect[ota_meta_record_size];
    OtaMetaRecord_t rec;

    FmcMock_Reset();
    erase_n = FmcMock_EraseCount();
    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), size, fw_crc);
    Expect(st == OTA_META_OK, "E1: CommitImageReady OK");
    Expect(FmcMock_EraseCount() == erase_n + 1u, "E1: erase once");
    Expect(OtaMeta_IsInstallPending() != 0, "E1: install pending");

    page = FmcMock_PagePtr();
    ready = ReadLe32(page + ota_meta_off_image_ready);
    board = ReadLe32(page + ota_meta_off_board_id);
    fail = ReadLe32(page + ota_meta_off_mate_ota_fail);
    Expect(ready == ota_meta_flag_set, "E1: READY=0 readback");
    Expect(board == ota_board_id_gt32, "E1: board_id LE 0x0001");
    Expect(memcmp(page + ota_meta_off_version, "v1.0.0", 7) == 0,
           "E1: Meta version field is v1.0.0 including leading v");
    Expect(fail == ota_meta_flag_erased, "E1: FAIL erased");

    (void)memset(&rec, 0, sizeof(rec));
    (void)memcpy(rec.filename, name, strlen(name));
    (void)memcpy(rec.version, "v1.0.0", 6);
    rec.image_size = size;
    rec.fw_crc32 = fw_crc;
    rec.image_ready = ota_meta_flag_erased;
    rec.app_valid = ota_meta_flag_erased;
    rec.mate_ota_fail = ota_meta_flag_erased;
    Expect(OtaMeta_EncodeRecord(&rec, expect) == OTA_META_OK, "E1: encode expect");
    calc = OtaMeta_ComputeCrc(page);
    Expect(calc == ReadLe32(page + ota_meta_off_meta_crc32),
           "E1: Meta CRC matches ComputeCrc");
    Expect(memcmp(page, expect, OTA_META_INFO_SIZE) == 0,
           "E1: info 72B matches encoded");
}

/** E2 Dirty page: mid-program fail → next attempt erases again */
static void Test_E2_DirtyPageReErase(void)
{
    const char *name = "cardreader-v2.0.0.bin";
    size_t erase_before;
    OtaMetaStatus_t st;

    FmcMock_Reset();
    erase_before = FmcMock_EraseCount();
    FmcMock_InjectFailProgramAt(0x20u);
    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), 128u, 0x11111111u);
    Expect(st == OTA_META_OK, "E2: eventually OK after retry");
    Expect(FmcMock_EraseCount() >= erase_before + 2u,
           "E2: erases>=2 after dirty mid-program");
    Expect(OtaMeta_IsInstallPending() != 0, "E2: pending after rebuild");
}

/** E3 Three failures → error */
static void Test_E3_ThreeFailures(void)
{
    const char *name = "cardreader-v3.0.0.bin";
    OtaMetaStatus_t st;
    size_t erase_n;

    FmcMock_Reset();
    FmcMock_InjectFailEraseAlways();
    erase_n = FmcMock_EraseCount();
    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), 64u, 0x22222222u);
    Expect(st == OTA_META_ERR_FLASH, "E3: Commit returns FLASH error");
    Expect(FmcMock_EraseCount() == erase_n + OTA_META_FLASH_ATTEMPT_MAX,
           "E3: exactly 3 erase attempts");
    Expect(OtaMeta_IsInstallPending() == 0, "E3: not pending");
}

/** E4 Idempotent already-committed → no re-erase */
static void Test_E4_IdempotentNoReErase(void)
{
    const char *name = "cardreader-v1.2.3.bin";
    uint32_t size = 32u;
    uint32_t crc = 0x33333333u;
    size_t erase_after_first;
    size_t prog_after_first;
    OtaMetaStatus_t st;

    FmcMock_Reset();
    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), size, crc);
    Expect(st == OTA_META_OK, "E4: first commit OK");
    erase_after_first = FmcMock_EraseCount();
    prog_after_first = FmcMock_ProgramCount();

    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), size, crc);
    Expect(st == OTA_META_OK, "E4: idempotent OK");
    Expect(FmcMock_EraseCount() == erase_after_first, "E4: no re-erase");
    Expect(FmcMock_ProgramCount() == prog_after_first, "E4: no re-program");

    FmcMock_InjectFailFlag(0x0000FFFFu);
    erase_after_first = FmcMock_EraseCount();
    st = OtaMeta_CommitImageReady(name, (uint8_t)strlen(name), size, crc);
    Expect(st == OTA_META_OK, "E4b: rebuild after FAIL half-write");
    Expect(FmcMock_EraseCount() > erase_after_first, "E4b: did erase to clear FAIL");
    Expect(ReadLe32(FmcMock_PagePtr() + ota_meta_off_mate_ota_fail)
               == ota_meta_flag_erased,
           "E4b: FAIL cleared to erased");
}

/**
@brief E5：RECEIVE_DATA 传输路径 Meta FMC program 计数保持 0
*/
static void Test_E5_TransferNeverProgramsMeta(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    uint8_t payload[64];
    size_t flen;
    size_t meta_prog_before;
    size_t meta_prog_after;

    BoardGt32Mock_Reset();
    FmcMock_Reset();
    Boot_TestSetNowMs(0u);
    BootMock_TxLogClear();
    Boot_OtaClearCommitImageReadyOkFlag();
    Boot_OtaClearCommitFailedStayOtaFlag();
    DrvUsart_Init();
    Expect(GT32_Init() == GT32_OK, "E5: GT32_Init");

    meta_prog_before = FmcMock_ProgramCount();
    Boot_OtaStartAttempt(&sess);

    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v1.0.0.bin", 64u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "E5: freeze header");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "E5: poll header+erase");
    Expect(sess.phase == BOOT_OTA_RECEIVE_DATA, "E5: RECEIVE_DATA");

    (void)memset(payload, 0x5A, sizeof(payload));
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, payload, 64u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "E5: freeze data");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "E5: poll data");
    Expect(sess.phase == BOOT_OTA_WAIT_EOT1, "E5: WAIT_EOT1");

    meta_prog_after = FmcMock_ProgramCount();
    Expect(meta_prog_after == meta_prog_before,
           "E5: Meta FMC program count remains 0 during transfer");
    Expect(FmcMock_EraseCount() == 0u, "E5: Meta erase count 0 during transfer");
}

/**
@brief Boot：SESSION_DONE → commit 成功
*/
static void Test_Boot_SessionDoneCommitOk(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    uint8_t payload[64];
    size_t flen;
    uint8_t eot = DRV_YM_EOT;

    BoardGt32Mock_Reset();
    FmcMock_Reset();
    Boot_TestSetNowMs(0u);
    BootMock_TxLogClear();
    Boot_OtaClearCommitImageReadyOkFlag();
    Boot_OtaClearCommitFailedStayOtaFlag();
    BootMock_ClearJumpToAppFlag();
    BootMock_ClearResetRequestFlag();
    BootMock_ClearInstallFailCount();
    DrvUsart_Init();
    Expect(GT32_Init() == GT32_OK, "boot-ok: GT32_Init");

    Boot_OtaStartAttempt(&sess);
    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v1.0.0.bin", 64u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "boot-ok: header");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-ok: poll header");

    (void)memset(payload, 0xA5, sizeof(payload));
    payload[0] = 0x00; payload[1] = 0x50; payload[2] = 0x00; payload[3] = 0x20;
    payload[4] = 0x21; payload[5] = 0x40; payload[6] = 0x00; payload[7] = 0x08;
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, payload, 64u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "boot-ok: data");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-ok: poll data");

    Expect(DrvUsartMock_FreezeRound(&eot, 1u) == 0, "boot-ok: EOT1");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-ok: poll EOT1");
    Expect(DrvUsartMock_FreezeRound(&eot, 1u) == 0, "boot-ok: EOT2");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-ok: poll EOT2");

    (void)memset(frame, 0, sizeof(frame));
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 0u, frame, 0u);
    /* empty block0: all-zero payload */
    {
        uint8_t empty_payload[128];
        (void)memset(empty_payload, 0, sizeof(empty_payload));
        flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 0u, empty_payload,
                              sizeof(empty_payload));
    }
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "boot-ok: empty Block0");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-ok: poll empty");
    Expect(sess.phase == BOOT_OTA_DONE_GRACE, "boot-ok: DONE_GRACE");

    Boot_TestSetNowMs(BOOT_OTA_PROGRESS_MS);
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-ok: grace expire");
    Expect(sess.phase == BOOT_OTA_SESSION_DONE, "boot-ok: SESSION_DONE");
    Expect(Boot_OtaCommitImageReadyOkFlag() != 0, "boot-ok: commit OK flag");
    Expect(Boot_OtaCommitFailedStayOtaFlag() == 0, "boot-ok: not stay_ota");
    Expect(OtaMeta_IsAppValid() != 0, "boot-ok: Meta APP_VALID after install");
    Expect(BootMock_JumpToAppFlag() != 0, "boot-ok: jump stub");
    Expect(ReadLe32(FmcMock_PagePtr() + ota_meta_off_image_ready) == 0u,
           "boot-ok: READY=0");
}

/**
@brief Boot：Commit 三次失败 → StartAttempt / stay_ota
*/
static void Test_Boot_CommitThreeFailStayOta(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    uint8_t payload[64];
    size_t flen;
    uint8_t eot = DRV_YM_EOT;
    size_t tx_before;

    BoardGt32Mock_Reset();
    FmcMock_Reset();
    FmcMock_InjectFailEraseAlways();
    Boot_TestSetNowMs(0u);
    BootMock_TxLogClear();
    Boot_OtaClearCommitImageReadyOkFlag();
    Boot_OtaClearCommitFailedStayOtaFlag();
    DrvUsart_Init();
    Expect(GT32_Init() == GT32_OK, "boot-fail: GT32_Init");

    Boot_OtaStartAttempt(&sess);
    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v9.9.9.bin", 64u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "boot-fail: header");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-fail: poll header");

    (void)memset(payload, 0xB0, sizeof(payload));
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, payload, 64u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "boot-fail: data");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-fail: poll data");

    Expect(DrvUsartMock_FreezeRound(&eot, 1u) == 0, "boot-fail: EOT1");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-fail: poll EOT1");
    Expect(DrvUsartMock_FreezeRound(&eot, 1u) == 0, "boot-fail: EOT2");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-fail: poll EOT2");

    {
        uint8_t empty_payload[128];
        (void)memset(empty_payload, 0, sizeof(empty_payload));
        flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 0u, empty_payload,
                              sizeof(empty_payload));
    }
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "boot-fail: empty");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-fail: poll empty");

    tx_before = BootMock_TxLogLen();
    Boot_TestSetNowMs(BOOT_OTA_PROGRESS_MS);
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "boot-fail: grace→commit fail");
    Expect(Boot_OtaCommitImageReadyOkFlag() == 0, "boot-fail: commit not OK");
    Expect(Boot_OtaCommitFailedStayOtaFlag() != 0, "boot-fail: stay_ota flag");
    Expect(sess.phase == BOOT_OTA_WAIT_HEADER, "boot-fail: back WAIT_HEADER");
    Expect(BootMock_TxLogLen() > tx_before, "boot-fail: SendPair C issued");
    Expect(BootMock_TxLogAt(BootMock_TxLogLen() - 1u) == DRV_YM_C,
           "boot-fail: last TX is C");
}

int main(void)
{
    g_failures = 0;

    Test_VersionField_v031();
    Test_E1_HappyCommit();
    Test_E2_DirtyPageReErase();
    Test_E3_ThreeFailures();
    Test_E4_IdempotentNoReErase();
    Test_E5_TransferNeverProgramsMeta();
    Test_Boot_SessionDoneCommitOk();
    Test_Boot_CommitThreeFailStayOta();

    if (g_failures != 0) {
        (void)printf("\n%d failure(s) in Slice5 Meta commit host E2\n", g_failures);
        return 1;
    }
    (void)printf("\nAll Slice5 Meta commit host E2 tests passed "
                 "(not E3; 实机尚未验证)\n");
    return 0;
}
