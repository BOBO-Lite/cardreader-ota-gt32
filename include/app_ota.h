/**
@file app_ota.h
@brief App↔Boot 契约（R02 §5.1.3）：写 BKP1/BKP2 请求升级 / 确认运行
@note 主机协议（含日后 Modbus）仅在 ACK 主机后调用 AppOta_RequestUpgrade；
      **本模块不含任何 Modbus FC/寄存器**。Boot 侧仍只读 BKP1/BKP2。
      实机系统复位 / BKP 寄存器未接线（非 E3）。
*/

#ifndef CARDREADER_APP_OTA_H
#define CARDREADER_APP_OTA_H

/**
@brief 请求进入 Boot OTA：BKP2=0、BKP1=0x3344，回读校验后系统复位
@return 0 仅当复位未发生（reset not taken）；复位已请求/已发生时返回非 0（主机桩返回 -1）；
        BKP 回读失败返回负值。成功路径以复位为准，调用方通常不可见返回。
*/
int AppOta_RequestUpgrade(void);

/**
@brief 关键初始化成功后确认 App 在跑：BKP1=0xAABB、BKP2=0 并回读
@return 0 成功；负值失败。不得刚进 main 或刚喂狗就调用。
*/
int AppOta_ConfirmRunning(void);

#ifdef BOARD_GT32_HOST_MOCK
/**
@brief 主机：RequestUpgrade 是否已请求系统复位（由 SystemResetStub 置位）
*/
extern int AppOta_HostResetRequested;

/**
@brief 主机复位桩：置 AppOta_HostResetRequested 并返回“已复位”
@return 非 0 表示复位已发生/已请求（did reset）；0 表示未执行（not taken）
*/
int AppOta_SystemResetStub(void);

/**
@brief 主机：清除复位请求标志
*/
void AppOtaMock_ClearHostResetRequested(void);

/**
@brief 主机：控制复位桩是否“执行”复位（测 not-taken 路径）
@param taken 非 0：桩返回 did reset；0：桩返回 not taken
*/
void AppOtaMock_SetSystemResetTaken(int taken);
#endif

#endif /* CARDREADER_APP_OTA_H */
