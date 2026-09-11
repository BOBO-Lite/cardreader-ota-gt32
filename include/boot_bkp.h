/**
@file boot_bkp.h
@brief Boot BKP1/BKP2 契约（R02 §2.2；主机 RAM 桩；实机寄存器未接线）
@note 语义参考 ref-v031 BKP_DATA1/BKP_DATA2；按 C 规范重写，非原样粘贴。
      不使用 BKP3。BKP2 为 16 位裸启动计数，不编码魔数。
*/

#ifndef CARDREADER_BOOT_BKP_H
#define CARDREADER_BOOT_BKP_H

#include <stdint.h>

/** App 请求进 OTA：BKP1 置位值（一次消费） */
#define BOOT_BKP1_OTA_REQUEST (0x3344u)
/** Boot 消费后的 BKP1 值 */
#define BOOT_BKP1_CONSUMED (0xAABBu)

/**
@brief App 连续未确认启动次数门限；BKP2 >= 此值拒绝跳转（含 0xFFFF）
*/
#define BOOT_APP_LAUNCH_LIMIT (5u)

/**
@brief 开启备份域写访问（主机桩；目标板 TODO 无引脚号）
@note 上电选路前调用；不触碰 App Flash。
*/
void BootBkp_EnableBackupDomain(void);

/**
@brief 写 BKP1（16 位裸值）并回读
@param value 目标值
@return 1 回读匹配；0 失败
*/
int BootBkp_Set1(uint16_t value);

/**
@brief 读 BKP1
@return 当前 16 位值
*/
uint16_t BootBkp_Get1(void);

/**
@brief 写 BKP2（启动计数）并回读
@param value 目标值
@return 1 回读匹配；0 失败
*/
int BootBkp_Set2(uint16_t value);

/**
@brief 读 BKP2
@return 当前启动计数值
*/
uint16_t BootBkp_Get2(void);

/**
@brief Slice 6 兼容：写 BKP2（等同 BootBkp_Set2）
@param value 裸数值
@return 1 回读匹配；0 失败
*/
int BootBkp_Set(uint16_t value);

/**
@brief Slice 6 兼容：读 BKP2（等同 BootBkp_Get2）
@return 当前值
*/
uint16_t BootBkp_Get(void);

#ifdef BOARD_GT32_HOST_MOCK
/**
@brief 主机：复位 BKP1/BKP2 RAM 桩为 0
*/
void BootBkpMock_Reset(void);

/**
@brief 主机：下一次 BootBkp_Set1 强制失败（一次）
*/
void BootBkpMock_InjectFailSet1Once(void);

/**
@brief 主机：下一次 BootBkp_Set2 强制失败（一次）
*/
void BootBkpMock_InjectFailSet2Once(void);
#endif

#endif /* CARDREADER_BOOT_BKP_H */
