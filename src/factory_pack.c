/**
@file factory_pack.c
@brief 出厂 128 KiB 片内包打包与反向核对（复用 ota_meta_codec；无 YMODEM）
@note 实机未验证（非 E3）。不编造引脚；不嵌入 GT32；禁止用 YMODEM 发送本包。
*/

#include "factory_pack.h"

#include <string.h>

#include "ota_layout.h"
#include "ota_meta_codec.h"

#define FACTORY_PACK_PREFIX "cardreader-"
#define FACTORY_PACK_SUFFIX ".bin"

/**
@brief 小端读 uint32
@param p 至少 4 字节
@return 数值
*/
static uint32_t FactoryPack_ReadU32Le(const uint8_t *p)
{
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/**
@brief 校验 App 向量（与 ota_image 语义一致；主机工具本地实现，不链 GT32）
@param app App 镜像起始
@param image_size 镜像长度
@return 1 OK；0 失败
*/
static int FactoryPack_VectorsOk(const uint8_t *app, uint32_t image_size)
{
    uint32_t msp;
    uint32_t reset;
    uint32_t reset_addr;
    uint32_t image_end;

    if (app == NULL || image_size < 8u || image_size > ota_app_image_max) {
        return 0;
    }

    msp = FactoryPack_ReadU32Le(app);
    reset = FactoryPack_ReadU32Le(app + 4u);
    reset_addr = reset & ~0x01u;
    image_end = ota_flash_app_base + image_size;

    if (msp <= ota_ram_base || msp > ota_ram_end || (msp & 0x07u) != 0u) {
        return 0;
    }
    if ((reset & 0x01u) == 0u || reset_addr < ota_flash_app_base
        || reset_addr >= image_end) {
        return 0;
    }
    return 1;
}

/**
@brief 从合法文件名提取版本字段（含前导 v）
@param filename 已通过 ValidateFilename 的名称
@param version_out 输出缓冲，容量 ota_meta_len_version
@return FACTORY_PACK_OK 或 FACTORY_PACK_ERR_VERSION
*/
static FactoryPackStatus_t FactoryPack_ExtractVersion(const char *filename,
                                                      char *version_out)
{
    size_t len;
    size_t prefix_len = sizeof(FACTORY_PACK_PREFIX) - 1u;
    size_t suffix_len = sizeof(FACTORY_PACK_SUFFIX) - 1u;
    size_t ver_len;

    if (filename == NULL || version_out == NULL) {
        return FACTORY_PACK_ERR_NULL;
    }

    len = strlen(filename);
    if (len <= (prefix_len + suffix_len)
        || len > (ota_meta_len_filename - 1u)) {
        return FACTORY_PACK_ERR_VERSION;
    }

    if (memcmp(filename, FACTORY_PACK_PREFIX, prefix_len) != 0) {
        return FACTORY_PACK_ERR_VERSION;
    }
    if (memcmp(filename + len - suffix_len, FACTORY_PACK_SUFFIX, suffix_len)
        != 0) {
        return FACTORY_PACK_ERR_VERSION;
    }

    ver_len = len - prefix_len - suffix_len;
    if (ver_len == 0u || ver_len > (ota_meta_len_version - 1u)) {
        return FACTORY_PACK_ERR_VERSION;
    }

    (void)memset(version_out, 0, ota_meta_len_version);
    (void)memcpy(version_out, filename + prefix_len, ver_len);
    return FACTORY_PACK_OK;
}

/**
@brief 检查 [start, end) 区间是否全为 0xFF
@param pkg 包缓冲
@param start 起始偏移
@param end 结束偏移（不含）
@return 1 全 FF；0 否
*/
static int FactoryPack_RegionIsFf(const uint8_t *pkg, size_t start, size_t end)
{
    size_t i;

    if (end > factory_pack_size || start > end) {
        return 0;
    }
    for (i = start; i < end; i++) {
        if (pkg[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

/**
@brief 将 Meta 编解码错误映射为 FactoryPackStatus
@param st codec 状态
@return FactoryPack 错误码
*/
static FactoryPackStatus_t FactoryPack_MapMeta(OtaMetaStatus_t st)
{
    if (st == OTA_META_OK) {
        return FACTORY_PACK_OK;
    }
    if (st == OTA_META_ERR_STRING || st == OTA_META_ERR_NULL) {
        return FACTORY_PACK_ERR_FILENAME;
    }
    if (st == OTA_META_ERR_SIZE) {
        return FACTORY_PACK_ERR_APP_SIZE;
    }
    if (st == OTA_META_ERR_CRC || st == OTA_META_ERR_BOARD_ID) {
        return FACTORY_PACK_ERR_CRC;
    }
    return FACTORY_PACK_ERR_META;
}

FactoryPackStatus_t FactoryPack_Build(const uint8_t *boot,
                                      size_t boot_len,
                                      const uint8_t *app,
                                      size_t app_len,
                                      const char *app_filename,
                                      uint8_t *out_pkg,
                                      size_t out_cap)
{
    OtaMetaRecord_t rec;
    OtaMetaStatus_t mst;
    FactoryPackStatus_t fst;
    uint8_t meta_buf[ota_meta_record_size];

    if (boot == NULL || app == NULL || app_filename == NULL || out_pkg == NULL) {
        return FACTORY_PACK_ERR_NULL;
    }
    if (out_cap < factory_pack_size) {
        return FACTORY_PACK_ERR_BUF;
    }
    if (boot_len == 0u || boot_len > ota_flash_boot_size) {
        return FACTORY_PACK_ERR_BOOT;
    }
    if (app_len < ota_app_image_min || app_len > ota_app_image_max) {
        return FACTORY_PACK_ERR_APP_SIZE;
    }
    if (OtaMeta_ValidateFilename(app_filename) != OTA_META_OK) {
        return FACTORY_PACK_ERR_FILENAME;
    }
    if (FactoryPack_VectorsOk(app, (uint32_t)app_len) == 0) {
        return FACTORY_PACK_ERR_VECTOR;
    }

    (void)memset(&rec, 0, sizeof(rec));
    (void)memset(rec.filename, 0, sizeof(rec.filename));
    (void)strncpy(rec.filename, app_filename, sizeof(rec.filename) - 1u);

    fst = FactoryPack_ExtractVersion(app_filename, rec.version);
    if (fst != FACTORY_PACK_OK) {
        return fst;
    }

    rec.image_size = (uint32_t)app_len;
    rec.fw_crc32 = OtaCrc32_Compute(app, app_len);
    rec.image_ready = ota_meta_flag_set;
    rec.app_valid = ota_meta_flag_set;
    rec.mate_ota_fail = ota_meta_flag_erased;
    rec.board_id = ota_board_id_gt32;

    mst = OtaMeta_EncodeRecord(&rec, meta_buf);
    if (mst != OTA_META_OK) {
        return FactoryPack_MapMeta(mst);
    }

    (void)memset(out_pkg, 0xFF, factory_pack_size);
    (void)memcpy(out_pkg + factory_pack_off_boot, boot, boot_len);
    (void)memcpy(out_pkg + factory_pack_off_app, app, app_len);
    (void)memcpy(out_pkg + factory_pack_off_meta, meta_buf, ota_meta_record_size);

    return FACTORY_PACK_OK;
}

FactoryPackStatus_t FactoryPack_Verify(const uint8_t *pkg, size_t pkg_len)
{
    OtaMetaRecord_t rec;
    OtaMetaStatus_t mst;
    const uint8_t *app;
    const uint8_t *meta;
    uint32_t fw_crc;
    size_t i;
    size_t boot_used;

    if (pkg == NULL) {
        return FACTORY_PACK_ERR_NULL;
    }
    if (pkg_len != factory_pack_size) {
        return FACTORY_PACK_ERR_LAYOUT;
    }

    meta = pkg + factory_pack_off_meta;
    app = pkg + factory_pack_off_app;

    mst = OtaMeta_DecodeRecord(meta, &rec, 1);
    if (mst != OTA_META_OK) {
        return FactoryPack_MapMeta(mst);
    }

    if (rec.image_ready != ota_meta_flag_set
        || rec.app_valid != ota_meta_flag_set
        || rec.mate_ota_fail != ota_meta_flag_erased) {
        return FACTORY_PACK_ERR_FLAGS;
    }
    if (rec.board_id != ota_board_id_gt32) {
        return FACTORY_PACK_ERR_CRC;
    }
    if (rec.image_size < ota_app_image_min
        || rec.image_size > ota_app_image_max) {
        return FACTORY_PACK_ERR_APP_SIZE;
    }
    if (OtaMeta_ValidateFilename(rec.filename) != OTA_META_OK) {
        return FACTORY_PACK_ERR_FILENAME;
    }
    if (FactoryPack_VectorsOk(app, rec.image_size) == 0) {
        return FACTORY_PACK_ERR_VECTOR;
    }

    fw_crc = OtaCrc32_Compute(app, rec.image_size);
    if (fw_crc != rec.fw_crc32) {
        return FACTORY_PACK_ERR_CRC;
    }

    /* App 声明长度之后直至 Meta 前须为 0xFF */
    if (FactoryPack_RegionIsFf(pkg,
                               factory_pack_off_app + (size_t)rec.image_size,
                               factory_pack_off_meta) == 0) {
        return FACTORY_PACK_ERR_PADDING;
    }
    /* Meta 有效头之后到包尾须为 0xFF */
    if (FactoryPack_RegionIsFf(pkg,
                               factory_pack_off_meta + ota_meta_record_size,
                               factory_pack_size) == 0) {
        return FACTORY_PACK_ERR_PADDING;
    }

    /* Boot：找最后一个非 FF 字节，其后到 App 前须全 FF */
    boot_used = 0u;
    for (i = 0u; i < factory_pack_off_app; i++) {
        if (pkg[i] != 0xFFu) {
            boot_used = i + 1u;
        }
    }
    if (boot_used == 0u || boot_used > ota_flash_boot_size) {
        return FACTORY_PACK_ERR_BOOT;
    }
    if (FactoryPack_RegionIsFf(pkg, boot_used, factory_pack_off_app) == 0) {
        return FACTORY_PACK_ERR_PADDING;
    }

    return FACTORY_PACK_OK;
}
