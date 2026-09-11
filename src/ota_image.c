/**
@file ota_image.c
@brief Secondary/App 校验与搬运；LastGood 备份（R02.1 §3.5）
@note Backup→Secondary；新增 LastGood。规范重写，非 GD32 头粘贴。
*/

#include "ota_image.h"

#include <string.h>

#include "board_gt32.h"
#include "fmc.h"
#include "gt32_raw.h"
#include "ota_layout.h"

#define OTA_CRC_CHUNK_SIZE (256u)

/**
@brief 校验 MSP/Reset 相对 App 链接范围
@param msp 初始栈指针
@param reset Reset_Handler（含 Thumb 位）
@param image_size 用于 Reset 上界（App 基址 + size）
@return 1 OK
*/
static uint8_t OtaImage_VectorsOk(uint32_t msp, uint32_t reset, uint32_t image_size)
{
    uint32_t reset_addr;
    uint32_t image_end;

    if (image_size < OTA_IMAGE_VECTOR_BYTES || image_size > ota_app_image_max) {
        return 0u;
    }

    reset_addr = reset & ~0x01u;
    image_end = ota_flash_app_base + image_size;

    /* MSP：在 SRAM 内或等于 one-past，且 8 字节对齐 */
    if (msp <= ota_ram_base || msp > ota_ram_end || (msp & 0x07u) != 0u) {
        return 0u;
    }
    if ((reset & 0x01u) == 0u || reset_addr < ota_flash_app_base
        || reset_addr >= image_end) {
        return 0u;
    }
    return 1u;
}

uint32_t OtaImage_Crc32Update(uint32_t crc_state, const uint8_t *data,
                              uint32_t length)
{
    uint32_t index;
    uint8_t bit;

    if (data == NULL && length > 0u) {
        return crc_state;
    }
    for (index = 0u; index < length; index++) {
        crc_state ^= data[index];
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc_state & 1u) != 0u) {
                crc_state = (crc_state >> 1) ^ 0xEDB88320u;
            } else {
                crc_state = (crc_state >> 1);
            }
        }
    }
    return crc_state;
}

uint32_t OtaImage_Crc32Finalize(uint32_t crc_state)
{
    return crc_state ^ 0xFFFFFFFFu;
}

/**
@brief 从缓冲前 8 字节解析向量并校验
*/
static uint8_t OtaImage_CheckVectorBuf(const uint8_t *vec, uint32_t image_size)
{
    uint32_t msp;
    uint32_t reset;

    if (vec == NULL) {
        return 0u;
    }
    msp = ((uint32_t)vec[0]) | ((uint32_t)vec[1] << 8) | ((uint32_t)vec[2] << 16)
          | ((uint32_t)vec[3] << 24);
    reset = ((uint32_t)vec[4]) | ((uint32_t)vec[5] << 8) | ((uint32_t)vec[6] << 16)
            | ((uint32_t)vec[7] << 24);
    return OtaImage_VectorsOk(msp, reset, image_size);
}

uint8_t OtaImage_CheckAppVectors(void)
{
    uint8_t vec[OTA_IMAGE_VECTOR_BYTES];

    if (Fmc_ReadApp(ota_flash_app_base, vec, OTA_IMAGE_VECTOR_BYTES) != FMC_OK) {
        return 0u;
    }
    /* LastGood 判定：Reset 落在整槽 App 范围，不依赖 Meta 大小 / APP_VALID */
    return OtaImage_CheckVectorBuf(vec, ota_app_image_max);
}

uint8_t OtaImage_VerifySecondary(uint32_t image_size, uint32_t expected_crc32)
{
    uint8_t chunk[OTA_CRC_CHUNK_SIZE];
    uint8_t vec[OTA_IMAGE_VECTOR_BYTES];
    uint32_t crc_state;
    uint32_t off;
    uint32_t n;
    GT32_Status_t gst;

    if (image_size < OTA_IMAGE_VECTOR_BYTES || image_size > ota_app_image_max) {
        return 0u;
    }

    gst = GT32_Read(ota_gt32_secondary_off, vec, OTA_IMAGE_VECTOR_BYTES);
    if (gst != GT32_OK) {
        return 0u;
    }
    if (OtaImage_CheckVectorBuf(vec, image_size) == 0u) {
        return 0u;
    }

    crc_state = OTA_IMAGE_CRC32_INITIAL;
    off = 0u;
    while (off < image_size) {
        n = image_size - off;
        if (n > OTA_CRC_CHUNK_SIZE) {
            n = OTA_CRC_CHUNK_SIZE;
        }
        BoardGt32_FeedWdt();
        gst = GT32_Read(ota_gt32_secondary_off + off, chunk, (size_t)n);
        if (gst != GT32_OK) {
            return 0u;
        }
        crc_state = OtaImage_Crc32Update(crc_state, chunk, n);
        off += n;
    }
    return (OtaImage_Crc32Finalize(crc_state) == expected_crc32) ? 1u : 0u;
}

uint8_t OtaImage_VerifyApp(uint32_t image_size, uint32_t expected_crc32)
{
    uint8_t chunk[OTA_CRC_CHUNK_SIZE];
    uint8_t vec[OTA_IMAGE_VECTOR_BYTES];
    uint32_t crc_state;
    uint32_t off;
    uint32_t n;

    if (image_size < OTA_IMAGE_VECTOR_BYTES || image_size > ota_app_image_max) {
        return 0u;
    }
    if (Fmc_ReadApp(ota_flash_app_base, vec, OTA_IMAGE_VECTOR_BYTES) != FMC_OK) {
        return 0u;
    }
    if (OtaImage_CheckVectorBuf(vec, image_size) == 0u) {
        return 0u;
    }

    crc_state = OTA_IMAGE_CRC32_INITIAL;
    off = 0u;
    while (off < image_size) {
        n = image_size - off;
        if (n > OTA_CRC_CHUNK_SIZE) {
            n = OTA_CRC_CHUNK_SIZE;
        }
        BoardGt32_FeedWdt();
        if (Fmc_ReadApp(ota_flash_app_base + off, chunk, (size_t)n) != FMC_OK) {
            return 0u;
        }
        crc_state = OtaImage_Crc32Update(crc_state, chunk, n);
        off += n;
    }
    return (OtaImage_Crc32Finalize(crc_state) == expected_crc32) ? 1u : 0u;
}

uint8_t OtaImage_BackupAppToLastGood(void)
{
    uint8_t chunk[OTA_CRC_CHUNK_SIZE];
    uint32_t off;
    uint32_t n;
    GT32_Status_t gst;

    /* 固定整槽 112640；先擦 LastGood 两块，再按页写入 */
    gst = GT32_EraseLastGoodSlot();
    if (gst != GT32_OK) {
        return 0u;
    }

    off = 0u;
    while (off < ota_app_image_max) {
        n = ota_app_image_max - off;
        if (n > OTA_CRC_CHUNK_SIZE) {
            n = OTA_CRC_CHUNK_SIZE;
        }
        BoardGt32_FeedWdt();
        if (Fmc_ReadApp(ota_flash_app_base + off, chunk, (size_t)n) != FMC_OK) {
            return 0u;
        }
        gst = GT32_WriteLastGood(off, chunk, (size_t)n);
        if (gst != GT32_OK) {
            return 0u;
        }
        off += n;
    }
    return 1u;
}

uint8_t OtaImage_CopySecondaryToApp(uint32_t image_size, uint32_t expected_crc32)
{
    uint8_t chunk[OTA_CRC_CHUNK_SIZE];
    uint8_t vectors[OTA_IMAGE_VECTOR_BYTES];
    uint32_t off;
    uint32_t n;
    uint32_t body_len;
    GT32_Status_t gst;

    if (OtaImage_VerifySecondary(image_size, expected_crc32) == 0u) {
        return 0u;
    }

    if (Fmc_EraseApp() != FMC_OK) {
        return 0u;
    }

    /* 先主体（跳过向量 8B），再写向量，避免半装可启动 */
    body_len = image_size - OTA_IMAGE_VECTOR_BYTES;
    off = OTA_IMAGE_VECTOR_BYTES;
    while (off < image_size) {
        n = image_size - off;
        if (n > OTA_CRC_CHUNK_SIZE) {
            n = OTA_CRC_CHUNK_SIZE;
        }
        BoardGt32_FeedWdt();
        gst = GT32_Read(ota_gt32_secondary_off + off, chunk, (size_t)n);
        if (gst != GT32_OK) {
            return 0u;
        }
        if (Fmc_ProgramApp(ota_flash_app_base + off, chunk, (size_t)n) != FMC_OK) {
            return 0u;
        }
        if (Fmc_CompareApp(ota_flash_app_base + off, chunk, (size_t)n) != FMC_OK) {
            return 0u;
        }
        off += n;
    }
    (void)body_len;

    gst = GT32_Read(ota_gt32_secondary_off, vectors, OTA_IMAGE_VECTOR_BYTES);
    if (gst != GT32_OK) {
        return 0u;
    }
    if (Fmc_ProgramApp(ota_flash_app_base, vectors, OTA_IMAGE_VECTOR_BYTES)
        != FMC_OK) {
        return 0u;
    }
    if (Fmc_CompareApp(ota_flash_app_base, vectors, OTA_IMAGE_VECTOR_BYTES)
        != FMC_OK) {
        return 0u;
    }

    return OtaImage_VerifyApp(image_size, expected_crc32);
}
