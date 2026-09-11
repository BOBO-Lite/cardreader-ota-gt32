/**
@file ota_meta_codec.h
@brief Meta 88 字节有效头编解码与 Meta CRC32（间断 72 字节覆盖）
*/

#ifndef CARDREADER_OTA_META_CODEC_H
#define CARDREADER_OTA_META_CODEC_H

#include <stddef.h>
#include <stdint.h>

/**
@brief Meta 逻辑记录（主机侧视图；落盘须经 Encode 逐字段序列化）
*/
typedef struct {
    char filename[32];
    char version[16];
    uint32_t image_size;
    uint32_t fw_crc32;
    uint32_t meta_crc32;
    uint32_t image_ready;
    uint32_t app_valid;
    uint32_t mate_ota_fail;
    uint32_t board_id;
} OtaMetaRecord_t;

/**
@brief 编解码结果：0 成功，负值失败（API-004）
*/
typedef enum {
    OTA_META_OK = 0,
    OTA_META_ERR_NULL = -1,
    OTA_META_ERR_SIZE = -2,
    OTA_META_ERR_STRING = -3,
    OTA_META_ERR_CRC = -4,
    OTA_META_ERR_BOARD_ID = -5,
    OTA_META_ERR_FLASH = -6
} OtaMetaStatus_t;

/**
@brief IEEE CRC32（反射，poly 0xEDB88320，初值/终值异或 0xFFFFFFFF）
@param data 输入字节
@param len 字节数
@return CRC32 值；data 为 NULL 且 len>0 时返回 0
*/
uint32_t OtaCrc32_Compute(const uint8_t *data, size_t len);

/**
@brief 按 R02.1 对 Meta 缓冲计算 Meta CRC（先 0x00..0x43，再 0x54..0x57）
@param meta 至少 88 字节的 Meta 缓冲
@return Meta CRC32；meta 为 NULL 时返回 0
*/
uint32_t OtaMeta_ComputeCrc(const uint8_t *meta);

/**
@brief 校验 OTA 文件名规则（小写前缀 cardreader-v…\.bin，文本最长 31）
@param name C 字符串文件名
@return OTA_META_OK；非法返回 OTA_META_ERR_STRING；name 为 NULL 返回 OTA_META_ERR_NULL
*/
OtaMetaStatus_t OtaMeta_ValidateFilename(const char *name);

/**
@brief 将逻辑记录序列化为 88 字节 Meta 缓冲（小端；字符串 NUL 填充）
@note 强制写入 board_id=ota_board_id_gt32（忽略 record->board_id）；
      自动计算并填入 meta_crc32（覆盖 0x00..0x43 与 0x54..0x57）；
      三旗标按 record 原样写入（调用方可先填 0xFFFFFFFF）；
      编码前调用 OtaMeta_ValidateFilename；校验 image_size 属于 [8,112640] 与版本字段长度
@param record 输入逻辑记录（image_size 须在 8..112640；filename 须符合 cardreader-v*.bin）
@param out_meta 输出缓冲，至少 88 字节
@return OTA_META_OK 或负错误码
*/
OtaMetaStatus_t OtaMeta_EncodeRecord(const OtaMetaRecord_t *record,
                                     uint8_t *out_meta);

/**
@brief 从 88 字节 Meta 缓冲解码逻辑记录
@note 始终解析并校验 board_id==ota_board_id_gt32（失败返回 OTA_META_ERR_BOARD_ID），与 verify_meta_crc 无关；
      verify_meta_crc 非 0 时再校验 Meta CRC
@param meta 输入缓冲，至少 88 字节
@param out_record 输出逻辑记录
@param verify_meta_crc 非 0 则额外校验 Meta CRC（board_id 始终校验）
@return OTA_META_OK 或负错误码
*/
OtaMetaStatus_t OtaMeta_DecodeRecord(const uint8_t *meta,
                                     OtaMetaRecord_t *out_record,
                                     int verify_meta_crc);

#endif /* CARDREADER_OTA_META_CODEC_H */
