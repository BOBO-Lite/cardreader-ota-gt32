# CardReader GT32 OTA — Slice 1–9

片内/GT32 分区常数、Meta 编解码、`gt32_raw`、Boot/YMODEM、`CommitImageReady`、**Slice 6 安装**、**Slice 7：上电三旗标选路 / BKP1 消费 / BKP2 门限 5 / `LOAD_A`**，以及 **Slice 8：App↔Boot `AppOta_*` 契约（无 Modbus）**（对照 ref-v031 语义按 C 规范重写）。

行为权威：`YMODEM-GT32-D1-R02`（R02.1）。

语义参考源：`/workspace/ref-v031/code/USER/DRIVERS/`（**禁止原样粘贴**；本仓库为规范重写）。

**非 E3：** 主机 E1+E2；**实机 / 掉电未验证**。

## 板级 SPI 引脚（用户确认 2026-09-12）

| 信号 | GPIO | 说明 |
| --- | --- | --- |
| SCK / MISO / MOSI | PB13 / PB14 / PB15 | SPI2，与屏共用 |
| GT32 CS | PB9 | Boot/OTA 驱动 |
| 屏 CS | PB12 | 同总线；Boot **不**驱动 |

`BOARD_GT32_PINS_CONFIRMED=1`（不等于 E3 通过）。

## Backup → Secondary / LastGood 映射

| ref-v031 | 本仓库（R02 GT32） |
| --- | --- |
| 片内 Backup 落盘 / `OtaImage_VerifyBackup` | GT32 **Secondary**（`ota_gt32_secondary_off`）；`OtaImage_VerifySecondary` via `GT32_Read` |
| `OtaImage_CopyBackupToApp` | `OtaImage_CopySecondaryToApp`（先主体后向量） |
| （无） | **LastGood**（`ota_gt32_lastgood_off`）：安装前整槽 112640 App→LastGood；`GT32_EraseLastGoodSlot`（2×D8h @ `0x20000`/`0x30000`）+ `GT32_WriteLastGood` |

## Slice 7 — 上电选路 + BKP + LOAD_A（R02 §2.2）

| 项 | 状态 |
| --- | --- |
| 三旗标选路 | **已实现** `Boot_SelectPath` / `Boot_ColdBootDecide`；`Boot_MainOnce`（`src/main.c`） |
| BKP1/BKP2 | **已实现** `include/boot_bkp.h` + 主机 RAM 桩；BKP1=`0x3344`→消费`0xAABB`；BKP2 门限 5 |
| `LOAD_A` | **已实现** 主机交接清单位；`BOARD_GT32_TARGET` 仅 TODO（引脚已确认：SPI2 PB13/14/15，GT32 CS=PB9，屏 CS=PB12（Boot 不驱动）） |
| LastGood 自动恢复 | **v1 不做**（BKP2≥5 → OTA only） |
| Chip Erase / App 业务 | **禁止 / 未改** |
| Slice 9 出厂包 | **已实现**（见下） |
| 实机 | **未验证**（非 E3） |

### 路由表（只看片内三旗标；不依赖 GT32）

READY/VALID：仅完整 `0` 为置位。FAIL：仅 `0xFFFFFFFF` 为未置位。

| Condition | Next |
| --- | --- |
| READY unset | OTA |
| READY set, VALID unset, FAIL unset | INSTALL（`RunInstallStage`；不开 UART/C） |
| READY set, FAIL set | OTA |
| READY+VALID set, FAIL unset | 看 BKP1/BKP2 |
| VALID and FAIL both set | OTA |

待安装优先于 BKP/跳转。READY+VALID 且 FAIL 未置位之后：

1. BKP1==`0x3344` → 写 BKP1=`0xAABB`（消费）→ OTA  
2. 否则 BKP2≥5 → OTA（**无** LastGood 自动恢复）  
3. 否则 BKP2++ 回读 → JUMP → `LOAD_A(app_base)`

### 统一跳转交接清单（主机记录位）

| Bit | 含义 |
| --- | --- |
| `BOOT_HANDOFF_RS485_RELEASED` | RS485 方向释放 |
| `BOOT_HANDOFF_DMA_USART_OFF` | DMA/USART 关闭 |
| `BOOT_HANDOFF_SYSTICK_OFF` | SysTick 关闭 |
| `BOOT_HANDOFF_NVIC_CLEARED` | NVIC/SysTick/PendSV 挂起清除 |
| `BOOT_HANDOFF_VTOR_SET` | VTOR=`ota_flash_app_base` |
| `BOOT_HANDOFF_MSP_SWITCHED` | MSP 已切换 |
| `BOOT_HANDOFF_WDT_FED` | WDT 喂狗并保持 |

主机 E2：`tests/test_boot_select.c`（路由矩阵 + BKP1 消费 + BKP2≥5 + JUMP 清单 + VALID&FAIL→OTA + 无 LastGood 自动路径）。


## Slice 8 — AppOta API（R02 §5.1.3）

| 项 | 状态 |
| --- | --- |
| `AppOta_RequestUpgrade` | **已实现** BKP2=0、BKP1=`0x3344`，回读后复位桩；主机：复位已请求 → 返回 `-1`；仅 not taken → `0`；BKP 失败负值 |
| `AppOta_ConfirmRunning` | **已实现** BKP1=`0xAABB`、BKP2=0 回读；成功 `0` |
| 主机复位桩 | `AppOta_SystemResetStub` / `AppOta_HostResetRequested` |
| Modbus FC/寄存器 | **不做**（本模块不含；主机协议仅 ACK 后调 API） |
| 实机 | **未验证**（非 E3） |

主机 E2：`tests/test_app_ota.c` + `scripts/check_app_ota_no_modbus.py`。



## Slice 9 — 出厂 128 KiB 打包工具（AC-YM-19）

| 项 | 状态 |
| --- | --- |
| `FactoryPack_Build` / `Verify` | **已实现**（`include/factory_pack.h` / `src/factory_pack.c`） |
| CLI | `build/factory_pack pack|verify`（`tools/factory_pack_main.c`） |
| Meta 出厂旗标 | READY=`0`，VALID=`0`，FAIL=`0xFFFFFFFF`，`board_id=0x0001` |
| GT32 | **不嵌入**片内包；出厂可空白 / 产线另写 |
| YMODEM 下发工厂整包 | **禁止**（`scripts/check_factory_no_ymodem.py`） |
| 证据 | 主机 E1+E2（`tests/test_factory_pack.c`）；**实机未验证（非 E3）** |
| 产线烧录回读 | **须另授权**后才标 E3 |
| AC-YM-15/20/22 E4 | **未宣称完成** |

### 包布局（相对包首 = MCU 相对 `0x08000000`）

```text
0x00000  Boot（≤16 KiB；余下 0xFF）
0x04000  App（8～112640；槽内余下 0xFF）
0x1F800  Meta 88B（字符串 NUL；保留区 0xFF）
0x20000  结束（总长 131072）
```

无片内 Secondary 副本。建议输出名 `factory-cardreader-v….bin`。

## Slice 6 — 安装（R02.1 §3.5）

| 项 | 状态 |
| --- | --- |
| `ota_image` | **已实现** VerifySecondary/VerifyApp/CheckAppVectors/BackupAppToLastGood/CopySecondaryToApp |
| `Boot_InstallOnce` / `Boot_RunInstallStage` | **已实现**；SESSION_DONE→CommitImageReady 成功后进入安装 |
| LastGood 规则 | 向量 OK → 必须整槽备份成功，否则本轮失败、**不擦 App**；向量 NOT OK → 跳过 LastGood |
| BKP | `BootBkp_Set/Get`（BKP2）；清 0 早于 VALID；成功后统一跳转经 `LOAD_A` |
| Chip Erase | **禁止** |

## Slice 1 — 布局与 Meta

| 区 | 地址/偏移 | 容量 |
| --- | --- | --- |
| Boot | `0x08000000`–`0x08003FFF` | 16 KiB |
| App | `0x08004000`–`0x0801F7FF` | 110 KiB（`APP_IMAGE_MAX=112640`） |
| Meta | `0x0801F800`–`0x0801FFFF` | 2 KiB（事务只擦第一页 1 KiB） |
| Secondary | GT32 `0x00000` | 128 KiB |
| LastGood | GT32 `0x20000` | 128 KiB |
| SRAM | `0x20000000`–`0x20005000`（MSP 上界） | 20 KiB |

`board_id = 0x0001`。Meta CRC 覆盖 `0x00..0x43` + `0x54..0x57`（72 字节）。

## Slice 2–5（摘要）

`gt32_raw` 白名单：`03h`/`06h`/`02h`/`20h`/`D8h`/`05h`。**禁止** CE `60h`/`C7h`。  
Boot OTA 相位机 + `CommitImageReady`；传输路径不写 Meta。

引脚：`BOARD_GT32_PINS_CONFIRMED 0` → **实机尚未验证**。

## 构建 / 测试

```bash
make -C /workspace/cardreader-ota-gt32 clean test
```

## R02 偏差

- **无功能偏差**（预期选路表与 v1 无 LastGood 自动恢复一致）。  
- 主机：BKP/跳转/复位/`LOAD_A` 为桩（交接清单位可测）；实机 PMU/BKP 寄存器、RS485/DMA/USART/SysTick/VTOR/MSP：**未接线 / 未验证**（`BOARD_GT32_TARGET` TODO，无引脚号）。  
- OTA 分支的 `All_Init`/发 C 由既有 `Boot_OtaStartAttempt` 承担；`Boot_MainOnce` 的 OTA 出口仅返回路径枚举（主机测选路，不替代完整 Boot 服务循环）。  
- 实机 FMC/GT32/掉电：**未验证**（非 E3/E4）。  
- AppOta：主机复位为桩；实机 NVIC/BKP：**未验证**（非 E3）。**无** Modbus FC/寄存器。
- FactoryPack：主机自检；**未板端烧录回读**（非 E3）。禁 YMODEM 发工厂包；GT32 不在片内包。
- **未声称 E3**；**未宣称** AC-YM-15/20/22 的 E4 完成。

## 文件

- `include/boot_bkp.h` / `src/boot_bkp.c` — BKP1/BKP2 契约与主机 RAM 桩  
- `src/main.c` — `Boot_MainOnce`（开备份域 → SelectPath → 分支）  
- `include/boot.h` / `src/boot.c` — 相位机 + Install + SelectPath + `LOAD_A`  
- `tests/test_boot_select.c` — Slice 7 主机 E2  
- `include/app_ota.h` / `src/app_ota.c` — Slice 8 App↔Boot 契约（无 Modbus）  
- `tests/test_app_ota.c` / `scripts/check_app_ota_no_modbus.py` — Slice 8 主机 E2 + 无 Modbus 证明  
- `include/factory_pack.h` / `src/factory_pack.c` / `tools/factory_pack_main.c` — Slice 9 出厂包  
- `tests/test_factory_pack.c` / `scripts/check_factory_no_ymodem.py` — Slice 9 主机 E2 + 禁 YMODEM  
- 既有：`ota_*` / `gt32_*` / `fmc` / `test_ota_*` / `test_boot_ota` / `test_ota_install`
