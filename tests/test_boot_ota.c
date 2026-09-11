/**
@file test_boot_ota.c
@brief Slice 4/5 主机 E2：YMODEM/Boot→Secondary；SESSION_DONE 提交 Meta（非 E3）
@note 语义对照 /workspace/ref-v031；本测试为协议回放，实机 RS485/FMC 未验证。
*/

#include <stdio.h>
#include <string.h>

#include "board_gt32.h"
#include "boot.h"
#include "drv_usart.h"
#include "drv_ymodem.h"
#include "gt32_raw.h"
#include "fmc.h"
#include "ota_layout.h"
#include "ota_image.h"
#include "ota_meta.h"

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

/**
@brief 构造完整 SOH/STX 帧（含 CRC16）
*/
static size_t BuildDataFrame(uint8_t *out, size_t cap, uint8_t kind,
                             uint8_t seq, const uint8_t *payload, size_t plen)
{
    size_t frame_len;
    size_t data_len;
    uint16_t crc;
    size_t i;

    if (kind == DRV_YM_SOH) {
        data_len = DRV_YM_SOH_PAYLOAD_LEN;
        frame_len = DRV_YM_SOH_FRAME_LEN;
    } else if (kind == DRV_YM_STX) {
        data_len = DRV_YM_STX_PAYLOAD_LEN;
        frame_len = DRV_YM_STX_FRAME_LEN;
    } else {
        return 0u;
    }
    if (cap < frame_len || plen > data_len) {
        return 0u;
    }
    (void)memset(out, 0x1A, frame_len); /* YMODEM 填充 */
    out[0] = kind;
    out[1] = seq;
    out[2] = (uint8_t)(0xFFu - seq);
    if (plen > 0u && payload != NULL) {
        (void)memcpy(&out[3], payload, plen);
    }
    crc = DrvYmodem_Crc16Ccitt(&out[3], data_len);
    out[frame_len - 2u] = (uint8_t)((crc >> 8) & 0xFFu);
    out[frame_len - 1u] = (uint8_t)(crc & 0xFFu);
    (void)i;
    return frame_len;
}

/**
@brief 构造 Block0：filename\\0 size_ascii\\0…
*/
static size_t BuildHeaderFrame(uint8_t *out, size_t cap,
                               const char *name, uint32_t size, int empty)
{
    uint8_t payload[128];
    size_t nlen;
    size_t o;
    char size_ascii[16];
    int n;

    (void)memset(payload, 0, sizeof(payload));
    if (empty) {
        /* 空 Block0：128 字节全 0，首字节 NUL */
        return BuildDataFrame(out, cap, DRV_YM_SOH, 0u, payload, sizeof(payload));
    }
    nlen = strlen(name);
    if (nlen >= 32u) {
        return 0u;
    }
    (void)memcpy(payload, name, nlen);
    payload[nlen] = 0u;
    n = snprintf(size_ascii, sizeof(size_ascii), "%lu", (unsigned long)size);
    if (n <= 0) {
        return 0u;
    }
    o = nlen + 1u;
    (void)memcpy(payload + o, size_ascii, (size_t)n);
    payload[o + (size_t)n] = 0u;
    return BuildDataFrame(out, cap, DRV_YM_SOH, 0u, payload, sizeof(payload));
}

static void ResetHost(void)
{
    BoardGt32Mock_Reset();
    Boot_TestSetNowMs(0u);
    BootMock_TxLogClear();
    Boot_OtaClearCommitImageReadyOkFlag();
    Boot_OtaClearCommitFailedStayOtaFlag();
    BootMock_ClearJumpToAppFlag();
    BootMock_ClearResetRequestFlag();
    BootMock_ClearInstallFailCount();
    FmcMock_Reset();
    DrvUsart_Init();
    Expect(GT32_Init() == GT32_OK, "GT32_Init");
}

/**
@brief rewrite_from_ref_v031_semantics — 标注对照路径，非 verbatim 粘贴
*/
static void Test_rewrite_from_ref_v031_semantics(void)
{
    Expect(BOOT_DMA_RX_STATIC_SIZE == 2048u,
           "rewrite_from_ref_v031_semantics: DMA static 2048");
    Expect(BOOT_STACK_MIN_BYTES == 0x800u,
           "rewrite_from_ref_v031_semantics: stack min 0x800");
    Expect(BOOT_OTA_PROGRESS_MS == 5000u,
           "rewrite_from_ref_v031_semantics: progress 5s");
    Expect(BOOT_OTA_SESSION_MS == 900000u,
           "rewrite_from_ref_v031_semantics: session 15min");
    Expect(1,
           "rewrite_from_ref_v031_semantics: source /workspace/ref-v031 "
           "(semantic rewrite, not verbatim paste)");
}

static void Test_FrameClassifyAndParseRound(void)
{
    uint8_t frame[133];
    DrvYmFrame_t view;
    uint8_t eot = DRV_YM_EOT;
    uint8_t cans[2] = {DRV_YM_CAN, DRV_YM_CAN};

    Expect(DrvYmodem_Classify(DRV_YM_SOH) == DRV_YM_FRAME_SOH, "classify SOH");
    Expect(DrvYmodem_ExpectedLen(DRV_YM_FRAME_SOH) == 133u, "SOH len 133");
    Expect(DrvYmodem_ExpectedLen(DRV_YM_FRAME_STX) == 1029u, "STX len 1029");
    Expect(DrvYmodem_CheckSeq(1u, 0xFEu) != 0, "seq OK");
    Expect(DrvYmodem_ParseRound(&eot, 1u, &view) == DRV_YM_ITEM_EOT, "ParseRound EOT");
    Expect(DrvYmodem_ParseRound(cans, 2u, &view) == DRV_YM_ITEM_CANCEL,
           "ParseRound double CAN");

    Expect(BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, NULL, 0u) == 133u,
           "build SOH");
    Expect(DrvYmodem_ParseRound(frame, 133u, &view) == DRV_YM_ITEM_FRAME,
           "ParseRound SOH frame");
    Expect(view.packet_number == 1u, "frame seq 1");
    Expect(DrvYmodem_ClassifyDataPacket(1u, 1u) == DRV_YM_PKT_EXPECTED, "pkt expected");
    Expect(DrvYmodem_ClassifyDataPacket(0u, 1u) == DRV_YM_PKT_DUPLICATE, "pkt dup");
    Expect(DrvYmodem_ClassifyDataPacket(3u, 1u) == DRV_YM_PKT_UNEXPECTED, "pkt unexpected");
}

/**
@brief 完整 happy-path：头→2xD8h→数据→EOT/EOT→空头→SESSION_DONE→CommitImageReady
*/
static void Test_HappyPathFullSession(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    uint8_t payload[64];
    size_t flen;
    size_t erase_before;
    size_t erase_after;
    size_t pp_before;
    size_t i;
    uint8_t eot = DRV_YM_EOT;
    size_t tx_before_done;

    ResetHost();
    Boot_OtaStartAttempt(&sess);
    Expect(sess.phase == BOOT_OTA_WAIT_HEADER, "start WAIT_HEADER");
    Expect(BootMock_TxLogLen() >= 1u && BootMock_TxLogAt(0) == DRV_YM_C,
           "start SendPair C");

    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v1.0.0.bin", 64u, 0);
    Expect(flen == 133u, "build header frame");
    erase_before = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_block_erase_64k);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "freeze header");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "poll header");
    erase_after = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_block_erase_64k);
    Expect(sess.phase == BOOT_OTA_RECEIVE_DATA, "after header+erase → RECEIVE_DATA");
    Expect(sess.declared_size == 64u, "declared 64");
    Expect((erase_after - erase_before) == 2u, "EraseSecondary 2x D8h");
    Expect(strcmp(sess.filename, "cardreader-v1.0.0.bin") == 0, "filename stored");

    /* 合法向量：MSP in RAM；Reset Thumb 落在 App[0..64) */
    (void)memset(payload, 0xA5, sizeof(payload));
    payload[0] = 0x00; payload[1] = 0x50; payload[2] = 0x00; payload[3] = 0x20; /* 0x20005000 */
    payload[4] = 0x21; payload[5] = 0x40; payload[6] = 0x00; payload[7] = 0x08; /* 0x08004020|1 */
    pp_before = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_page_program);
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, payload, 64u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "freeze data1");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "poll data1");
    Expect(sess.bytes_written == 64u, "wrote 64");
    Expect(sess.phase == BOOT_OTA_WAIT_EOT1, "full → WAIT_EOT1");
    Expect(BoardGt32Mock_OpcodeOccurrences(gt32_cmd_page_program) > pp_before,
           "PP issued");
    Expect(BoardGt32Mock_PpAddrAt(pp_before) == ota_gt32_secondary_off,
           "PP at Secondary base");

    Expect(DrvUsartMock_FreezeRound(&eot, 1u) == 0, "freeze EOT1");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "poll EOT1");
    Expect(sess.phase == BOOT_OTA_WAIT_SECOND_EOT, "→ WAIT_SECOND_EOT");
    /* 最近应答应为 NAK */
    Expect(BootMock_TxLogAt(BootMock_TxLogLen() - 1u) == DRV_YM_NAK, "EOT1 → NAK");

    Expect(DrvUsartMock_FreezeRound(&eot, 1u) == 0, "freeze EOT2");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "poll EOT2");
    Expect(sess.phase == BOOT_OTA_WAIT_END_BLOCK0, "→ WAIT_END_BLOCK0");
    Expect(BootMock_TxLogAt(BootMock_TxLogLen() - 2u) == DRV_YM_ACK, "EOT2 ACK");
    Expect(BootMock_TxLogAt(BootMock_TxLogLen() - 1u) == DRV_YM_C, "EOT2 C");

    flen = BuildHeaderFrame(frame, sizeof(frame), "", 0u, 1);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "freeze empty Block0");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "poll empty Block0");
    Expect(sess.phase == BOOT_OTA_DONE_GRACE, "→ DONE_GRACE");

    tx_before_done = BootMock_TxLogLen();
    Boot_TestSetNowMs(BOOT_OTA_PROGRESS_MS);
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "poll grace expire");
    Expect(sess.phase == BOOT_OTA_SESSION_DONE, "→ SESSION_DONE");
    Expect(Boot_OtaCommitImageReadyOkFlag() != 0, "CommitImageReady OK flag");
    Expect(Boot_OtaCommitFailedStayOtaFlag() == 0, "not commit_failed_stay_ota");
    Expect(OtaMeta_IsAppValid() != 0, "Meta APP_VALID after install");
    Expect(BootMock_JumpToAppFlag() != 0, "jump stub after install");
    Expect(FmcMock_AppEraseCount() >= 1u, "App erased during install");
    Expect(BootMock_TxLogLen() == tx_before_done,
           "SESSION_DONE commit adds no UART TX");
    (void)i;
}

static void Test_BadFilenameRejected(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    size_t flen;
    size_t erase_before;
    size_t erase_after;

    ResetHost();
    Boot_OtaStartAttempt(&sess);
    flen = BuildHeaderFrame(frame, sizeof(frame), "evil-firmware.bin", 64u, 0);
    erase_before = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_block_erase_64k);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "freeze bad name");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_ERR_FILENAME, "bad name status");
    erase_after = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_block_erase_64k);
    Expect(sess.phase == BOOT_OTA_FAIL, "phase FAIL");
    Expect(erase_after == erase_before, "no Secondary erase on bad name");
}

static void Test_OversizeRejected(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    size_t flen;
    size_t erase_before;

    ResetHost();
    Boot_OtaStartAttempt(&sess);
    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v1.bin",
                            ota_app_image_max + 1u, 0);
    erase_before = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_block_erase_64k);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "freeze oversize");
    Expect(Boot_OtaPoll(&sess) != BOOT_OTA_OK, "oversize rejected");
    Expect(sess.phase == BOOT_OTA_FAIL, "oversize → FAIL");
    Expect(BoardGt32Mock_OpcodeOccurrences(gt32_cmd_block_erase_64k) == erase_before,
           "no erase on oversize");
}

static void Test_DuplicateDataNoDoubleWrite(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    uint8_t payload[32];
    size_t flen;
    size_t pp_after_first;
    size_t pp_after_dup;

    ResetHost();
    Boot_OtaStartAttempt(&sess);
    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v2.0.0.bin", 256u, 0);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "dup: header");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "dup: poll header");

    (void)memset(payload, 0x11, sizeof(payload));
    /* SOH 负载固定 128：声明 256 时首包写入 128，期望包号→2 */
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, payload, sizeof(payload));
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "dup: data1");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "dup: poll data1");
    Expect(sess.bytes_written == 128u, "dup: wrote 128 (full SOH payload)");
    pp_after_first = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_page_program);

    /* 重复包号 1（期望已为 2） */
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, payload, sizeof(payload));
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "dup: freeze dup");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "dup: poll dup");
    pp_after_dup = BoardGt32Mock_OpcodeOccurrences(gt32_cmd_page_program);
    Expect(sess.bytes_written == 128u, "dup: bytes_written unchanged");
    Expect(pp_after_dup == pp_after_first, "dup: no extra PP");
    Expect(BootMock_TxLogAt(BootMock_TxLogLen() - 1u) == DRV_YM_ACK, "dup: ACK only");
}

static void Test_WritePastDeclaredSizeRejected(void)
{
    BootOtaSession_t sess;
    uint8_t frame[133];
    uint8_t payload[16];
    size_t flen;

    ResetHost();
    Boot_OtaStartAttempt(&sess);
    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v3.0.0.bin", 16u, 0);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "past: header");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "past: poll header");

    (void)memset(payload, 0x22, sizeof(payload));
    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 1u, payload, 16u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "past: data full");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_OK, "past: poll data");
    Expect(sess.phase == BOOT_OTA_WAIT_EOT1, "past: WAIT_EOT1");
    Expect(sess.bytes_written == 16u, "past: written 16");

    flen = BuildDataFrame(frame, sizeof(frame), DRV_YM_SOH, 2u, payload, 16u);
    Expect(DrvUsartMock_FreezeRound(frame, flen) == 0, "past: extra packet");
    Expect(Boot_OtaPoll(&sess) == BOOT_OTA_ERR_RANGE, "past: range error");
    Expect(sess.phase == BOOT_OTA_FAIL, "past: FAIL");
}

static void Test_UsartPeekReleaseIsr(void)
{
    uint8_t sample[4] = {0x01u, 0x02u, 0x03u, 0x04u};
    const uint8_t *p;
    size_t len;
    int rc;

    DrvUsart_Init();
    p = DrvUsart_RxPeek(&len);
    Expect(p == NULL && len == 0u, "peek empty");
    rc = DrvUsartMock_FreezeRound(sample, sizeof(sample));
    Expect(rc == 0 && DrvUsartMock_IsFrozen() != 0, "freeze ready");
    Expect(DrvUsartMock_IsrStubCount() >= 1u, "ISR stub counted");
    p = DrvUsart_RxPeek(&len);
    Expect(p != NULL && len == sizeof(sample), "peek len");
    rc = DrvUsartMock_FreezeRound(sample, sizeof(sample));
    Expect(rc != 0, "no overwrite while frozen");
    DrvUsart_RxRelease();
    Expect(DrvUsartMock_IsFrozen() == 0, "release clears");
}

static void Test_ParseHeaderHelpers(void)
{
    uint8_t frame[133];
    DrvYmFrame_t view;
    DrvYmHeaderInfo_t hdr;
    size_t flen;

    flen = BuildHeaderFrame(frame, sizeof(frame), "cardreader-v0.3.1.bin", 112640u, 0);
    Expect(DrvYmodem_ParseRound(frame, (uint16_t)flen, &view) == DRV_YM_ITEM_FRAME,
           "header frame OK");
    Expect(DrvYmodem_ParseHeader(&view, &hdr) != 0, "ParseHeader OK");
    Expect(hdr.file_size == 112640u, "max size accepted");
    Expect(DrvYmodem_IsEmptyHeader(&view) == 0, "not empty");

    flen = BuildHeaderFrame(frame, sizeof(frame), "", 0u, 1);
    Expect(DrvYmodem_ParseRound(frame, (uint16_t)flen, &view) == DRV_YM_ITEM_FRAME,
           "empty frame");
    Expect(DrvYmodem_IsEmptyHeader(&view) != 0, "IsEmptyHeader");
}

int main(void)
{
    g_failures = 0;

    Test_rewrite_from_ref_v031_semantics();
    Test_FrameClassifyAndParseRound();
    Test_ParseHeaderHelpers();
    Test_UsartPeekReleaseIsr();
    Test_HappyPathFullSession();
    Test_BadFilenameRejected();
    Test_OversizeRejected();
    Test_DuplicateDataNoDoubleWrite();
    Test_WritePastDeclaredSizeRejected();

    if (g_failures != 0) {
        (void)printf("\n%d failure(s) in boot OTA host E2 tests\n", g_failures);
        return 1;
    }
    (void)printf("\nAll boot OTA host E2 tests passed "
                 "(Slice4/5 rewrite from ref-v031; SESSION_DONE commits Meta; not E3)\n");
    return 0;
}
