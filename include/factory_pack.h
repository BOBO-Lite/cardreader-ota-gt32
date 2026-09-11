/**
@file factory_pack.h
@brief 出厂片内 128 KiB 整包（Boot+App+Meta）打包与反向核对（R02 §4 / AC-YM-19）
@note 禁止用 YMODEM 发送本包；GT32 内容不嵌入片内包；产线烧录回读须另授权才标 E3。
      实机尚未验证（非 E3）。
*/

#ifndef CARDREADER_FACTORY_PACK_H
#define CARDREADER_FACTORY_PACK_H

#include <stddef.h>
#include <stdint.h>

/** 片内工厂包固定总长（字节）= 128 KiB */
#define factory_pack_size             (131072u)

/** 包内 Boot 偏移（相对包首 = MCU 相对 0x08000000） */
#define factory_pack_off_boot         (0x00000u)

/** 包内 App 偏移 */
#define factory_pack_off_app          (0x04000u)

/** 包内 Meta 有效头偏移 */
#define factory_pack_off_meta         (0x1F800u)

/**
@brief 出厂打包/核对结果：0 成功，负值失败
*/
typedef enum {
    FACTORY_PACK_OK = 0,
    FACTORY_PACK_ERR_NULL = -1,
    FACTORY_PACK_ERR_BOOT = -2,
    FACTORY_PACK_ERR_APP_SIZE = -3,
    FACTORY_PACK_ERR_FILENAME = -4,
    FACTORY_PACK_ERR_VECTOR = -5,
    FACTORY_PACK_ERR_META = -6,
    FACTORY_PACK_ERR_BUF = -7,
    FACTORY_PACK_ERR_LAYOUT = -8,
    FACTORY_PACK_ERR_CRC = -9,
    FACTORY_PACK_ERR_FLAGS = -10,
    FACTORY_PACK_ERR_PADDING = -11,
    FACTORY_PACK_ERR_VERSION = -12
} FactoryPackStatus_t;

/**
@brief 将 Boot+App 打成固定 131072 字节片内工厂包
@param boot Boot BIN 缓冲；不可为 NULL
@param boot_len Boot 长度，范围 (0, 16384]
@param app App BIN 缓冲；不可为 NULL
@param app_len App 长度，范围 [8, 112640]
@param app_filename App 原始文件名（须通过 OtaMeta_ValidateFilename）；Meta 存此名
@param out_pkg 输出缓冲；不可为 NULL
@param out_cap 输出容量；须 ≥ factory_pack_size
@return FACTORY_PACK_OK 或负错误码
@note 初值全 0xFF；Meta：READY/VALID=0，FAIL=0xFFFFFFFF，board_id=0x0001，Meta CRC 由 codec 计算。
      本接口不发送、不烧录、不经 YMODEM。
*/
FactoryPackStatus_t FactoryPack_Build(const uint8_t *boot,
                                      size_t boot_len,
                                      const uint8_t *app,
                                      size_t app_len,
                                      const char *app_filename,
                                      uint8_t *out_pkg,
                                      size_t out_cap);

/**
@brief 反向解析核对工厂包：长度、偏移、填充、向量、字符串、两 CRC、board_id、三旗标
@param pkg 包缓冲；不可为 NULL
@param pkg_len 须等于 factory_pack_size
@return FACTORY_PACK_OK 或负错误码
@note 不要求还原精确 Boot 尾长；Boot 区只检查「非 FF 后缀之后至 App 前」为 FF 的连续填充。
*/
FactoryPackStatus_t FactoryPack_Verify(const uint8_t *pkg, size_t pkg_len);

#endif /* CARDREADER_FACTORY_PACK_H */
