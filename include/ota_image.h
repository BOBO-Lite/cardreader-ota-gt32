/**
@file ota_image.h
@brief Secondary/App 镜像校验与搬运；LastGood 整槽备份（R02.1 §3.5）
@note 语义参考 ref-v031 ota_image.c；Backup→Secondary，并增加 LastGood。规范重写，不粘贴 GD32 头。
*/

#ifndef CARDREADER_OTA_IMAGE_H
#define CARDREADER_OTA_IMAGE_H

#include <stdint.h>

/** CRC32 增量初值（与 OtaCrc32_Compute 一致） */
#define OTA_IMAGE_CRC32_INITIAL (0xFFFFFFFFu)

/** 向量表最小长度（MSP + Reset） */
#define OTA_IMAGE_VECTOR_BYTES (8u)

/**
@brief 增量更新反射 CRC32 状态
@param crc_state 初值 0xFFFFFFFF 或上次状态
@param data 字节
@param length 长度
@return 未终结状态
*/
uint32_t OtaImage_Crc32Update(uint32_t crc_state, const uint8_t *data,
                              uint32_t length);

/**
@brief CRC32 终异或
@param crc_state 未终结状态
@return 最终 CRC32
*/
uint32_t OtaImage_Crc32Finalize(uint32_t crc_state);

/**
@brief 校验 Secondary：长度、向量、整镜像 CRC（经 GT32_Read）
@param image_size Meta 声明大小
@param expected_crc32 Meta fw_crc32
@return 1 通过；0 失败
*/
uint8_t OtaImage_VerifySecondary(uint32_t image_size, uint32_t expected_crc32);

/**
@brief 校验 App：长度、向量、整镜像 CRC（经 Fmc_ReadApp）
@param image_size 安装大小
@param expected_crc32 期望 CRC
@return 1 通过；0 失败
*/
uint8_t OtaImage_VerifyApp(uint32_t image_size, uint32_t expected_crc32);

/**
@brief 仅检查当前 App 向量（LastGood 前置；不要求 Meta APP_VALID）
@note MSP 在 SRAM 且 8 字节对齐；Reset Thumb 且落在
      [ota_flash_app_base, ota_flash_app_base+ota_app_image_max)
@return 1 向量 OK；0 NOT OK
*/
uint8_t OtaImage_CheckAppVectors(void);

/**
@brief 整槽备份当前 App → GT32 LastGood（固定 ota_app_image_max 字节）
@return 1 成功；0 失败（调用方不得擦 App）
*/
uint8_t OtaImage_BackupAppToLastGood(void);

/**
@brief 擦 App 后 Secondary→App（先主体后向量），再校验
@param image_size 镜像长度
@param expected_crc32 期望 CRC
@return 1 成功；0 失败（Secondary 复核失败时不擦 App）
*/
uint8_t OtaImage_CopySecondaryToApp(uint32_t image_size, uint32_t expected_crc32);

#endif /* CARDREADER_OTA_IMAGE_H */
