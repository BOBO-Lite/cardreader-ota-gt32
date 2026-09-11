# 实现计划 — 行为切片（A3/A2）

> 切片单位是**可独立说明、独立验证、独立回退的行为**，不是文件堆砌。  
> 行为权威：[`../YMODEM-GT32-D1-R02.md`](../YMODEM-GT32-D1-R02.md)（含 **R02.1 补丁**）。  
> 设计冻结：[DESIGN_FREEZE.md](./DESIGN_FREEZE.md)。每片结束输出【交付卡】并停止（除非用户明确允许连续多片）。

**实现策略（最优）：** 优先**移植** `CardReaderBootLoader-v0.3.1` / R10 `boot.c` 状态机到 GT32 分区与 `gt32_raw` 布局，而非协议绿场重写。仓库/工程路径仍 **待工程确认**。

通用约束（各片默认「不做」）：

- 不编造 SPI 引脚 / Keil 路径  
- 不对 GT32 发 Chip Erase  
- 不静默改 R02 时序、失败出口、三旗标语义  
- 不夹带无关重构或发送器/旧板改动  
- **不做** Modbus FC/寄存器映射（Slice 8 仅为 AppOta C API）  

---

## Slice 1 — `ota_layout.h` + Meta 编解码 / `board_id`（无 YMODEM）

| 项 | 内容 |
| --- | --- |
| 目标 | 固化片内/GT32 分区常数；实现 Meta 88 字节有效头编解码（含 `board_id=0x0001`）与 Meta CRC（间断 72 字节覆盖）；主机可测 |
| 级别 | A3（NVM 布局/CRC） |
| 主要文件 | `ota_layout.h`；`ota_meta_codec.c/.h`（及必要主机测试） |
| 依赖 | 无（可先于 SPI/YMODEM） |
| AC | AC-YM-01（格式/`board_id`/大小界）；AC-YM-03（布局与 CRC） |
| 所需 E | **E1**（编译/静态）；编解码单测 **E2** |
| 停止/检查点 | 交付卡：布局常数表、CRC 样例向量、主机测试结果；**不**写片内 Flash |
| 不做 | YMODEM；GT32 访问；Meta 页擦写事务；安装 |

---

## Slice 2 — `gt32_raw` probe / 读 / 写 / 擦

| 项 | 内容 |
| --- | --- |
| 目标 | Boot 可链接的薄 SPI 驱动：RDSR、Read、Page Program、Sector/Block Erase；probe；明确错误码；**禁止 CE API** |
| 级别 | A3（硬件/SPI） |
| 主要文件 | `gt32_raw.c/.h`；`board_gt32.h`（引脚占位或已确认引脚） |
| 依赖 | Slice 1 的偏移常数（Secondary/LastGood） |
| AC | AC-YM-05（命令集/禁 CE 相关部分）；AC-YM-22（probe 失败语义准备） |
| 所需 E | 先 **E1**（编译/链接进 Boot 骨架）；引脚确认后 **E3** 实板读写擦 |
| 停止/检查点 | 交付卡：命令白名单、禁 CE 断言/无符号、时钟≤9 MHz；引脚未确认则标「实机未验证」 |
| 不做 | Secondary 业务预擦封装可放本片或 Slice 3；不写 YMODEM；不 Chip Erase；不 invent 引脚 |

---

## Slice 3 — Secondary 预擦 + 写路径单元

| 项 | 内容 |
| --- | --- |
| 目标 | `GT32_EraseSecondarySlot()` 整槽 128 KiB 预擦（`20h`/`D8h`、段尾喂狗）；页写路径；不越界；声明长度外不写 |
| 级别 | A3 |
| 主要文件 | `gt32_raw` 扩展或 `ota_image` 写路径；与喂狗桩对接 |
| 依赖 | Slice 2 |
| AC | AC-YM-02；AC-YM-05 |
| 所需 E | 逻辑/边界 **E2**；实板擦写与时长登记 **E3** |
| 停止/检查点 | 交付卡含实板擦除时长（若已测）或「待 AC-YM-05 登记」；确认无 CE |
| 不做 | YMODEM 状态机；Meta 提交；安装搬运 |

---

## Slice 4 — YMODEM 接收写入 Secondary（移植 R10 / v0.3.1 `boot.c` 状态机）

| 项 | 内容 |
| --- | --- |
| 目标 | **移植** R10/`CardReaderBootLoader-v0.3.1` `boot.c` 状态机到 R02：DMA **static 2048**、Boot 栈≥`0x800`、ISR 仅冻结标志、Reply/SendPair、文件头校验、预擦接入、逐包写 Secondary、双 EOT/收尾、传输失败出口；写目标为 GT32 Secondary |
| 级别 | A3 |
| 主要文件 | `boot.c/.h`；`drv_ymodem.*`；`drv_usart.*`；与 Slice 3 预擦衔接 |
| 依赖 | Slice 1（文件名/大小界）、Slice 3（预擦/写） |
| AC | AC-YM-01；AC-YM-06；AC-YM-07；AC-YM-08；AC-YM-11；AC-YM-12；AC-YM-09（传输侧） |
| 所需 E | 协议回放/主机 **E2** → 实板 RS485 **E3** |
| 停止/检查点 | 交付卡：阶段表对照 R02 §3.3；明确「传输中不写 Meta」；实机未测项单列 |
| 不做 | Meta `IMAGE_READY` 提交；安装；AppOta（Slice 8）；绿场重写协议 |

---

## Slice 5 — Meta `IMAGE_READY` 整页事务提交

| 项 | 内容 |
| --- | --- |
| 目标 | 会话关闭后由 RAM 构造记录；`OtaMeta_CommitImageReady()`：只擦 Meta 第一页 1 KiB；编程信息区+CRC+`board_id`；写 READY=0 并回读；最多 3 次；脏页不续写；失败留 Boot 发 C、不装 App |
| 级别 | A3 |
| 主要文件 | `ota_meta.c/.h`；`fmc.*`；`boot.c` 在 `SESSION_DONE` 后调用 |
| 依赖 | Slice 1（codec）；Slice 4（会话关闭时机） |
| AC | AC-YM-03；AC-YM-04；AC-YM-14（READY 相关）；掉电点归 AC-YM-15 |
| 所需 E | 主机/逻辑 **E2**；板端擦写 **E3**；掉电注入 **E4**（可与总测合并，本片至少列出计划） |
| 停止/检查点 | 交付卡：事务步骤与半写策略；证明传输路径仍不写 Meta |
| 不做 | `APP_VALID`/`MATE_OTA_FAIL` 安装路径可骨架对接但完整安装在 Slice 6；不擦 App |

---

## Slice 6 — 安装：LastGood 备份 + Secondary→App + VALID

| 项 | 内容 |
| --- | --- |
| 目标 | `Boot_InstallOnce()`：复核 Secondary → 向量 OK 则整槽 112640 字节备份到 LastGood（必须成功，否则本轮失败、不擦 App；向量 NOT OK 跳过；**不**要求 Meta APP_VALID）→ 擦 App → Secondary→App（先主体后向量）→ 校验 → BKP2=0 回读 → `APP_VALID`；安装计数×3 与 FAIL 提交 |
| 级别 | A3 |
| 主要文件 | `ota_image.c/.h`；`boot.c` 安装阶段；`ota_meta` VALID/FAIL |
| 依赖 | Slice 2–5 |
| AC | AC-YM-14；AC-YM-18；AC-YM-21；AC-YM-22（安装阶段介质失败）；AC-YM-15（备份/搬运掉电） |
| 所需 E | **E3** 实板安装；关键掉电 **E4** |
| 停止/检查点 | 交付卡：备份强制成功证据；证明 READY 前/备份失败路径不擦 App |
| 不做 | v1 LastGood 自动恢复；上电选路完整联调可留 Slice 7 收口 |

---

## Slice 7 — Boot 选路 + BKP1/BKP2 统一跳转

| 项 | 内容 |
| --- | --- |
| 目标 | 上电只读三旗标选路；BKP1 消费；BKP2 门限 5（v1 无自动恢复）；统一跳转交接（释放 RS485、关 DMA/串口/计时、清挂起、VTOR/栈、WDT 保持） |
| 级别 | A3 |
| 主要文件 | `main.c`；`boot.c` 跳转路径 |
| 依赖 | Slice 5–6（旗标与安装结果语义） |
| AC | AC-YM-13；AC-YM-16；AC-YM-17；AC-YM-18 |
| 所需 E | **E3** |
| 停止/检查点 | 交付卡：选路表用例；跳转前后外设/中断状态检查清单 |
| 不做 | App 内业务；自动 LastGood 恢复 |

---

## Slice 8 — AppOta API + ConfirmRunning（BKP/复位；不做 Modbus FC）

| 项 | 内容 |
| --- | --- |
| 目标 | 提供 App↔Boot 契约：`app_ota.h` 中 `AppOta_RequestUpgrade()`（BKP2=0、BKP1=`0x3344`，回读后系统复位）与 `AppOta_ConfirmRunning()`（关键初始化成功后 BKP1=`0xAABB`、BKP2=0 并回读）；Boot 仍只读 BKP1/BKP2。文档约定：主机协议（含日后 Modbus）仅在 ACK 主机后调用 `AppOta_RequestUpgrade()` |
| 级别 | **A3**（触及 BKP / 系统复位） |
| 主要文件 | `app_ota.h`；App 侧实现（路径待工程确认） |
| 依赖 | Slice 7 的 BKP 契约与跳转语义 |
| AC | R02 §5.1.3；联调：RequestUpgrade → Boot OTA；ConfirmRunning 清启动计数 |
| 所需 E | 契约/静态 **E2**；实板进 OTA / 确认运行 **E3** |
| 停止/检查点 | 交付卡：API 语义与回读校验；证明未引入 Modbus FC/寄存器映射；确认不改 App 业务 |
| 不做 | **Modbus 功能码/寄存器映射**；修改 App 业务逻辑；改发送器 |

---

## Slice 9 — 出厂 128 KiB 打包工具

| 项 | 内容 |
| --- | --- |
| 目标 | PC 工具：Boot≤16 KiB + App + Meta（READY/VALID=0，FAIL=`0xFFFFFFFF`，`board_id=0x0001`，Meta CRC 正确）→ 固定 131072 字节；反向解析核对；**禁止**用 YMODEM 发送该包 |
| 级别 | A2 |
| 主要文件 | 出厂工具源码（语言/路径待工程确认）；复用 Slice 1 codec 规则 |
| 依赖 | Slice 1（布局与 CRC）；理想情况下 Boot/App 产物可用 |
| AC | AC-YM-19 |
| 所需 E | 工具自检 **E1/E2**；产线烧录回读 **E3**（授权后） |
| 停止/检查点 | 交付卡：包布局图、自检向量、GT32 不嵌入片内包的说明 |
| 不做 | 经 YMODEM 下发工厂包；强制预置 Secondary/LastGood |

---

## 建议实施顺序与集成注意

```text
1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9
```

- Slice 1 与工具侧（Slice 9）的 codec 应共用同一套偏移/CRC 定义，避免双份漂移  
- Slice 4 完成前可用主机回放推进 E2；RS485/掉电不得用 E2 冒充 E3/E4  
- AC-YM-15/20/22 的完整 E4 可在 Slice 6–7 后做「发布验收」专项，不阻塞单片「实现完成，发布验收未完成」状态  
- 任一片发现必须改 R02 行为：停 → 设计变更 → 更新冻结卡批准栏  

## 总验收入口（发布）

全部计划切片实现完成后，按 DESIGN_FREEZE §9 逐项核对 AC-YM-01～22 的证据等级；未达 E3/E4 的项不得宣称发布验收完成。
