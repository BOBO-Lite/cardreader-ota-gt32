/**
@file ota_layout.h
@brief CardReader GT32 OTA 片内/外挂分区与 Meta 字段偏移常数（R02.1）
*/

#ifndef CARDREADER_OTA_LAYOUT_H
#define CARDREADER_OTA_LAYOUT_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* 片内 Flash（GD32F103CB，128 KiB）                                         */
/* ------------------------------------------------------------------------- */

/** Boot 起始地址 */
#define ota_flash_boot_base           (0x08000000u)
/** Boot 结束地址（含） */
#define ota_flash_boot_end            (0x08003FFFu)
/** Boot 容量（字节） */
#define ota_flash_boot_size           (16384u)

/** App 起始地址 */
#define ota_flash_app_base            (0x08004000u)
/** App 结束地址（含） */
#define ota_flash_app_end             (0x0801F7FFu)
/** App 槽容量 / 镜像最大长度（字节）= 110 KiB */
#define ota_app_image_max             (112640u)

/** Meta/DATA 物理基址 */
#define ota_flash_meta_base           (0x0801F800u)
/** Meta/DATA 物理结束地址（含） */
#define ota_flash_meta_end            (0x0801FFFFu)
/** Meta 物理区容量（字节）= 2 KiB */
#define ota_flash_meta_size           (2048u)

/** Meta 整页事务擦写页起始（第一页） */
#define ota_flash_meta_page_base      (0x0801F800u)
/** Meta 整页事务擦写页结束（含） */
#define ota_flash_meta_page_end       (0x0801FBFFu)
/** Meta 整页事务页大小（字节）= 1 KiB */
#define ota_flash_meta_page_size      (1024u)

/** Meta 第二页预留起始（v1 不擦写） */
#define ota_flash_meta_reserve_base   (0x0801FC00u)

/* ------------------------------------------------------------------------- */
/* SRAM（GD32F103CB 20 KiB；向量 MSP 判定）                                    */
/* ------------------------------------------------------------------------- */

/** SRAM 起始 */
#define ota_ram_base                  (0x20000000u)
/** SRAM 一端（可作 MSP 上界，含 one-past） */
#define ota_ram_end                   (0x20005000u)

/* ------------------------------------------------------------------------- */
/* GT32L32S0140 用户区偏移（相对外挂 0x000000）                               */
/* ------------------------------------------------------------------------- */

/** Secondary（YMODEM 落盘）偏移 */
#define ota_gt32_secondary_off        (0x00000u)
/** Secondary 容量（字节）= 128 KiB */
#define ota_gt32_secondary_size       (131072u)

/** LastGood（安装前 App 备份）偏移 */
#define ota_gt32_lastgood_off         (0x20000u)
/** LastGood 容量（字节）= 128 KiB */
#define ota_gt32_lastgood_size        (131072u)

/** 配置/资源区起始偏移（v1 不做外挂 OTA meta） */
#define ota_gt32_config_off           (0x40000u)

/** GT32 用户区起始偏移 */
#define ota_gt32_user_off             (0x00000u)
/** GT32 用户区结束地址（含） */
#define ota_gt32_user_end             (0x7FFFFu)
/** GT32 用户区容量（字节）= 512 KiB */
#define ota_gt32_user_size            (524288u)

/* ------------------------------------------------------------------------- */
/* Meta 有效逻辑记录（88 字节）字段偏移（相对 Meta 基址）                     */
/* ------------------------------------------------------------------------- */

#define ota_meta_off_filename         (0x00u)
#define ota_meta_len_filename         (32u)

#define ota_meta_off_version          (0x20u)
#define ota_meta_len_version          (16u)

#define ota_meta_off_size_ascii       (0x30u)
#define ota_meta_len_size_ascii       (16u)

#define ota_meta_off_fw_crc32         (0x40u)
#define ota_meta_len_fw_crc32         (4u)

#define ota_meta_off_meta_crc32       (0x44u)
#define ota_meta_len_meta_crc32       (4u)

#define ota_meta_off_image_ready      (0x48u)
#define ota_meta_len_image_ready      (4u)

#define ota_meta_off_app_valid        (0x4Cu)
#define ota_meta_len_app_valid        (4u)

#define ota_meta_off_mate_ota_fail    (0x50u)
#define ota_meta_len_mate_ota_fail    (4u)

#define ota_meta_off_board_id         (0x54u)
#define ota_meta_len_board_id         (4u)

/** 有效逻辑头总长度（含 board_id） */
#define ota_meta_record_size          (88u)

/** Meta CRC 覆盖：0x00..0x43（68）+ 0x54..0x57（4）= 72 字节 */
#define ota_meta_crc_span_a_off       (0x00u)
#define ota_meta_crc_span_a_len       (68u)
#define ota_meta_crc_span_b_off       (0x54u)
#define ota_meta_crc_span_b_len       (4u)
#define ota_meta_crc_cover_len        (72u)

/** 新板 board_id 常量（小端 uint32） */
#define ota_board_id_gt32             (0x0001u)

/** 镜像声明大小合法下界（字节） */
#define ota_app_image_min             (8u)

/** 擦后/未置位 FAIL 的字面值；READY/VALID 置位值为 0 */
#define ota_meta_flag_erased          (0xFFFFFFFFu)
#define ota_meta_flag_set             (0x00000000u)

#endif /* CARDREADER_OTA_LAYOUT_H */
