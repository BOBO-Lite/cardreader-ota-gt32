/**
@file app_ota.c
@brief App↔Boot BKP 契约实现（R02 §5.1.3；主机复位桩；无 Modbus）
@note 实机未验证（非 E3）。不发明引脚；不含 Modbus FC/寄存器。
*/

#include "app_ota.h"

#include "boot_bkp.h"

#ifdef BOARD_GT32_HOST_MOCK
int AppOta_HostResetRequested;
static int s_system_reset_taken = 1;

int AppOta_SystemResetStub(void)
{
    if (s_system_reset_taken != 0) {
        AppOta_HostResetRequested = 1;
        return 1; /* did reset */
    }
    return 0; /* not taken */
}

void AppOtaMock_ClearHostResetRequested(void)
{
    AppOta_HostResetRequested = 0;
}

void AppOtaMock_SetSystemResetTaken(int taken)
{
    s_system_reset_taken = (taken != 0) ? 1 : 0;
}
#else
/**
@brief 目标板系统复位（桩：未接线）
@return 若指令后仍返回则视为 not taken（0）
*/
static int AppOta_SystemResetStub(void)
{
    /* TODO: NVIC/系统复位（实机未验证；无引脚号） */
    return 0;
}
#endif

int AppOta_RequestUpgrade(void)
{
    /* BKP2=0 并回读 */
    if (BootBkp_Set2(0u) == 0 || BootBkp_Get2() != 0u) {
        return -2;
    }
    /* BKP1=0x3344 并回读 */
    if (BootBkp_Set1(BOOT_BKP1_OTA_REQUEST) == 0 ||
        BootBkp_Get1() != BOOT_BKP1_OTA_REQUEST) {
        return -3;
    }
    /* 成功路径以复位为准：主机桩若 did reset 则返回 -1（调用方不应见 0） */
    if (AppOta_SystemResetStub() != 0) {
        return -1;
    }
    return 0; /* reset not taken */
}

int AppOta_ConfirmRunning(void)
{
    /* BKP1=0xAABB 并回读 */
    if (BootBkp_Set1(BOOT_BKP1_CONSUMED) == 0 ||
        BootBkp_Get1() != BOOT_BKP1_CONSUMED) {
        return -4;
    }
    /* BKP2=0 并回读 */
    if (BootBkp_Set2(0u) == 0 || BootBkp_Get2() != 0u) {
        return -5;
    }
    return 0;
}
