/**
@file boot_bkp.c
@brief BKP1/BKP2 主机 RAM 桩与备份域开启桩（R02 §2.2）
@note 实机 BKP 寄存器未接线（非 E3）；BOARD_GT32_TARGET 仅留 TODO，不发明引脚。
*/

#include "boot_bkp.h"

static uint16_t s_bkp1;
static uint16_t s_bkp2;
#ifdef BOARD_GT32_HOST_MOCK
static int s_fail_set1_once;
static int s_fail_set2_once;
#endif

void BootBkp_EnableBackupDomain(void)
{
#ifdef BOARD_GT32_TARGET
    /* TODO: 开启 PMU/BKP 时钟与备份域写保护解除（无引脚号；实机未验证） */
#else
    /* 主机：无硬件；调用即视为已开启 */
#endif
}

int BootBkp_Set1(uint16_t value)
{
#ifdef BOARD_GT32_TARGET
    /* TODO: 写 BKP_DATA1 并回读（实机未验证） */
    (void)value;
    return 0;
#else
#ifdef BOARD_GT32_HOST_MOCK
    if (s_fail_set1_once != 0) {
        s_fail_set1_once = 0;
        return 0;
    }
#endif
    s_bkp1 = value;
    return (BootBkp_Get1() == value) ? 1 : 0;
#endif
}

uint16_t BootBkp_Get1(void)
{
#ifdef BOARD_GT32_TARGET
    /* TODO: 读 BKP_DATA1 */
    return 0u;
#else
    return s_bkp1;
#endif
}

int BootBkp_Set2(uint16_t value)
{
#ifdef BOARD_GT32_TARGET
    /* TODO: 写 BKP_DATA2 并回读（实机未验证） */
    (void)value;
    return 0;
#else
#ifdef BOARD_GT32_HOST_MOCK
    if (s_fail_set2_once != 0) {
        s_fail_set2_once = 0;
        return 0;
    }
#endif
    s_bkp2 = value;
    return (BootBkp_Get2() == value) ? 1 : 0;
#endif
}

uint16_t BootBkp_Get2(void)
{
#ifdef BOARD_GT32_TARGET
    /* TODO: 读 BKP_DATA2 */
    return 0u;
#else
    return s_bkp2;
#endif
}

int BootBkp_Set(uint16_t value)
{
    return BootBkp_Set2(value);
}

uint16_t BootBkp_Get(void)
{
    return BootBkp_Get2();
}

#ifdef BOARD_GT32_HOST_MOCK
void BootBkpMock_Reset(void)
{
    s_bkp1 = 0u;
    s_bkp2 = 0u;
    s_fail_set1_once = 0;
    s_fail_set2_once = 0;
}

void BootBkpMock_InjectFailSet1Once(void)
{
    s_fail_set1_once = 1;
}

void BootBkpMock_InjectFailSet2Once(void)
{
    s_fail_set2_once = 1;
}
#endif
