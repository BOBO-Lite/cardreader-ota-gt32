/**
@file boot.c
@brief Boot OTA 阶段机 + 选路 / LOAD_A（R02 §2.2 / §3.3；Secondary 在 GT32）
@note 语义参考 /workspace/ref-v031/code/USER/DRIVERS/src/boot.c；按 C 规范重写，非原样粘贴。
      传输过程不写 Meta；SESSION_DONE 调用 CommitImageReady 后进入安装阶段。
      v1 无 LastGood 自动恢复。
*/

#include "boot.h"

#include <string.h>

#include "board_gt32.h"
#include "drv_usart.h"
#include "drv_ymodem.h"
#include "gt32_raw.h"
#include "ota_layout.h"
#include "ota_image.h"
#include "ota_meta.h"

/*
 * 传输路径不得调用 Commit*；SESSION_DONE 调 CommitImageReady，
 * 安装阶段调 CommitAppValid / CommitOtaFail。
 */

#define BOOT_CRC32_INIT (0xFFFFFFFFu)
#define BOOT_TX_LOG_CAP (128u)
#define SEND_CAN_CAN (1u)
#define SEND_NO_CAN  (0u)

/* 供 SESSION_DONE 调用；定义见文件后部 */
void Boot_RunInstallStage(void);

static int s_commit_image_ready_ok_flag;
static uint8_t s_install_fails;
#ifdef BOARD_GT32_HOST_MOCK
static int s_jump_to_app_flag;
static int s_reset_request_flag;
static uint32_t s_handoff_checklist;
static uint32_t s_handoff_vtor;
#endif

#ifdef BOARD_GT32_HOST_MOCK
static int s_commit_failed_stay_ota;
static int s_use_test_now;
static uint32_t s_test_now_ms;
static uint8_t s_tx_log[BOOT_TX_LOG_CAP];
static size_t s_tx_log_len;
#endif

/**
@brief 取得当前毫秒时基
*/
static uint32_t Boot_NowMs(void)
{
#ifdef BOARD_GT32_HOST_MOCK
    if (s_use_test_now != 0) {
        return s_test_now_ms;
    }
#endif
    return BoardGt32_GetTickMs();
}

/**
@brief IEEE CRC32 增量更新（与 OtaCrc32_Compute 同一 poly）
*/
static uint32_t Boot_Crc32Update(uint32_t crc_state, const uint8_t *data, size_t len)
{
    size_t i;
    int bit;

    if (data == NULL && len > 0u) {
        return crc_state;
    }
    for (i = 0u; i < len; i++) {
        crc_state ^= (uint32_t)data[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc_state & 1u) != 0u) {
                crc_state = (crc_state >> 1) ^ 0xEDB88320u;
            } else {
                crc_state = (crc_state >> 1);
            }
        }
    }
    return crc_state;
}

/**
@brief 完成 CRC32（终异或）
*/
static uint32_t Boot_Crc32Finalize(uint32_t crc_state)
{
    return crc_state ^ 0xFFFFFFFFu;
}

/**
@brief 主机或桩：记录发送字节
*/
static void Boot_RecordTx(const uint8_t *bytes, size_t len)
{
#ifdef BOARD_GT32_HOST_MOCK
    size_t i;
    for (i = 0u; i < len; i++) {
        if (s_tx_log_len < BOOT_TX_LOG_CAP) {
            s_tx_log[s_tx_log_len++] = bytes[i];
        }
    }
#else
    (void)bytes;
    (void)len;
    /* 目标板：经 USART 阻塞发送（本切片未接线） */
#endif
}

BootOtaStatus_t Boot_OtaSendPair(uint8_t first, uint8_t second)
{
    uint8_t buf[2];

    if (second == 0u) {
        Boot_RecordTx(&first, 1u);
        return BOOT_OTA_OK;
    }
    buf[0] = first;
    buf[1] = second;
    Boot_RecordTx(buf, 2u);
    return BOOT_OTA_OK;
}

BootOtaStatus_t Boot_OtaReply(uint8_t first, uint8_t second)
{
    DrvUsart_RxRelease();
    return Boot_OtaSendPair(first, second);
}

int Boot_OtaCommitImageReadyOkFlag(void)
{
    return s_commit_image_ready_ok_flag;
}

void Boot_OtaClearCommitImageReadyOkFlag(void)
{
    s_commit_image_ready_ok_flag = 0;
}

/**
@brief SESSION_DONE：提交 IMAGE_READY；失败则 StartAttempt 留 OTA/C
*/
static void Boot_OtaFinishSessionDone(BootOtaSession_t *sess)
{
    OtaMetaStatus_t mst;

    sess->phase = BOOT_OTA_SESSION_DONE;
    /* 进入 SESSION_DONE 时清零安装 RAM 计数（R02 §3.5） */
    s_install_fails = 0u;
    mst = OtaMeta_CommitImageReady(sess->filename, sess->filename_length,
                                   sess->declared_size, sess->image_crc32);
    if (mst == OTA_META_OK) {
        s_commit_image_ready_ok_flag = 1;
        Boot_RunInstallStage();
        return;
    }
    /* 三次事务失败：不装 App；留 Boot 发 C（R02 §3.4.5） */
#ifdef BOARD_GT32_HOST_MOCK
    s_commit_failed_stay_ota = 1;
#endif
    s_commit_image_ready_ok_flag = 0;
    Boot_OtaStartAttempt(sess);
}

#ifdef BOARD_GT32_HOST_MOCK
int Boot_OtaCommitFailedStayOtaFlag(void)
{
    return s_commit_failed_stay_ota;
}

void Boot_OtaClearCommitFailedStayOtaFlag(void)
{
    s_commit_failed_stay_ota = 0;
}

void Boot_TestSetNowMs(uint32_t ms)
{
    s_use_test_now = 1;
    s_test_now_ms = ms;
}

void BootMock_TxLogClear(void)
{
    s_tx_log_len = 0u;
}

size_t BootMock_TxLogLen(void)
{
    return s_tx_log_len;
}

uint8_t BootMock_TxLogAt(size_t index)
{
    if (index >= s_tx_log_len) {
        return 0xFFu;
    }
    return s_tx_log[index];
}
#endif

void Boot_OtaSessionReset(BootOtaSession_t *sess)
{
    if (sess == NULL) {
        return;
    }
    (void)memset(sess, 0, sizeof(*sess));
    sess->phase = BOOT_OTA_IDLE;
}

void Boot_OtaStartAttempt(BootOtaSession_t *sess)
{
    if (sess == NULL) {
        return;
    }
    (void)memset(sess, 0, sizeof(*sess));
    sess->phase = BOOT_OTA_WAIT_HEADER;
    sess->session_base_ms = Boot_NowMs();
    (void)Boot_OtaSendPair(DRV_YM_C, 0u);
    sess->phase_mark_ms = Boot_NowMs();
}

/**
@brief 传输失败出口桩：可选 CAN+CAN，清 RX，相位→FAIL
@note 主机不阻塞 15s 重试；实机重试/复位留后续切片。
*/
static BootOtaStatus_t Boot_OtaFailAttempt(BootOtaSession_t *sess,
                                           uint8_t send_can,
                                           BootOtaStatus_t err)
{
    if (send_can == SEND_CAN_CAN) {
        (void)Boot_OtaSendPair(DRV_YM_CAN, DRV_YM_CAN);
    }
    DrvUsart_RxRelease();
    sess->phase = BOOT_OTA_FAIL;
    return err;
}

/**
@brief 首包前积压头是否与已接受头一致
*/
static int Boot_OtaHeaderMatches(BootOtaSession_t *sess, const DrvYmFrame_t *frame)
{
    DrvYmHeaderInfo_t header;
    uint8_t i;

    if (DrvYmodem_ParseHeader(frame, &header) == 0) {
        return 0;
    }
    if (header.filename_length != sess->filename_length
        || header.file_size != sess->declared_size) {
        return 0;
    }
    for (i = 0u; i < header.filename_length; i++) {
        if (header.filename[i] != (uint8_t)sess->filename[i]) {
            return 0;
        }
    }
    return 1;
}

/**
@brief EOT1 前镜像检查桩：长度一致；向量/Secondary 复核留实机
*/
static int Boot_OtaImageCheckStub(const BootOtaSession_t *sess)
{
    if (sess->bytes_written != sess->declared_size) {
        return 0;
    }
    /* 向量/独立 Secondary CRC 复核：Slice4 主机桩视为通过 */
    (void)sess;
    return 1;
}

/**
@brief 消费 Block0（文件头 / 空头 / 积压头）
*/
static BootOtaStatus_t Boot_OtaHandleHeader(BootOtaSession_t *sess,
                                            const DrvYmFrame_t *frame)
{
    DrvYmHeaderInfo_t header;
    size_t i;

    if (sess->phase == BOOT_OTA_WAIT_HEADER) {
        if (DrvYmodem_IsEmptyHeader(frame) != 0) {
            (void)Boot_OtaReply(DRV_YM_ACK, 0u);
            return BOOT_OTA_OK;
        }
        if (DrvYmodem_ParseHeader(frame, &header) == 0) {
            DrvUsart_RxRelease();
            return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_FILENAME);
        }
        for (i = 0u; i < header.filename_length; i++) {
            sess->filename[i] = (char)header.filename[i];
        }
        sess->filename[header.filename_length] = '\0';
        sess->filename_length = header.filename_length;
        sess->declared_size = header.file_size;
        sess->bytes_written = 0u;
        sess->image_crc_state = BOOT_CRC32_INIT;
        sess->image_crc32 = 0u;
        sess->expected_packet = 1u;
        sess->can_count = 0u;
        (void)Boot_OtaReply(DRV_YM_ACK, 0u);
        sess->phase = BOOT_OTA_ERASE_SECONDARY;
        sess->phase_mark_ms = Boot_NowMs();
        return BOOT_OTA_OK;
    }

    if (sess->phase == BOOT_OTA_RECEIVE_DATA
        && sess->expected_packet == 1u && sess->bytes_written == 0u) {
        if (Boot_OtaHeaderMatches(sess, frame) != 0) {
            (void)Boot_OtaReply(DRV_YM_ACK, DRV_YM_C);
        } else {
            (void)Boot_OtaReply(DRV_YM_NAK, 0u);
        }
        return BOOT_OTA_OK;
    }

    if (sess->phase == BOOT_OTA_WAIT_END_BLOCK0) {
        if (DrvYmodem_IsEmptyHeader(frame) == 0) {
            DrvUsart_RxRelease();
            return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_PROTOCOL);
        }
        (void)Boot_OtaReply(DRV_YM_ACK, 0u);
        sess->phase = BOOT_OTA_DONE_GRACE;
        sess->can_count = 0u;
        sess->phase_mark_ms = Boot_NowMs();
        return BOOT_OTA_OK;
    }

    if (sess->phase == BOOT_OTA_DONE_GRACE) {
        if (DrvYmodem_IsEmptyHeader(frame) != 0) {
            (void)Boot_OtaReply(DRV_YM_ACK, 0u);
            return BOOT_OTA_OK;
        }
    }

    (void)Boot_OtaReply(DRV_YM_NAK, 0u);
    return BOOT_OTA_OK;
}

/**
@brief 写入或确认数据包
*/
static BootOtaStatus_t Boot_OtaHandleData(BootOtaSession_t *sess,
                                          const DrvYmFrame_t *frame)
{
    DrvYmPacketMatch_t match;
    uint32_t remaining;
    uint32_t write_length;
    GT32_Status_t gst;

    if (sess->phase != BOOT_OTA_RECEIVE_DATA
        && sess->phase != BOOT_OTA_WAIT_EOT1) {
        (void)Boot_OtaReply(DRV_YM_NAK, 0u);
        return BOOT_OTA_OK;
    }

    match = DrvYmodem_ClassifyDataPacket(frame->packet_number, sess->expected_packet);
    if (match == DRV_YM_PKT_DUPLICATE) {
        (void)Boot_OtaReply(DRV_YM_ACK, 0u);
        return BOOT_OTA_OK;
    }
    if (match != DRV_YM_PKT_EXPECTED) {
        (void)Boot_OtaReply(DRV_YM_NAK, 0u);
        return BOOT_OTA_OK;
    }

    /* 声明长度已收齐后的新期望包：拒绝写入（含 WAIT_EOT1） */
    if (sess->bytes_written >= sess->declared_size) {
        DrvUsart_RxRelease();
        return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_RANGE);
    }

    remaining = sess->declared_size - sess->bytes_written;
    write_length = (remaining < (uint32_t)frame->payload_length)
                       ? remaining
                       : (uint32_t)frame->payload_length;
    if (write_length == 0u) {
        DrvUsart_RxRelease();
        return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_RANGE);
    }

    gst = GT32_WriteSecondary(sess->bytes_written, frame->payload,
                              (size_t)write_length, sess->declared_size);
    if (gst != GT32_OK) {
        DrvUsart_RxRelease();
        if (gst == GT32_ERR_RANGE) {
            return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_RANGE);
        }
        return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_GT32);
    }

    sess->bytes_written += write_length;
    sess->image_crc_state = Boot_Crc32Update(sess->image_crc_state, frame->payload,
                                             (size_t)write_length);
    sess->expected_packet++;
    (void)Boot_OtaReply(DRV_YM_ACK, 0u);
    sess->phase_mark_ms = Boot_NowMs();

    if (sess->bytes_written >= sess->declared_size) {
        sess->phase = BOOT_OTA_WAIT_EOT1;
    }
    return BOOT_OTA_OK;
}

/**
@brief 双 EOT 握手
*/
static BootOtaStatus_t Boot_OtaHandleEot(BootOtaSession_t *sess)
{
    if (sess->phase == BOOT_OTA_RECEIVE_DATA
        || sess->phase == BOOT_OTA_WAIT_EOT1) {
        sess->image_crc32 = Boot_Crc32Finalize(sess->image_crc_state);
        if (Boot_OtaImageCheckStub(sess) == 0) {
            DrvUsart_RxRelease();
            return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_PROTOCOL);
        }
        (void)Boot_OtaReply(DRV_YM_NAK, 0u);
        sess->phase = BOOT_OTA_WAIT_SECOND_EOT;
        sess->phase_mark_ms = Boot_NowMs();
        return BOOT_OTA_OK;
    }

    if (sess->phase == BOOT_OTA_WAIT_SECOND_EOT) {
        (void)Boot_OtaReply(DRV_YM_ACK, DRV_YM_C);
        sess->phase = BOOT_OTA_WAIT_END_BLOCK0;
        sess->phase_mark_ms = Boot_NowMs();
        return BOOT_OTA_OK;
    }

    if (sess->phase == BOOT_OTA_WAIT_END_BLOCK0) {
        (void)Boot_OtaReply(DRV_YM_ACK, DRV_YM_C);
        return BOOT_OTA_OK;
    }

    (void)Boot_OtaReply(DRV_YM_NAK, 0u);
    return BOOT_OTA_OK;
}

/**
@brief 分类并消费一轮冻结 RX
*/
static BootOtaStatus_t Boot_OtaConsumeRound(BootOtaSession_t *sess,
                                            const uint8_t *data,
                                            uint16_t length)
{
    DrvYmFrame_t frame;
    DrvYmItem_t item;
    BootOtaStatus_t st;

    item = DrvYmodem_ParseRound(data, length, &frame);
    if (item == DRV_YM_ITEM_EMPTY) {
        DrvUsart_RxRelease();
        return BOOT_OTA_OK;
    }
    if (item == DRV_YM_ITEM_CAN) {
        sess->can_count++;
        DrvUsart_RxRelease();
        if (sess->can_count >= 2u) {
            return Boot_OtaFailAttempt(sess, SEND_NO_CAN, BOOT_OTA_ERR_CANCEL);
        }
        return BOOT_OTA_OK;
    }
    if (item == DRV_YM_ITEM_CANCEL) {
        DrvUsart_RxRelease();
        return Boot_OtaFailAttempt(sess, SEND_NO_CAN, BOOT_OTA_ERR_CANCEL);
    }

    sess->can_count = 0u;
    if (item == DRV_YM_ITEM_EOT) {
        return Boot_OtaHandleEot(sess);
    }
    if (item == DRV_YM_ITEM_INVALID) {
        (void)Boot_OtaReply(DRV_YM_NAK, 0u);
        return BOOT_OTA_OK;
    }

    if (frame.packet_number == 0u
        && (sess->phase == BOOT_OTA_WAIT_HEADER
            || sess->phase == BOOT_OTA_WAIT_END_BLOCK0
            || sess->phase == BOOT_OTA_DONE_GRACE
            || (sess->phase == BOOT_OTA_RECEIVE_DATA
                && sess->expected_packet == 1u
                && sess->bytes_written == 0u))) {
        return Boot_OtaHandleHeader(sess, &frame);
    }

    st = Boot_OtaHandleData(sess, &frame);
    return st;
}

/**
@brief 独占擦除 Secondary 全槽，擦完 SendPair(\'C\') → RECEIVE_DATA
*/
static BootOtaStatus_t Boot_OtaEraseSecondary(BootOtaSession_t *sess)
{
    uint32_t erase_start_ms;
    uint32_t erase_end_ms;
    GT32_Status_t gst;

    erase_start_ms = Boot_NowMs();
    gst = GT32_EraseSecondarySlot();
    if (gst != GT32_OK) {
        return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_GT32);
    }
    (void)Boot_OtaSendPair(DRV_YM_C, 0u);
    sess->phase = BOOT_OTA_RECEIVE_DATA;
    erase_end_ms = Boot_NowMs();
    sess->phase_mark_ms = Boot_NowMs();
    /* 擦除墙钟从总会话扣除（R02 §3.3.4） */
    sess->session_base_ms += (uint32_t)(erase_end_ms - erase_start_ms);
    return BOOT_OTA_OK;
}

/**
@brief 服务总会话 / 进展催发 / DONE_GRACE
*/
static BootOtaStatus_t Boot_OtaServiceTimeout(BootOtaSession_t *sess)
{
    uint32_t now = Boot_NowMs();

    if (sess->phase == BOOT_OTA_FAIL
        || sess->phase == BOOT_OTA_IDLE
        || sess->phase == BOOT_OTA_SESSION_DONE
        || sess->phase == BOOT_OTA_ERASE_SECONDARY) {
        return BOOT_OTA_OK;
    }

    if ((uint32_t)(now - sess->session_base_ms) >= BOOT_OTA_SESSION_MS) {
        if (sess->phase == BOOT_OTA_DONE_GRACE) {
            Boot_OtaFinishSessionDone(sess);
            return BOOT_OTA_OK;
        }
        return Boot_OtaFailAttempt(sess, SEND_CAN_CAN, BOOT_OTA_ERR_TIMEOUT);
    }

    if (sess->phase == BOOT_OTA_DONE_GRACE) {
        if ((uint32_t)(now - sess->phase_mark_ms) >= BOOT_OTA_PROGRESS_MS) {
            Boot_OtaFinishSessionDone(sess);
        }
        return BOOT_OTA_OK;
    }

    if ((uint32_t)(now - sess->phase_mark_ms) < BOOT_OTA_PROGRESS_MS) {
        return BOOT_OTA_OK;
    }

    switch (sess->phase) {
    case BOOT_OTA_WAIT_HEADER:
        (void)Boot_OtaSendPair(DRV_YM_C, 0u);
        sess->phase_mark_ms = Boot_NowMs();
        break;
    case BOOT_OTA_RECEIVE_DATA:
        if (sess->bytes_written == 0u) {
            (void)Boot_OtaSendPair(DRV_YM_C, 0u);
        } else {
            (void)Boot_OtaSendPair(DRV_YM_NAK, 0u);
        }
        sess->phase_mark_ms = Boot_NowMs();
        break;
    case BOOT_OTA_WAIT_EOT1:
    case BOOT_OTA_WAIT_SECOND_EOT:
        (void)Boot_OtaSendPair(DRV_YM_NAK, 0u);
        sess->phase_mark_ms = Boot_NowMs();
        break;
    case BOOT_OTA_WAIT_END_BLOCK0:
        (void)Boot_OtaSendPair(DRV_YM_C, 0u);
        sess->phase_mark_ms = Boot_NowMs();
        break;
    default:
        break;
    }
    return BOOT_OTA_OK;
}

BootOtaStatus_t Boot_OtaPoll(BootOtaSession_t *sess)
{
    const uint8_t *data;
    size_t length;
    BootOtaStatus_t st;

    if (sess == NULL) {
        return BOOT_OTA_ERR_NULL;
    }

    if (sess->phase == BOOT_OTA_SESSION_DONE || sess->phase == BOOT_OTA_FAIL
        || sess->phase == BOOT_OTA_IDLE) {
        return (sess->phase == BOOT_OTA_FAIL) ? BOOT_OTA_ERR_PHASE : BOOT_OTA_OK;
    }

    if (sess->phase == BOOT_OTA_ERASE_SECONDARY) {
        st = Boot_OtaEraseSecondary(sess);
        if (st != BOOT_OTA_OK) {
            return st;
        }
        return Boot_OtaServiceTimeout(sess);
    }

    data = DrvUsart_RxPeek(&length);
    if (data != NULL) {
        if (length > 0xFFFFu) {
            length = 0xFFFFu;
        }
        st = Boot_OtaConsumeRound(sess, data, (uint16_t)length);
        if (st != BOOT_OTA_OK) {
            return st;
        }
        if (sess->phase == BOOT_OTA_ERASE_SECONDARY) {
            /* 头 ACK 后同轮继续擦，不插入延时 */
            st = Boot_OtaEraseSecondary(sess);
            if (st != BOOT_OTA_OK) {
                return st;
            }
        }
    }

    return Boot_OtaServiceTimeout(sess);
}


/**
@brief 上电选路（R02 §2.2）：三旗标 + BKP1 消费 + BKP2 门限
*/
BootPath_t Boot_SelectPath(void)
{
    int ready;
    int valid;
    int fail;
    uint16_t bkp2;

    ready = OtaMeta_IsImageReadySet();
    valid = OtaMeta_IsAppValidFlagSet();
    fail = OtaMeta_IsOtaFailSet();

    /* READY 未置位 → OTA */
    if (ready == 0) {
        return BOOT_PATH_OTA;
    }

    /* READY 置位且 FAIL 置位（含 VALID&FAIL）→ OTA，不再安装 */
    if (fail != 0) {
        return BOOT_PATH_OTA;
    }

    /* READY 置位，VALID 未置位，FAIL 未置位 → 安装（优先于 BKP/跳转） */
    if (valid == 0) {
        return BOOT_PATH_INSTALL;
    }

    /* READY+VALID，FAIL 未置位 → BKP1 / BKP2 */
    if (BootBkp_Get1() == BOOT_BKP1_OTA_REQUEST) {
        (void)BootBkp_Set1(BOOT_BKP1_CONSUMED);
        return BOOT_PATH_OTA;
    }

    bkp2 = BootBkp_Get2();
    /* BKP2 >= 5：OTA；v1 不做 LastGood 自动恢复 */
    if (bkp2 >= BOOT_APP_LAUNCH_LIMIT) {
        return BOOT_PATH_OTA;
    }

    /* BKP2++ 回读；失败不得跳转 */
    if (BootBkp_Set2((uint16_t)(bkp2 + 1u)) == 0) {
        return BOOT_PATH_OTA;
    }
    return BOOT_PATH_JUMP;
}

BootPath_t Boot_ColdBootDecide(void)
{
    return Boot_SelectPath();
}

/**
@brief 统一跳转交接（主机记清单位；目标板 TODO 无引脚）
*/
void LOAD_A(uint32_t addr)
{
#ifdef BOARD_GT32_HOST_MOCK
    s_handoff_checklist = 0u;
    s_handoff_vtor = 0u;
#endif

    if (addr != ota_flash_app_base) {
        return;
    }

    /* 不够格：无 VALID 则返回（上电选路随后进 OTA） */
    if (OtaMeta_IsAppValid() == 0) {
        return;
    }

#ifdef BOARD_GT32_TARGET
    /*
     * TODO 实机交接（无引脚号；实机未验证）：
     * - 等待发送结束并释放 RS485 方向
     * - 关闭 Boot DMA / USART 及其中断、SysTick 与其它计时源
     * - 清理 NVIC 与 SysTick/PendSV 挂起
     * - 关中断后设 VTOR=app_base、屏障、切换 MSP，进入 App Reset
     * - 交接前喂狗并保持 WDT 运行
     */
    (void)addr;
    return;
#elif defined(BOARD_GT32_HOST_MOCK)
    /* 主机：记录交接清单位，不模拟真实寄存器 */
    s_handoff_checklist = BOOT_HANDOFF_ALL;
    s_handoff_vtor = addr;
    s_jump_to_app_flag = 1;
#else
    (void)addr;
#endif
}

/**
@brief 安装成功后的统一跳转：BKP2++ 回读后 LOAD_A
*/
static void Boot_RequestJumpToApp(void)
{
    uint16_t launch_count;

    launch_count = BootBkp_Get2();
    if (launch_count >= BOOT_APP_LAUNCH_LIMIT) {
        return;
    }
    if (BootBkp_Set2((uint16_t)(launch_count + 1u)) == 0) {
        return;
    }
    LOAD_A(ota_flash_app_base);
}

/**
@brief 主机：请求复位（FAIL 提交成功后）
*/
static void Boot_RequestReset(void)
{
#ifdef BOARD_GT32_HOST_MOCK
    s_reset_request_flag = 1;
#else
    /* TODO: NVIC system reset */
#endif
}

int Boot_InstallOnce(void)
{
    uint32_t file_size = 0u;
    uint32_t image_crc32 = 0u;

    /* 1. 已 APP_VALID → 成功，不擦 App */
    if (OtaMeta_IsAppValid() != 0) {
        return 1;
    }
    /* 2. 非待安装 → 结束，不由本函数计数 */
    if (OtaMeta_IsInstallPending() == 0) {
        return 0;
    }
    /* 3. ReadImageInfo + 复核 Secondary；失败不擦 App */
    if (OtaMeta_ReadImageInfo(&file_size, &image_crc32) != OTA_META_OK) {
        return 0;
    }
    if (OtaImage_VerifySecondary(file_size, image_crc32) == 0u) {
        return 0;
    }
    /* 4. LastGood：向量 OK 必须整槽备份；NOT OK 跳过 */
    if (OtaImage_CheckAppVectors() != 0u) {
        if (OtaImage_BackupAppToLastGood() == 0u) {
            return 0; /* 不擦 App */
        }
    }
    /* 5–6. 擦 App → Secondary→App（先主体后向量）→ 校验 */
    if (OtaImage_CopySecondaryToApp(file_size, image_crc32) == 0u) {
        return 0;
    }
    /* 7. BKP2=0 回读，早于 VALID */
    if (BootBkp_Set(0u) == 0) {
        return 0;
    }
    /* 8. CommitAppValid */
    if (OtaMeta_CommitAppValid() != OTA_META_OK || OtaMeta_IsAppValid() == 0) {
        return 0;
    }
    return 1;
}

void Boot_RunInstallStage(void)
{
    for (;;) {
        if (OtaMeta_IsAppValid() != 0) {
            Boot_RequestJumpToApp();
            return;
        }
        if (OtaMeta_IsInstallPending() == 0) {
            return;
        }
        if (Boot_InstallOnce() != 0) {
            Boot_RequestJumpToApp();
            return;
        }

        s_install_fails++;
        if (s_install_fails < BOOT_INSTALL_ATTEMPT_MAX) {
            BoardGt32_DelayMs(BOOT_INSTALL_RETRY_MS);
            continue;
        }

        if (OtaMeta_IsInstallPending() != 0) {
            if (OtaMeta_CommitOtaFail() == OTA_META_OK) {
                BoardGt32_DelayMs(BOOT_INSTALL_RETRY_MS);
                Boot_RequestReset();
            } else {
                /* FAIL 写不进：留 Boot 发 C */
#ifdef BOARD_GT32_HOST_MOCK
                s_commit_failed_stay_ota = 1;
#endif
                /* 无会话对象时仅置标志；上电/调用方随后 StartAttempt */
            }
        }
        return;
    }
}

#ifdef BOARD_GT32_HOST_MOCK
int BootMock_JumpToAppFlag(void)
{
    return s_jump_to_app_flag;
}

void BootMock_ClearJumpToAppFlag(void)
{
    s_jump_to_app_flag = 0;
}

int BootMock_ResetRequestFlag(void)
{
    return s_reset_request_flag;
}

void BootMock_ClearResetRequestFlag(void)
{
    s_reset_request_flag = 0;
}

uint8_t BootMock_InstallFailCount(void)
{
    return s_install_fails;
}

void BootMock_ClearInstallFailCount(void)
{
    s_install_fails = 0u;
}

uint32_t BootMock_HandoffChecklist(void)
{
    return s_handoff_checklist;
}

void BootMock_ClearHandoffChecklist(void)
{
    s_handoff_checklist = 0u;
    s_handoff_vtor = 0u;
}

uint32_t BootMock_HandoffVtor(void)
{
    return s_handoff_vtor;
}
#endif
