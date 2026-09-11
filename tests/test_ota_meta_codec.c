/**
@file test_ota_meta_codec.c
@brief Slice 1 主机单测：CRC32 向量、Meta 编解码、board_id/偏移
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "ota_layout.h"
#include "ota_meta_codec.h"

static int s_failures = 0;

/**
@brief 断言相等（uint32）
*/
static void ExpectEqU32(const char *name, uint32_t got, uint32_t expect)
{
    if (got != expect) {
        (void)printf("FAIL: %s got=0x%08X expect=0x%08X\n",
                     name, (unsigned)got, (unsigned)expect);
        s_failures++;
    } else {
        (void)printf("PASS: %s\n", name);
    }
}

/**
@brief 断言状态码
*/
static void ExpectStatus(const char *name,
                         OtaMetaStatus_t got,
                         OtaMetaStatus_t expect)
{
    if (got != expect) {
        (void)printf("FAIL: %s status=%d expect=%d\n",
                     name, (int)got, (int)expect);
        s_failures++;
    } else {
        (void)printf("PASS: %s\n", name);
    }
}

/**
@brief 断言内存相等
*/
static void ExpectMem(const char *name,
                      const void *got,
                      const void *expect,
                      size_t n)
{
    if (memcmp(got, expect, n) != 0) {
        (void)printf("FAIL: %s memcmp mismatch (len=%u)\n",
                     name, (unsigned)n);
        s_failures++;
    } else {
        (void)printf("PASS: %s\n", name);
    }
}

/**
@brief AC：ASCII "123456789" → 0xCBF43926
*/
static void TestCrc32Vector(void)
{
    static const uint8_t k_msg[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };
    uint32_t crc = OtaCrc32_Compute(k_msg, sizeof(k_msg));
    ExpectEqU32("CRC32(\"123456789\")", crc, 0xCBF43926u);
}

/**
@brief 布局常数抽样
*/
static void TestLayoutConstants(void)
{
    ExpectEqU32("boot_base", ota_flash_boot_base, 0x08000000u);
    ExpectEqU32("app_base", ota_flash_app_base, 0x08004000u);
    ExpectEqU32("app_image_max", ota_app_image_max, 112640u);
    ExpectEqU32("meta_base", ota_flash_meta_base, 0x0801F800u);
    ExpectEqU32("meta_page_size", ota_flash_meta_page_size, 1024u);
    ExpectEqU32("secondary_off", ota_gt32_secondary_off, 0x00000u);
    ExpectEqU32("lastgood_off", ota_gt32_lastgood_off, 0x20000u);
    ExpectEqU32("board_id", ota_board_id_gt32, 0x0001u);
    ExpectEqU32("meta_off_board_id", ota_meta_off_board_id, 0x54u);
    ExpectEqU32("meta_record_size", ota_meta_record_size, 88u);
    ExpectEqU32("meta_crc_cover_len", ota_meta_crc_cover_len, 72u);
}

/**
@brief Encode/Decode 往返与 board_id/偏移/CRC
*/
static void TestEncodeDecodeRoundtrip(void)
{
    OtaMetaRecord_t in;
    OtaMetaRecord_t out;
    uint8_t buf[88];
    OtaMetaStatus_t st;
    uint32_t board_le;
    uint32_t stored_crc;
    uint32_t calc_crc;

    (void)memset(&in, 0x00, sizeof(in));
    (void)strncpy(in.filename, "cardreader-v0.3.1.bin", sizeof(in.filename) - 1u);
    (void)strncpy(in.version, "v0.3.1", sizeof(in.version) - 1u);
    in.image_size = 112640u;
    in.fw_crc32 = 0xA1B2C3D4u;
    in.image_ready = ota_meta_flag_erased;
    in.app_valid = ota_meta_flag_erased;
    in.mate_ota_fail = ota_meta_flag_erased;
    in.board_id = 0xDEADu; /* Encode 应强制写成 0x0001 */

    (void)memset(buf, 0xA5, sizeof(buf));
    st = OtaMeta_EncodeRecord(&in, buf);
    ExpectStatus("EncodeRecord", st, OTA_META_OK);

    /* 文件名偏移 */
    ExpectMem("filename@0x00",
              buf + ota_meta_off_filename,
              "cardreader-v0.3.1.bin",
              strlen("cardreader-v0.3.1.bin") + 1u);

    /* 版本偏移 */
    ExpectMem("version@0x20",
              buf + ota_meta_off_version,
              "v0.3.1",
              strlen("v0.3.1") + 1u);

    /* 大小十进制 ASCII */
    ExpectMem("size_ascii@0x30",
              buf + ota_meta_off_size_ascii,
              "112640",
              strlen("112640") + 1u);

    /* fw CRC LE @0x40 */
    ExpectEqU32("fw_crc_b0", buf[ota_meta_off_fw_crc32 + 0u], 0xD4u);
    ExpectEqU32("fw_crc_b1", buf[ota_meta_off_fw_crc32 + 1u], 0xC3u);
    ExpectEqU32("fw_crc_b2", buf[ota_meta_off_fw_crc32 + 2u], 0xB2u);
    ExpectEqU32("fw_crc_b3", buf[ota_meta_off_fw_crc32 + 3u], 0xA1u);

    /* board_id LE @0x54 == 0x0001 */
    board_le = (uint32_t)buf[0x54]
             | ((uint32_t)buf[0x55] << 8)
             | ((uint32_t)buf[0x56] << 16)
             | ((uint32_t)buf[0x57] << 24);
    ExpectEqU32("board_id@0x54", board_le, 0x0001u);

    stored_crc = (uint32_t)buf[0x44]
               | ((uint32_t)buf[0x45] << 8)
               | ((uint32_t)buf[0x46] << 16)
               | ((uint32_t)buf[0x47] << 24);
    calc_crc = OtaMeta_ComputeCrc(buf);
    ExpectEqU32("meta_crc_matches_compute", stored_crc, calc_crc);

    /* 间断覆盖：改动旗标区不应改变 Meta CRC 计算结果所用输入；
       但改动 0x00..0x43 或 board_id 应改变 */
    {
        uint8_t buf2[88];
        uint32_t c1;
        uint32_t c2;
        (void)memcpy(buf2, buf, sizeof(buf2));
        buf2[0x48] = 0x00; /* IMAGE_READY 不在 CRC 覆盖内 */
        c1 = OtaMeta_ComputeCrc(buf);
        c2 = OtaMeta_ComputeCrc(buf2);
        ExpectEqU32("flag_change_keeps_meta_crc", c1, c2);

        buf2[0x10] ^= 0x01u; /* 文件名字段内 */
        c2 = OtaMeta_ComputeCrc(buf2);
        if (c1 == c2) {
            (void)printf("FAIL: filename_change_should_alter_meta_crc\n");
            s_failures++;
        } else {
            (void)printf("PASS: filename_change_alters_meta_crc\n");
        }
    }

    st = OtaMeta_DecodeRecord(buf, &out, 1);
    ExpectStatus("DecodeRecord+verify", st, OTA_META_OK);
    ExpectEqU32("decoded_size", out.image_size, 112640u);
    ExpectEqU32("decoded_fw_crc", out.fw_crc32, 0xA1B2C3D4u);
    ExpectEqU32("decoded_board_id", out.board_id, 0x0001u);
    if (strcmp(out.filename, "cardreader-v0.3.1.bin") != 0) {
        (void)printf("FAIL: decoded_filename\n");
        s_failures++;
    } else {
        (void)printf("PASS: decoded_filename\n");
    }
    if (strcmp(out.version, "v0.3.1") != 0) {
        (void)printf("FAIL: decoded_version\n");
        s_failures++;
    } else {
        (void)printf("PASS: decoded_version\n");
    }
}

/**
@brief 大小边界
*/
static void TestSizeBounds(void)
{
    OtaMetaRecord_t in;
    uint8_t buf[88];
    OtaMetaStatus_t st;

    (void)memset(&in, 0x00, sizeof(in));
    (void)strncpy(in.filename, "cardreader-v0.1.0.bin", sizeof(in.filename) - 1u);
    (void)strncpy(in.version, "v0.1.0", sizeof(in.version) - 1u);
    in.image_ready = ota_meta_flag_erased;
    in.app_valid = ota_meta_flag_erased;
    in.mate_ota_fail = ota_meta_flag_erased;

    in.image_size = 7u;
    st = OtaMeta_EncodeRecord(&in, buf);
    ExpectStatus("size_too_small", st, OTA_META_ERR_SIZE);

    in.image_size = 112641u;
    st = OtaMeta_EncodeRecord(&in, buf);
    ExpectStatus("size_too_large", st, OTA_META_ERR_SIZE);

    in.image_size = 8u;
    st = OtaMeta_EncodeRecord(&in, buf);
    ExpectStatus("size_min_ok", st, OTA_META_OK);

    in.image_size = 112640u;
    st = OtaMeta_EncodeRecord(&in, buf);
    ExpectStatus("size_max_ok", st, OTA_META_OK);
}

/**
@brief CRC 篡改应导致 Decode 校验失败
*/

/**
@brief 文件名边界（AC-S1-4）
*/
static void TestFilenameBounds(void)
{
    OtaMetaRecord_t in;
    uint8_t buf[88];
    OtaMetaStatus_t st;
    char long_name[40];
    size_t i;

    ExpectStatus("filename_ok",
                 OtaMeta_ValidateFilename("cardreader-v0.3.1.bin"),
                 OTA_META_OK);
    ExpectStatus("filename_bad_prefix",
                 OtaMeta_ValidateFilename("CardReader-v0.3.1.bin"),
                 OTA_META_ERR_STRING);
    ExpectStatus("filename_not_cardreader",
                 OtaMeta_ValidateFilename("other-v0.3.1.bin"),
                 OTA_META_ERR_STRING);
    ExpectStatus("filename_no_suffix",
                 OtaMeta_ValidateFilename("cardreader-v0.3.1"),
                 OTA_META_ERR_STRING);
    ExpectStatus("filename_bad_char",
                 OtaMeta_ValidateFilename("cardreader-v0.3.1-rc.bin"),
                 OTA_META_ERR_STRING);
    ExpectStatus("filename_null",
                 OtaMeta_ValidateFilename(NULL),
                 OTA_META_ERR_NULL);

    /* 32 字符文本（无终止空间）应拒绝 */
    for (i = 0u; i < 32u; i++) {
        long_name[i] = 'a';
    }
    long_name[32] = '\0';
    ExpectStatus("filename_too_long_raw",
                 OtaMeta_ValidateFilename(long_name),
                 OTA_META_ERR_STRING);

    (void)memset(&in, 0x00, sizeof(in));
    (void)strncpy(in.filename, "other-v0.1.0.bin", sizeof(in.filename) - 1u);
    (void)strncpy(in.version, "v0.1.0", sizeof(in.version) - 1u);
    in.image_size = 8u;
    in.image_ready = ota_meta_flag_erased;
    in.app_valid = ota_meta_flag_erased;
    in.mate_ota_fail = ota_meta_flag_erased;
    st = OtaMeta_EncodeRecord(&in, buf);
    ExpectStatus("encode_rejects_bad_filename", st, OTA_META_ERR_STRING);
}

static void TestCrcTamper(void)
{
    OtaMetaRecord_t in;
    OtaMetaRecord_t out;
    uint8_t buf[88];
    OtaMetaStatus_t st;

    (void)memset(&in, 0x00, sizeof(in));
    (void)strncpy(in.filename, "cardreader-v1.0.0.bin", sizeof(in.filename) - 1u);
    (void)strncpy(in.version, "v1.0.0", sizeof(in.version) - 1u);
    in.image_size = 1024u;
    in.fw_crc32 = 0x12345678u;
    in.image_ready = ota_meta_flag_erased;
    in.app_valid = ota_meta_flag_erased;
    in.mate_ota_fail = ota_meta_flag_erased;

    st = OtaMeta_EncodeRecord(&in, buf);
    ExpectStatus("encode_for_tamper", st, OTA_META_OK);

    buf[0x44] ^= 0xFFu;
    st = OtaMeta_DecodeRecord(buf, &out, 1);
    ExpectStatus("decode_crc_fail", st, OTA_META_ERR_CRC);
}

int main(void)
{
    (void)printf("=== Slice1 ota_meta_codec host tests ===\n");
    TestCrc32Vector();
    TestLayoutConstants();
    TestEncodeDecodeRoundtrip();
    TestSizeBounds();
    TestFilenameBounds();
    TestCrcTamper();

    if (s_failures != 0) {
        (void)printf("RESULT: FAIL (%d)\n", s_failures);
        return 1;
    }
    (void)printf("RESULT: PASS\n");
    return 0;
}
