/**
@file main.c
@brief 上电入口桩：备份域 → SelectPath → INSTALL / JUMP / OTA（R02 §2.2）
@note 主机以 Boot_MainOnce 供测试调用；不提供与测试冲突的 main()。
      实机 All_Init / UART 开关于 OTA 分支留后续；INSTALL 不开 UART/C。
*/

#include "boot.h"

#include "ota_layout.h"
#include "ota_meta.h"

/**
@brief 上电一次选路并分支
@return BootPath_t 数值；JUMP 经 LOAD_A 成功（主机记清单）仍返回 JUMP；
        LOAD_A 不够格返回则改为 OTA
*/
int Boot_MainOnce(void)
{
    BootPath_t path;

    BootBkp_EnableBackupDomain();

    path = Boot_SelectPath();

    switch (path) {
    case BOOT_PATH_INSTALL:
        /* 不开 UART、不发 C（R02 §2.2 / §3.5） */
        Boot_RunInstallStage();
        /* 安装成功会 LOAD_A；FAIL 提交失败则留 OTA 由上层处理 */
        if (OtaMeta_IsAppValid() != 0) {
#ifdef BOARD_GT32_HOST_MOCK
            if (BootMock_JumpToAppFlag() != 0) {
                return (int)BOOT_PATH_JUMP;
            }
#endif
            return (int)BOOT_PATH_JUMP;
        }
        return (int)BOOT_PATH_OTA;

    case BOOT_PATH_JUMP:
        LOAD_A(ota_flash_app_base);
#ifdef BOARD_GT32_HOST_MOCK
        if (BootMock_JumpToAppFlag() != 0
            && (BootMock_HandoffChecklist() & BOOT_HANDOFF_ALL) == BOOT_HANDOFF_ALL) {
            return (int)BOOT_PATH_JUMP;
        }
#endif
        /* 不够格或交接未完成 → OTA */
        return (int)BOOT_PATH_OTA;

    case BOOT_PATH_RESET:
        /* 桩：保留枚举；主机可观测复位请求 */
        return (int)BOOT_PATH_RESET;

    case BOOT_PATH_OTA:
    default:
        return (int)BOOT_PATH_OTA;
    }
}
