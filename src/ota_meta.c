/**
@file ota_meta.c
@brief Meta 整页事务 CommitImageReady（R02.1 §3.4.5）
@note 语义参考 /workspace/ref-v031/.../ota_meta.c；规范重写，非 GD32 头粘贴。
*/

#include "ota_meta.h"

#include <string.h>

#include "fmc.h"
#include "ota_layout.h"

/* 文件名前缀 cardreader-；版本段为 v<digits>.<digits>.<digits>（R02 / ref-v031） */
#define OTA_META_PREFIX "cardreader-"
#define OTA_META_PREFIX_LEN (11u)
#define OTA_META_SUFFIX ".bin"
#define OTA_META_SUFFIX_LEN (4u)

/**
@brief 校验版本文本：v + 三段十进制（至少各 1 位数字）
*/
static int OtaMeta_VersionTextOk(const char *ver, uint8_t ver_len)
{
    uint8_t i;
    uint8_t dots;
    uint8_t digits_in_part;

    if (ver == NULL || ver_len < 6u) {
        return 0;
    }
    if (ver[0] != 'v') {
        return 0;
    }
    dots = 0u;
    digits_in_part = 0u;
    for (i = 1u; i < ver_len; i++) {
        char c = ver[i];
        if (c >= '0' && c <= '9') {
            digits_in_part++;
            continue;
        }
        if (c == '.') {
            if (digits_in_part == 0u) {
                return 0;
            }
            dots++;
            digits_in_part = 0u;
            if (dots > 2u) {
                return 0;
            }
            continue;
        }
        return 0;
    }
    if (dots != 2u || digits_in_part == 0u) {
        return 0;
    }
    return 1;
}

/**
@brief 从文件名提取版本文本到 version[16]（含开头 v，例 v0.3.1）
@note 前缀为 cardreader-（非 cardreader-v）；对齐 R02 §3.4.2 与 ref-v031。
@return OTA_META_OK 或 OTA_META_ERR_STRING / NULL
*/
static OtaMetaStatus_t OtaMeta_ExtractVersion(const char *filename,
                                              uint8_t filename_length,
                                              char *version_out)
{
    uint8_t ver_len;

    if (filename == NULL || version_out == NULL) {
        return OTA_META_ERR_NULL;
    }
    if (filename_length <= (uint8_t)(OTA_META_PREFIX_LEN + OTA_META_SUFFIX_LEN)
        || filename_length > (uint8_t)(ota_meta_len_filename - 1u)) {
        return OTA_META_ERR_STRING;
    }
    if (memcmp(filename, OTA_META_PREFIX, OTA_META_PREFIX_LEN) != 0) {
        return OTA_META_ERR_STRING;
    }
    if (memcmp(filename + filename_length - OTA_META_SUFFIX_LEN,
               OTA_META_SUFFIX, OTA_META_SUFFIX_LEN) != 0) {
        return OTA_META_ERR_STRING;
    }
    ver_len = (uint8_t)(filename_length - OTA_META_PREFIX_LEN - OTA_META_SUFFIX_LEN);
    if (ver_len == 0u || ver_len > (uint8_t)(ota_meta_len_version - 1u)) {
        return OTA_META_ERR_STRING;
    }
    if (OtaMeta_VersionTextOk(filename + OTA_META_PREFIX_LEN, ver_len) == 0) {
        return OTA_META_ERR_STRING;
    }
    (void)memset(version_out, 0, ota_meta_len_version);
    (void)memcpy(version_out, filename + OTA_META_PREFIX_LEN, ver_len);
    return OTA_META_OK;
}

/**
@brief FAIL 已置位（含半写）：任何非 0xFFFFFFFF
*/
int OtaMeta_IsOtaFailSet(void)
{
    uint32_t word = ota_meta_flag_erased;

    if (Fmc_ReadMetaWord(ota_meta_off_mate_ota_fail, &word) != FMC_OK) {
        return 1;
    }
    return (word != ota_meta_flag_erased) ? 1 : 0;
}

/**
@brief READY 完整置位（仅全 0）
*/
int OtaMeta_IsImageReadySet(void)
{
    uint32_t word = ota_meta_flag_erased;

    if (Fmc_ReadMetaWord(ota_meta_off_image_ready, &word) != FMC_OK) {
        return 0;
    }
    return (word == ota_meta_flag_set) ? 1 : 0;
}

/**
@brief VALID 完整置位（仅全 0）
*/
int OtaMeta_IsAppValidFlagSet(void)
{
    uint32_t word = ota_meta_flag_erased;

    if (Fmc_ReadMetaWord(ota_meta_off_app_valid, &word) != FMC_OK) {
        return 0;
    }
    return (word == ota_meta_flag_set) ? 1 : 0;
}

int OtaMeta_IsInstallPending(void)
{
    return (OtaMeta_IsImageReadySet() != 0
            && OtaMeta_IsAppValidFlagSet() == 0
            && OtaMeta_IsOtaFailSet() == 0) ? 1 : 0;
}

int OtaMeta_IsAppValid(void)
{
    return (OtaMeta_IsImageReadySet() != 0
            && OtaMeta_IsAppValidFlagSet() != 0
            && OtaMeta_IsOtaFailSet() == 0) ? 1 : 0;
}

OtaMetaStatus_t OtaMeta_ReadImageInfo(uint32_t *file_size, uint32_t *image_crc32)
{
    uint8_t meta[ota_meta_record_size];
    OtaMetaRecord_t rec;
    OtaMetaStatus_t st;

    if (file_size == NULL || image_crc32 == NULL) {
        return OTA_META_ERR_NULL;
    }
    if (Fmc_ReadMeta(0u, meta, ota_meta_record_size) != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }
    /* 不验 Meta CRC；board_id 仍由 Decode 校验 */
    st = OtaMeta_DecodeRecord(meta, &rec, 0);
    if (st != OTA_META_OK) {
        return st;
    }
    *file_size = rec.image_size;
    *image_crc32 = rec.fw_crc32;
    return OTA_META_OK;
}

/**
@brief 编程 READY=0 并回读；已是 0 则成功；半写失败不补写
*/
static OtaMetaStatus_t OtaMeta_ProgramReadyOnce(void)
{
    uint32_t current = ota_meta_flag_erased;

    if (Fmc_ReadMetaWord(ota_meta_off_image_ready, &current) != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }
    if (current == ota_meta_flag_set) {
        return OTA_META_OK;
    }
    if (current != ota_meta_flag_erased) {
        /* 半写：本轮失败，禁止补写 */
        return OTA_META_ERR_FLASH;
    }
    if (Fmc_ProgramMetaWord(ota_meta_off_image_ready, ota_meta_flag_set) != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }
    if (Fmc_ReadMetaWord(ota_meta_off_image_ready, &current) != FMC_OK
        || current != ota_meta_flag_set) {
        return OTA_META_ERR_FLASH;
    }
    return OTA_META_OK;
}

/**
@brief 单次整页事务：擦 → 编 88B（旗标 F）回读 → READY=0 回读
*/
static OtaMetaStatus_t OtaMeta_CommitAttempt(const uint8_t *record)
{
    uint32_t board_id = 0u;

    if (Fmc_EraseMetaPage() != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }

    /* 连续编程 0x00..0x57（88B），三旗标为 0xFFFFFFFF */
    if (Fmc_ProgramMeta(0u, record, ota_meta_record_size) != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }
    if (Fmc_CompareMeta(0u, record, ota_meta_record_size) != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }
    if (Fmc_ReadMetaWord(ota_meta_off_board_id, &board_id) != FMC_OK
        || board_id != ota_board_id_gt32) {
        return OTA_META_ERR_FLASH;
    }

    return OtaMeta_ProgramReadyOnce();
}

OtaMetaStatus_t OtaMeta_CommitImageReady(const char *filename,
                                         uint8_t filename_length,
                                         uint32_t file_size,
                                         uint32_t image_crc32)
{
    OtaMetaRecord_t rec;
    uint8_t record[ota_meta_record_size];
    OtaMetaStatus_t st;
    uint8_t attempt;
    uint32_t board_id = 0u;
    char name_tmp[ota_meta_len_filename];

    if (filename == NULL) {
        return OTA_META_ERR_NULL;
    }
    if (filename_length == 0u
        || filename_length > (uint8_t)(ota_meta_len_filename - 1u)) {
        return OTA_META_ERR_STRING;
    }

    (void)memset(&rec, 0, sizeof(rec));
    (void)memset(name_tmp, 0, sizeof(name_tmp));
    (void)memcpy(name_tmp, filename, filename_length);
    st = OtaMeta_ValidateFilename(name_tmp);
    if (st != OTA_META_OK) {
        return st;
    }
    (void)memcpy(rec.filename, name_tmp, sizeof(rec.filename));
    st = OtaMeta_ExtractVersion(filename, filename_length, rec.version);
    if (st != OTA_META_OK) {
        return st;
    }
    rec.image_size = file_size;
    rec.fw_crc32 = image_crc32;
    rec.image_ready = ota_meta_flag_erased;
    rec.app_valid = ota_meta_flag_erased;
    rec.mate_ota_fail = ota_meta_flag_erased;
    rec.board_id = ota_board_id_gt32;

    st = OtaMeta_EncodeRecord(&rec, record);
    if (st != OTA_META_OK) {
        return st;
    }

    /*
     * 已提交：信息区 72B 与 RAM 一致、board_id=0x0001、READY=0、FAIL 全 F。
     * FAIL 半写/已置位 → 必须整页重建。
     */
    if (Fmc_CompareMeta(0u, record, OTA_META_INFO_SIZE) == FMC_OK
        && Fmc_ReadMetaWord(ota_meta_off_board_id, &board_id) == FMC_OK
        && board_id == ota_board_id_gt32
        && OtaMeta_IsImageReadySet() != 0
        && OtaMeta_IsOtaFailSet() == 0) {
        return OTA_META_OK;
    }

    for (attempt = 0u; attempt < OTA_META_FLASH_ATTEMPT_MAX; attempt++) {
        if (OtaMeta_CommitAttempt(record) == OTA_META_OK) {
            return OTA_META_OK;
        }
        /* 任一步失败：结束本轮，下一轮从整页擦除开始（不脏页续写） */
    }
    return OTA_META_ERR_FLASH;
}

/**
@brief 编程单个旗标字：已是目标成功；半写失败；全 F 则写并回读
*/
static OtaMetaStatus_t OtaMeta_ProgramFlagOnce(uint32_t offset)
{
    uint32_t current = ota_meta_flag_erased;

    if (Fmc_ReadMetaWord(offset, &current) != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }
    if (current == ota_meta_flag_set) {
        return OTA_META_OK;
    }
    if (current != ota_meta_flag_erased) {
        /* 半写：本轮失败，禁止补写 */
        return OTA_META_ERR_FLASH;
    }
    if (Fmc_ProgramMetaWord(offset, ota_meta_flag_set) != FMC_OK) {
        return OTA_META_ERR_FLASH;
    }
    if (Fmc_ReadMetaWord(offset, &current) != FMC_OK
        || current != ota_meta_flag_set) {
        return OTA_META_ERR_FLASH;
    }
    return OTA_META_OK;
}

/**
@brief 旗标字最多 OTA_META_FLASH_ATTEMPT_MAX 次编程
*/
static OtaMetaStatus_t OtaMeta_ProgramFlagWithRetry(uint32_t offset)
{
    uint8_t attempt;

    for (attempt = 0u; attempt < OTA_META_FLASH_ATTEMPT_MAX; attempt++) {
        if (OtaMeta_ProgramFlagOnce(offset) == OTA_META_OK) {
            return OTA_META_OK;
        }
    }
    return OTA_META_ERR_FLASH;
}

OtaMetaStatus_t OtaMeta_CommitAppValid(void)
{
    /* READY 必须完整置位；FAIL（含半写）已置位则互斥拒绝 */
    if (OtaMeta_IsImageReadySet() == 0 || OtaMeta_IsOtaFailSet() != 0) {
        return OTA_META_ERR_FLASH;
    }
    if (OtaMeta_IsAppValidFlagSet() != 0) {
        return OTA_META_OK;
    }
    return OtaMeta_ProgramFlagWithRetry(ota_meta_off_app_valid);
}

OtaMetaStatus_t OtaMeta_CommitOtaFail(void)
{
    /* READY 必须完整置位；VALID 完整置位则互斥拒绝 */
    if (OtaMeta_IsImageReadySet() == 0 || OtaMeta_IsAppValidFlagSet() != 0) {
        return OTA_META_ERR_FLASH;
    }
    /* FAIL 半写/已置位视为已成功（R02 §3.4.6） */
    if (OtaMeta_IsOtaFailSet() != 0) {
        return OTA_META_OK;
    }
    return OtaMeta_ProgramFlagWithRetry(ota_meta_off_mate_ota_fail);
}
