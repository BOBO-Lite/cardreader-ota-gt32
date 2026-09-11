/**
@file ota_meta.h
@brief Meta 整页事务与旗标提交（R02.1 §3.4.5–3.4.6 / §3.5）
@note 语义参考 /workspace/ref-v031/.../ota_meta.c；按 C 规范重写，非原样粘贴。
*/

#ifndef CARDREADER_OTA_META_H
#define CARDREADER_OTA_META_H

#include <stdint.h>

#include "ota_layout.h"
#include "ota_meta_codec.h"

/** 整页事务最大尝试次数 */
#define OTA_META_FLASH_ATTEMPT_MAX (3u)

/**
@brief 信息区长度（filename..meta_crc）= IMAGE_READY 偏移 = 72
*/
#define OTA_META_INFO_SIZE (ota_meta_off_image_ready)

/**
@brief IMAGE_READY 完整置位（仅全 0）
@return 非 0 表示置位
*/
int OtaMeta_IsImageReadySet(void);

/**
@brief APP_VALID 完整置位（仅全 0；不看 READY/FAIL）
@return 非 0 表示置位
*/
int OtaMeta_IsAppValidFlagSet(void);

/**
@brief MATE_OTA_FAIL 已置位（任何非 0xFFFFFFFF，含半写）
@return 非 0 表示置位
*/
int OtaMeta_IsOtaFailSet(void);

/**
@brief IMAGE_READY 已置位、APP_VALID 未置位、FAIL 未置位 → 待安装
@return 非 0 表示待安装
*/
int OtaMeta_IsInstallPending(void);

/**
@brief IMAGE_READY+APP_VALID 均完整 0，且 FAIL 为 0xFFFFFFFF
@return 非 0 表示 App 有效
*/
int OtaMeta_IsAppValid(void);

/**
@brief 读出已落盘的镜像大小与固件 CRC（不验 Meta CRC）
@param file_size 输出大小
@param image_crc32 输出镜像 CRC
@return OTA_META_OK 或负错误码
*/
OtaMetaStatus_t OtaMeta_ReadImageInfo(uint32_t *file_size, uint32_t *image_crc32);

/**
@brief 整页事务提交 IMAGE_READY（最多 OTA_META_FLASH_ATTEMPT_MAX 次）
@param filename 文件名文本（不必 NUL 结尾，长度由 filename_length 给出）
@param filename_length 文件名字节数（1..31）
@param file_size 声明镜像大小
@param image_crc32 过程累计固件 CRC32
@return OTA_META_OK；参数/编解码/三次事务失败返回负状态（API-004）
@note 每次：擦第一页 → 编程信息+CRC+board_id → 编程 READY=0 并回读。
      脏页不续写。已提交且 FAIL 未置位则不重擦。
*/
OtaMetaStatus_t OtaMeta_CommitImageReady(const char *filename,
                                         uint8_t filename_length,
                                         uint32_t file_size,
                                         uint32_t image_crc32);


/**
@brief 提交 APP_VALID=0（须 READY 已置位且 FAIL 未置位）
@return OTA_META_OK；互斥/半写/硬件失败返回负状态
@note VALID 已为 0 则幂等成功。半写 VALID 当失败、不补写。最多 3 次字编程。
*/
OtaMetaStatus_t OtaMeta_CommitAppValid(void);

/**
@brief 提交 MATE_OTA_FAIL（须 READY 已置位且 VALID 未完整置位）
@return OTA_META_OK；互斥/硬件失败返回负状态
@note FAIL 已置位（含半写）视为已成功（R02 §3.4.6）。最多 3 次字编程。
*/
OtaMetaStatus_t OtaMeta_CommitOtaFail(void);

#endif /* CARDREADER_OTA_META_H */
