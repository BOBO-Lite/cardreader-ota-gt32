/**
@file boot.h
@brief Boot OTA 阶段机 + 上电选路 / 统一跳转（R02 §2.2 / §3.3 / §3.5）
@note 语义参考 /workspace/ref-v031/code/USER/DRIVERS/src/boot.c；按 C 规范重写。
*/

#ifndef CARDREADER_BOOT_H
#define CARDREADER_BOOT_H

#include <stddef.h>
#include <stdint.h>

#include "boot_bkp.h"

/**
@brief DMA 单缓冲静态容量（字节）；须 static，禁止栈上大 VLA（R02 §3.2）
*/
#define BOOT_DMA_RX_STATIC_SIZE (2048u)

/**
@brief Boot 主栈最低预算（字节）= 0x800；仅文档/常数，不伪造链接脚本路径
*/
#define BOOT_STACK_MIN_BYTES (0x800u)

/** 进展催发 / DONE_GRACE 固定收尾（毫秒） */
#define BOOT_OTA_PROGRESS_MS (5000u)
/** 总会话期限（毫秒）= 15 分钟 */
#define BOOT_OTA_SESSION_MS (900000u)

/** 安装尝试上限（R02 §3.5） */
#define BOOT_INSTALL_ATTEMPT_MAX (3u)
/** 安装重试间隔（毫秒） */
#define BOOT_INSTALL_RETRY_MS (5000u)

/**
@brief 统一跳转交接检查清单位（主机记录；目标板 TODO）
*/
#define BOOT_HANDOFF_RS485_RELEASED  (1u << 0)
#define BOOT_HANDOFF_DMA_USART_OFF   (1u << 1)
#define BOOT_HANDOFF_SYSTICK_OFF     (1u << 2)
#define BOOT_HANDOFF_NVIC_CLEARED    (1u << 3)
#define BOOT_HANDOFF_VTOR_SET        (1u << 4)
#define BOOT_HANDOFF_MSP_SWITCHED    (1u << 5)
#define BOOT_HANDOFF_WDT_FED         (1u << 6)
/** 主机完整交接清单掩码 */
#define BOOT_HANDOFF_ALL \
    (BOOT_HANDOFF_RS485_RELEASED | BOOT_HANDOFF_DMA_USART_OFF | \
     BOOT_HANDOFF_SYSTICK_OFF | BOOT_HANDOFF_NVIC_CLEARED | \
     BOOT_HANDOFF_VTOR_SET | BOOT_HANDOFF_MSP_SWITCHED | BOOT_HANDOFF_WDT_FED)

/**
@brief 上电选路结果（R02 §2.2）
*/
typedef enum {
    BOOT_PATH_OTA = 0,
    BOOT_PATH_INSTALL = 1,
    BOOT_PATH_JUMP = 2,
    BOOT_PATH_RESET = 3
} BootPath_t;

/**
@brief R02 §3.3 会话相位（ERASE_BACKUP 已映射为 ERASE_SECONDARY）
*/
typedef enum {
    BOOT_OTA_IDLE = 0,
    BOOT_OTA_WAIT_HEADER,
    BOOT_OTA_ERASE_SECONDARY,
    BOOT_OTA_RECEIVE_DATA,
    BOOT_OTA_WAIT_EOT1,
    BOOT_OTA_WAIT_SECOND_EOT,
    BOOT_OTA_WAIT_END_BLOCK0,
    BOOT_OTA_DONE_GRACE,
    BOOT_OTA_SESSION_DONE,
    BOOT_OTA_FAIL
} BootOtaPhase_t;

/**
@brief Boot API 状态：0 成功，负值失败
*/
typedef enum {
    BOOT_OTA_OK = 0,
    BOOT_OTA_ERR_NULL = -1,
    BOOT_OTA_ERR_PARAM = -2,
    BOOT_OTA_ERR_PHASE = -3,
    BOOT_OTA_ERR_FILENAME = -4,
    BOOT_OTA_ERR_SIZE = -5,
    BOOT_OTA_ERR_GT32 = -6,
    BOOT_OTA_ERR_RANGE = -7,
    BOOT_OTA_ERR_PROTOCOL = -8,
    BOOT_OTA_ERR_CANCEL = -9,
    BOOT_OTA_ERR_TIMEOUT = -10,
    BOOT_OTA_ERR_META = -11
} BootOtaStatus_t;

/**
@brief 跨循环会话 RAM（传输路径不写 Meta；仅 SESSION_DONE 提交）
*/
typedef struct {
    BootOtaPhase_t phase;
    uint32_t declared_size;
    uint32_t bytes_written;
    uint8_t expected_packet;
    uint8_t can_count;
    uint8_t filename_length;
    char filename[32];
    uint32_t image_crc_state;
    uint32_t image_crc32;
    uint32_t session_base_ms;
    uint32_t phase_mark_ms;
} BootOtaSession_t;

/**
@brief 复位会话为 IDLE（不触碰 Flash / Meta）
@param sess 会话
*/
void Boot_OtaSessionReset(BootOtaSession_t *sess);

/**
@brief 开始一次接收尝试 → WAIT_HEADER，并 SendPair('C')
@param sess 会话
*/
void Boot_OtaStartAttempt(BootOtaSession_t *sess);

/**
@brief 主循环一步：若相位为 ERASE_SECONDARY 则擦槽；否则 Peek 消费一轮；再服务超时
@param sess 会话
@return BOOT_OTA_OK 或最近一次失败码（phase 可能已为 FAIL/SESSION_DONE）
*/
BootOtaStatus_t Boot_OtaPoll(BootOtaSession_t *sess);

/**
@brief Reply：先 RxRelease 再发送 1～2 字节（主机写入 TX 日志）
@param first 首字节
@param second 次字节；0 表示只发 1 字节
@return BOOT_OTA_OK
*/
BootOtaStatus_t Boot_OtaReply(uint8_t first, uint8_t second);

/**
@brief SendPair：只发送不 Release（催发/开场 C/擦完 C）
@param first 首字节
@param second 次字节；0 表示只发 1 字节
@return BOOT_OTA_OK
*/
BootOtaStatus_t Boot_OtaSendPair(uint8_t first, uint8_t second);

/**
@brief SESSION_DONE 后 CommitImageReady 成功标志（主机/观测）
@return 非 0 已成功提交
*/
int Boot_OtaCommitImageReadyOkFlag(void);

/**
@brief 清除 Commit 成功标志（主机测试复位）
*/
void Boot_OtaClearCommitImageReadyOkFlag(void);


/**
@brief 一次安装尝试（R02 §3.5）：复核 Secondary→LastGood→擦 App→搬运→BKP2→VALID
@return 1 成功（APP_VALID 已置位）；0 本轮失败
@note 已 VALID：直接成功且不擦 App。非待安装：返回 0 且由 RunInstallStage 不计入。
*/
int Boot_InstallOnce(void);

/**
@brief 安装阶段：最多 BOOT_INSTALL_ATTEMPT_MAX 次；第 3 次失败提交 FAIL
@note 成功则主机跳转标志；FAIL 提交失败则留 OTA/C（StartAttempt）。
*/
void Boot_RunInstallStage(void);

/**
@brief 上电选路：只读三 Meta 旗标 + BKP1/BKP2（R02 §2.2）
@return BOOT_PATH_OTA / INSTALL / JUMP；（RESET 为桩预留）
@note 待安装优先于 BKP1/跳转。READY+VALID 且 FAIL 未置位时：
      BKP1==0x3344 → 消费为 0xAABB 并返回 OTA；
      否则 BKP2>=5 → OTA（v1 无 LastGood 自动恢复）；
      否则 BKP2++ 回读成功 → JUMP。回读失败 → OTA。
*/
BootPath_t Boot_SelectPath(void);

/**
@brief 同 Boot_SelectPath（别名）
*/
BootPath_t Boot_ColdBootDecide(void);

/**
@brief 统一跳转：硬件交接清单后进入 App（主机记录清单位；不发明寄存器）
@param addr 必须为 ota_flash_app_base
@note 主机：置交接位后返回。目标板：TODO 外设关闭/VTOR/MSP（无引脚号）。
      不够格或不完整则返回；交接后返回视为跳转失败（调用方复位）。
*/
void LOAD_A(uint32_t addr);

/**
@brief 上电一次：开备份域 → SelectPath → 分支（INSTALL/JUMP/OTA）
@return 最终路径枚举值（JUMP 成功在主机仍返回 JUMP；LOAD_A 返回则改 OTA）
@note 主机可测入口；不开 UART/C 于 INSTALL 路径。
*/
int Boot_MainOnce(void);

#ifdef BOARD_GT32_HOST_MOCK
/**
@brief 主机：最近一次 LOAD_A 交接清单位掩码
@return 位掩码
*/
uint32_t BootMock_HandoffChecklist(void);

/**
@brief 主机：清除交接清单记录
*/
void BootMock_ClearHandoffChecklist(void);

/**
@brief 主机：最近一次 LOAD_A 写入的 VTOR/app_base 记录
@return 地址；未跳转则为 0
*/
uint32_t BootMock_HandoffVtor(void);

/**
@brief 主机：CommitImageReady 三次失败后留 OTA/C 路径标志
@return 非 0 表示 commit 失败并已 StartAttempt
*/
int Boot_OtaCommitFailedStayOtaFlag(void);

/**
@brief 主机：清除 commit_failed_stay_ota
*/
void Boot_OtaClearCommitFailedStayOtaFlag(void);

/**
@brief 主机：强制当前会话时间基准（毫秒）
@param ms 绝对毫秒
*/
void Boot_TestSetNowMs(uint32_t ms);

/**
@brief 主机：清空 TX 日志
*/
void BootMock_TxLogClear(void);

/**
@brief 主机：TX 日志已记录字节数
@return 长度
*/
size_t BootMock_TxLogLen(void);

/**
@brief 主机：取 TX 日志第 index 字节
@param index 下标
@return 字节；越界 0xFF
*/
uint8_t BootMock_TxLogAt(size_t index);

/**
@brief 主机：安装成功后跳转桩是否触发
@return 非 0 已请求跳转
*/
int BootMock_JumpToAppFlag(void);

/**
@brief 主机：清除跳转标志
*/
void BootMock_ClearJumpToAppFlag(void);

/**
@brief 主机：FAIL 提交后复位桩是否触发
@return 非 0 已请求复位
*/
int BootMock_ResetRequestFlag(void);

/**
@brief 主机：清除复位请求标志
*/
void BootMock_ClearResetRequestFlag(void);

/**
@brief 主机：当前安装失败累计次数
@return 次数
*/
uint8_t BootMock_InstallFailCount(void);

/**
@brief 主机：清零安装失败计数（测试复位）
*/
void BootMock_ClearInstallFailCount(void);

#endif /* BOARD_GT32_HOST_MOCK */

#endif /* CARDREADER_BOOT_H */
