/**
@file test_app_ota.c
@brief Slice 8 主机 E2：AppOta_RequestUpgrade / ConfirmRunning（R02 §5.1.3）
@note 无 Modbus；实机未验证（非 E3）。
*/

#include <stdio.h>

#include "app_ota.h"
#include "boot_bkp.h"

static int g_failures;

static void Expect(int cond, const char *msg)
{
    if (!cond) {
        (void)printf("FAIL: %s\n", msg);
        g_failures++;
    } else {
        (void)printf("PASS: %s\n", msg);
    }
}

static void ResetHost(void)
{
    BootBkpMock_Reset();
    AppOtaMock_ClearHostResetRequested();
    AppOtaMock_SetSystemResetTaken(1);
}

static void TestRequestUpgradeOk(void)
{
    int rc;

    ResetHost();
    (void)BootBkp_Set2(3u);
    (void)BootBkp_Set1(0x1111u);

    rc = AppOta_RequestUpgrade();
    Expect(rc == -1, "RequestUpgrade: reset taken → return -1 (not 0)");
    Expect(AppOta_HostResetRequested != 0, "RequestUpgrade: HostResetRequested set");
    Expect(BootBkp_Get2() == 0u, "RequestUpgrade: BKP2=0");
    Expect(BootBkp_Get1() == BOOT_BKP1_OTA_REQUEST, "RequestUpgrade: BKP1=0x3344");
}

static void TestRequestUpgradeResetNotTaken(void)
{
    int rc;

    ResetHost();
    AppOtaMock_SetSystemResetTaken(0);
    rc = AppOta_RequestUpgrade();
    Expect(rc == 0, "RequestUpgrade: reset not taken → return 0");
    Expect(AppOta_HostResetRequested == 0, "RequestUpgrade: flag clear when not taken");
    Expect(BootBkp_Get2() == 0u, "RequestUpgrade not-taken: BKP2=0");
    Expect(BootBkp_Get1() == BOOT_BKP1_OTA_REQUEST, "RequestUpgrade not-taken: BKP1=0x3344");
}

static void TestRequestUpgradeFailBkp2(void)
{
    int rc;

    ResetHost();
    BootBkpMock_InjectFailSet2Once();
    rc = AppOta_RequestUpgrade();
    Expect(rc == -2, "RequestUpgrade: BKP2 verify fail → -2");
    Expect(AppOta_HostResetRequested == 0, "RequestUpgrade fail BKP2: no reset");
}

static void TestRequestUpgradeFailBkp1(void)
{
    int rc;

    ResetHost();
    BootBkpMock_InjectFailSet1Once();
    rc = AppOta_RequestUpgrade();
    Expect(rc == -3, "RequestUpgrade: BKP1 verify fail → -3");
    Expect(AppOta_HostResetRequested == 0, "RequestUpgrade fail BKP1: no reset");
    Expect(BootBkp_Get2() == 0u, "RequestUpgrade fail BKP1: BKP2 already 0");
}

static void TestConfirmRunningOk(void)
{
    int rc;

    ResetHost();
    (void)BootBkp_Set1(BOOT_BKP1_OTA_REQUEST);
    (void)BootBkp_Set2(4u);

    rc = AppOta_ConfirmRunning();
    Expect(rc == 0, "ConfirmRunning: OK → 0");
    Expect(BootBkp_Get1() == BOOT_BKP1_CONSUMED, "ConfirmRunning: BKP1=0xAABB");
    Expect(BootBkp_Get2() == 0u, "ConfirmRunning: BKP2=0");
    Expect(AppOta_HostResetRequested == 0, "ConfirmRunning: no reset");
}

static void TestConfirmRunningFailBkp1(void)
{
    int rc;

    ResetHost();
    BootBkpMock_InjectFailSet1Once();
    rc = AppOta_ConfirmRunning();
    Expect(rc == -4, "ConfirmRunning: BKP1 fail → -4");
}

static void TestConfirmRunningFailBkp2(void)
{
    int rc;

    ResetHost();
    BootBkpMock_InjectFailSet2Once();
    rc = AppOta_ConfirmRunning();
    Expect(rc == -5, "ConfirmRunning: BKP2 fail → -5");
    Expect(BootBkp_Get1() == BOOT_BKP1_CONSUMED, "ConfirmRunning fail BKP2: BKP1 already AABB");
}

int main(void)
{
    g_failures = 0;
    TestRequestUpgradeOk();
    TestRequestUpgradeResetNotTaken();
    TestRequestUpgradeFailBkp2();
    TestRequestUpgradeFailBkp1();
    TestConfirmRunningOk();
    TestConfirmRunningFailBkp1();
    TestConfirmRunningFailBkp2();

    if (g_failures != 0) {
        (void)printf("RESULT: %d failure(s)\n", g_failures);
        return 1;
    }
    (void)printf("RESULT: all AppOta Slice 8 host tests PASS\n");
    return 0;
}
