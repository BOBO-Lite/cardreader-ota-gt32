/**
@file ota_meta_codec.c
@brief Meta 编解码与 CRC32 实现（R02.1；NVM 逐字段小端序列化）
*/

#include "ota_meta_codec.h"

#include <string.h>

#include "ota_layout.h"

/**
@brief 小端写入 uint32
@param dst 目标地址
@param value 数值
*/
static void OtaMeta_WriteU32Le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/**
@brief 小端读取 uint32
@param src 源地址
@return 数值
*/
static uint32_t OtaMeta_ReadU32Le(const uint8_t *src)
{
    return ((uint32_t)src[0])
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16)
         | ((uint32_t)src[3] << 24);
}

/**
@brief 将 C 字符串写入定长字段（NUL 填充，禁止越界）
@param dst 字段起始
@param field_len 字段总长度（含终止符空间）
@param src 源字符串（可为 NULL，视为空）
@return OTA_META_OK；文本过长（无终止符空间）返回 OTA_META_ERR_STRING
*/
static OtaMetaStatus_t OtaMeta_WriteNulPadded(uint8_t *dst,
                                              size_t field_len,
                                              const char *src)
{
    size_t i;
    size_t n;

    if (dst == NULL || field_len == 0u) {
        return OTA_META_ERR_NULL;
    }

    (void)memset(dst, 0x00, field_len);

    if (src == NULL) {
        return OTA_META_OK;
    }

    n = strlen(src);
    /* 最大有效文本 = field_len - 1（须留终止符） */
    if (n > (field_len - 1u)) {
        return OTA_META_ERR_STRING;
    }

    for (i = 0u; i < n; i++) {
        dst[i] = (uint8_t)src[i];
    }

    return OTA_META_OK;
}

/**
@brief 从定长 NUL 填充字段复制到输出缓冲（保证终止）
@param src 字段起始
@param field_len 字段长度
@param dst 输出（容量至少 field_len）
*/
static void OtaMeta_ReadNulPadded(const uint8_t *src,
                                  size_t field_len,
                                  char *dst)
{
    size_t i;

    (void)memset(dst, 0x00, field_len);
    for (i = 0u; i < field_len; i++) {
        dst[i] = (char)src[i];
        if (src[i] == 0u) {
            break;
        }
    }
    /* 若字段内无 NUL，仍强制末字节为 0，避免越界当 C 字符串用 */
    dst[field_len - 1u] = '\0';
}

/**
@brief 将 image_size 写成十进制 ASCII（NUL 填充）
@param dst 大小字段
@param field_len 字段长度
@param size 镜像大小
@return OTA_META_OK 或错误
*/
static OtaMetaStatus_t OtaMeta_WriteSizeAscii(uint8_t *dst,
                                              size_t field_len,
                                              uint32_t size)
{
    char tmp[16];
    size_t i;
    size_t n;
    uint32_t v;

    if (size < ota_app_image_min || size > ota_app_image_max) {
        return OTA_META_ERR_SIZE;
    }

    (void)memset(tmp, 0x00, sizeof(tmp));
    v = size;
    n = 0u;
    /* 逆序生成十进制数字 */
    do {
        if (n >= (sizeof(tmp) - 1u)) {
            return OTA_META_ERR_SIZE;
        }
        tmp[n] = (char)('0' + (v % 10u));
        v /= 10u;
        n++;
    } while (v > 0u);

    /* 反转为正序 */
    for (i = 0u; i < (n / 2u); i++) {
        char c = tmp[i];
        tmp[i] = tmp[n - 1u - i];
        tmp[n - 1u - i] = c;
    }

    return OtaMeta_WriteNulPadded(dst, field_len, tmp);
}

/**
@brief 解析十进制 ASCII 大小字段
@param src 字段起始
@param field_len 字段长度
@param out_size 解析结果
@return OTA_META_OK 或错误
*/
static OtaMetaStatus_t OtaMeta_ParseSizeAscii(const uint8_t *src,
                                              size_t field_len,
                                              uint32_t *out_size)
{
    size_t i;
    uint32_t acc;
    int seen_digit;

    if (src == NULL || out_size == NULL) {
        return OTA_META_ERR_NULL;
    }

    acc = 0u;
    seen_digit = 0;
    for (i = 0u; i < field_len; i++) {
        uint8_t c = src[i];
        if (c == 0u) {
            break;
        }
        if (c < (uint8_t)'0' || c > (uint8_t)'9') {
            return OTA_META_ERR_SIZE;
        }
        seen_digit = 1;
        /* 溢出检查：acc * 10 + d */
        if (acc > (0xFFFFFFFFu / 10u)) {
            return OTA_META_ERR_SIZE;
        }
        acc = acc * 10u;
        {
            uint32_t d = (uint32_t)(c - (uint8_t)'0');
            if (acc > (0xFFFFFFFFu - d)) {
                return OTA_META_ERR_SIZE;
            }
            acc += d;
        }
    }

    if (seen_digit == 0) {
        return OTA_META_ERR_SIZE;
    }
    if (acc < ota_app_image_min || acc > ota_app_image_max) {
        return OTA_META_ERR_SIZE;
    }

    *out_size = acc;
    return OTA_META_OK;
}

uint32_t OtaCrc32_Compute(const uint8_t *data, size_t len)
{
    uint32_t crc;
    size_t i;
    int bit;

    if (data == NULL) {
        if (len == 0u) {
            /* 空输入仍走算法：初值异或终值 = 0 */
            return 0u ^ 0xFFFFFFFFu ^ 0xFFFFFFFFu;
        }
        return 0u;
    }

    crc = 0xFFFFFFFFu;
    for (i = 0u; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc = (crc >> 1);
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t OtaMeta_ComputeCrc(const uint8_t *meta)
{
    uint32_t crc;
    size_t i;
    int bit;
    const uint8_t *p;
    size_t n;

    if (meta == NULL) {
        return 0u;
    }

    crc = 0xFFFFFFFFu;

    /* 升序：先 0x00..0x43（68 字节），再 0x54..0x57（4 字节） */
    p = meta + ota_meta_crc_span_a_off;
    n = ota_meta_crc_span_a_len;
    for (i = 0u; i < n; i++) {
        crc ^= (uint32_t)p[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc = (crc >> 1);
            }
        }
    }

    p = meta + ota_meta_crc_span_b_off;
    n = ota_meta_crc_span_b_len;
    for (i = 0u; i < n; i++) {
        crc ^= (uint32_t)p[i];
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc = (crc >> 1);
            }
        }
    }

    return crc ^ 0xFFFFFFFFu;
}


OtaMetaStatus_t OtaMeta_ValidateFilename(const char *name)
{
    size_t len;
    size_t i;
    static const char k_prefix[] = "cardreader-v";
    static const char k_suffix[] = ".bin";
    const size_t prefix_len = sizeof(k_prefix) - 1u;
    const size_t suffix_len = sizeof(k_suffix) - 1u;

    if (name == NULL) {
        return OTA_META_ERR_NULL;
    }

    len = strlen(name);
    /* 最大有效文本 31（32 字节字段含终止符） */
    if (len == 0u || len > (ota_meta_len_filename - 1u)) {
        return OTA_META_ERR_STRING;
    }

    if (len < (prefix_len + suffix_len + 1u)) {
        return OTA_META_ERR_STRING;
    }

    if (strncmp(name, k_prefix, prefix_len) != 0) {
        return OTA_META_ERR_STRING;
    }

    if (strcmp(name + (len - suffix_len), k_suffix) != 0) {
        return OTA_META_ERR_STRING;
    }

    /* 版本段：prefix 与 .bin 之间，须含数字；允许 '.'；禁止其它大写/空白 */
    for (i = prefix_len; i < (len - suffix_len); i++) {
        char c = name[i];
        if ((c >= '0' && c <= '9') || (c == '.')) {
            continue;
        }
        return OTA_META_ERR_STRING;
    }

    return OTA_META_OK;
}

OtaMetaStatus_t OtaMeta_EncodeRecord(const OtaMetaRecord_t *record,
                                     uint8_t *out_meta)
{
    OtaMetaStatus_t st;
    uint32_t meta_crc;

    if (record == NULL || out_meta == NULL) {
        return OTA_META_ERR_NULL;
    }

    st = OtaMeta_ValidateFilename(record->filename);
    if (st != OTA_META_OK) {
        return st;
    }

    (void)memset(out_meta, 0x00, ota_meta_record_size);

    st = OtaMeta_WriteNulPadded(out_meta + ota_meta_off_filename,
                                ota_meta_len_filename,
                                record->filename);
    if (st != OTA_META_OK) {
        return st;
    }

    st = OtaMeta_WriteNulPadded(out_meta + ota_meta_off_version,
                                ota_meta_len_version,
                                record->version);
    if (st != OTA_META_OK) {
        return st;
    }

    st = OtaMeta_WriteSizeAscii(out_meta + ota_meta_off_size_ascii,
                                ota_meta_len_size_ascii,
                                record->image_size);
    if (st != OTA_META_OK) {
        return st;
    }

    OtaMeta_WriteU32Le(out_meta + ota_meta_off_fw_crc32, record->fw_crc32);

    /* Meta CRC 槽位先置 0，不参与覆盖；正式值稍后填入 */
    OtaMeta_WriteU32Le(out_meta + ota_meta_off_meta_crc32, 0u);

    OtaMeta_WriteU32Le(out_meta + ota_meta_off_image_ready,
                       record->image_ready);
    OtaMeta_WriteU32Le(out_meta + ota_meta_off_app_valid,
                       record->app_valid);
    OtaMeta_WriteU32Le(out_meta + ota_meta_off_mate_ota_fail,
                       record->mate_ota_fail);

    /* Boot 写入固定 board_id，不取自调用方随意值 */
    OtaMeta_WriteU32Le(out_meta + ota_meta_off_board_id, ota_board_id_gt32);

    meta_crc = OtaMeta_ComputeCrc(out_meta);
    OtaMeta_WriteU32Le(out_meta + ota_meta_off_meta_crc32, meta_crc);

    return OTA_META_OK;
}

OtaMetaStatus_t OtaMeta_DecodeRecord(const uint8_t *meta,
                                     OtaMetaRecord_t *out_record,
                                     int verify_meta_crc)
{
    OtaMetaStatus_t st;
    uint32_t stored_crc;
    uint32_t calc_crc;

    if (meta == NULL || out_record == NULL) {
        return OTA_META_ERR_NULL;
    }

    (void)memset(out_record, 0x00, sizeof(*out_record));

    OtaMeta_ReadNulPadded(meta + ota_meta_off_filename,
                          ota_meta_len_filename,
                          out_record->filename);
    OtaMeta_ReadNulPadded(meta + ota_meta_off_version,
                          ota_meta_len_version,
                          out_record->version);

    st = OtaMeta_ParseSizeAscii(meta + ota_meta_off_size_ascii,
                                ota_meta_len_size_ascii,
                                &out_record->image_size);
    if (st != OTA_META_OK) {
        return st;
    }

    out_record->fw_crc32 = OtaMeta_ReadU32Le(meta + ota_meta_off_fw_crc32);
    out_record->meta_crc32 = OtaMeta_ReadU32Le(meta + ota_meta_off_meta_crc32);
    out_record->image_ready =
        OtaMeta_ReadU32Le(meta + ota_meta_off_image_ready);
    out_record->app_valid =
        OtaMeta_ReadU32Le(meta + ota_meta_off_app_valid);
    out_record->mate_ota_fail =
        OtaMeta_ReadU32Le(meta + ota_meta_off_mate_ota_fail);
    out_record->board_id =
        OtaMeta_ReadU32Le(meta + ota_meta_off_board_id);

    if (out_record->board_id != ota_board_id_gt32) {
        return OTA_META_ERR_BOARD_ID;
    }

    if (verify_meta_crc != 0) {
        stored_crc = out_record->meta_crc32;
        calc_crc = OtaMeta_ComputeCrc(meta);
        if (stored_crc != calc_crc) {
            return OTA_META_ERR_CRC;
        }
    }

    return OTA_META_OK;
}
