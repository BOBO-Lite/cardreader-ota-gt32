# AGENTS.md

> CardReader 新板 BootLoader OTA（GT32）长期规则。删掉后容易造成错误的内容才保留。  
> 行为细节以 [`../YMODEM-GT32-D1-R02.md`](../YMODEM-GT32-D1-R02.md) 为准；本文件只固化协作边界与红线。

## 1. 项目基线

- 产品/用途：CardReader **新板** BootLoader OTA（含 GT32L32S0140 外挂 Secondary/LastGood）；旧板（无外挂）**不在范围**
- MCU：GD32F103CB（片内 Flash 128 KiB）
- 外挂：GT32L32S0140 字库芯片用户区 512 KiB（`0x000000`–`0x07FFFF`）
- 板卡/PCB/BOM：新板 **从 v0.1 起**（有 GT32）；具体 BOM 细节待工程确认
- RTOS/运行框架：**裸机**（除非 R02 另有说明；当前 R02 未假定 RTOS）
- 编译器/SDK/优化等级：**待工程确认**（Keil / GCC 入口均待钉死）
- 实际生效源码目录：待工程确认（优先移植 `CardReaderBootLoader-v0.3.1` / R10；R02 §6 建议路径：`code/USER/...`）
- 主要构建配置：Boot / App；Debug/Release — **待工程确认**
- 行为权威：[`../YMODEM-GT32-D1-R02.md`](../YMODEM-GT32-D1-R02.md)（`YMODEM-GT32-D1-R02` + **R02.1 补丁**；≡ FEATURE_SPEC）
- 手册：GT32L32S0140 **VER1.0I_N**；GD32F103 用户手册 **截至 2026-09-11 最新版**
- 提纲（背景）：[`../YMODEM-GT32-D1-R01-提纲.md`](../YMODEM-GT32-D1-R01-提纲.md)
- C 专项规范：[C 分册](https://github.com/BOBOEMS/AI_Collaborative_Code_Standards/tree/main/C)（**链接，勿整份复制**）
- 协作协议：[EAI-DP V1.2](https://github.com/BOBOEMS/AI_Collaborative_Code_Standards/blob/main/AI%20%E5%8D%8F%E4%BD%9C%E5%BC%80%E5%8F%91%E8%A7%84%E8%8C%83/embedded-ai-development-protocol-v1.2.md)
- 团队风险与人工评审：本项目为 **个人+AI**，**不采用**共享治理 R0–R2 强制门禁；人工审查仍覆盖关键路径与证据

## 2. 分区与协议（摘自 R02，勿静默改）

### 2.1 片内（GD32F103CB）

| 分区 | 地址 | 容量 |
| --- | --- | --- |
| Boot | `0x08000000`–`0x08003FFF` | 16 KiB |
| App | `0x08004000`–`0x0801F7FF` | 110 KiB |
| Meta/DATA | `0x0801F800`–`0x0801FFFF` | 2 KiB（整页事务只擦第一页 1 KiB：`0x0801F800`–`0x0801FBFF`） |

无片内 Backup/DOWNLOAD。`APP_IMAGE_MAX = 112640`。

### 2.2 GT32 用户区

| 外挂偏移 | 容量 | 用途 |
| --- | --- | --- |
| `0x00000` | 128 KiB | Secondary（YMODEM 落盘） |
| `0x20000` | 128 KiB | LastGood（安装前备份当前 App） |
| `0x40000` 起 | 余下 | 配置/资源；**v1 不做外挂 OTA meta** |

### 2.3 通信与入口

- Boot OTA 阶段：UART **9600 8N1**，RS485 半双工，**YMODEM**（DMA **static 2048**）
- App↔Boot：`AppOta_RequestUpgrade()` / `AppOta_ConfirmRunning()`（写 BKP；**不做** Modbus FC 映射）
- BKP1：请求 `0x3344`，已消费 `0xAABB`；BKP2：裸数值，门限 5；不用 BKP3
- 文件名：小写前缀 `cardreader-v<主>.<次>.<修订>.bin`；`board_id = 0x0001`（Boot 写入 Meta）

## 3. 构建与验证入口

- 修改前基线构建：**待工程确认**（Keil 工程路径 / GCC 命令未钉死前不得假装已有基线命令）
- 静态检查：按 C 分册与工程既有工具；无则注明“无”
- PC 单测/Mock/协议回放：优先覆盖 Meta 编解码、YMODEM 帧判定、镜像边界（E2）
- 目标板烧录：**烧录/擦写前必须获得用户明确授权**（目标、范围、次数）
- 板端功能测试：真实 RS485 + GT32 + FWDGT（E3）
- 故障注入：掉电/复位/介质失败（E4；见 DESIGN_FREEZE 证据映射）
- 构建产物目录：放在约定输出目录，不得覆盖用户交付物

## 4. AI 协作规则

- 遵守 EAI-DP V1.2；本仓库入口见 [README.md](./README.md)
- **R02 是唯一行为权威**；AI 不得静默改变 OTA 行为
- 只实现当前任务明确要求的行为；范围外保持不变
- 未要求的新功能、状态、重试、超时、恢复、配置、抽象、依赖和重构，只能列为建议，不得实施
- “分析、解释、审查、诊断、先给方案”默认 A0，只读
- A1：局部行为、约 ≤2 生产文件、≤约 80 行有效生产代码，且不触及红线
- A2：先微型方案，批准后按可独立验收行为实施
- 命中任一红线即为 **A3**：先设计冻结 + 用户批准；独立上下文 AI 复审；人审关键路径与证据
- 用户批准方案版本、允许切片与检查点后，授权在该范围内持续有效；不对已批准内容重复请示
- AI 可主动升级，不得自行降级；风险由行为决定，不由行数决定
- 方案冻结后不得自行偏离；出现计划外变化时暂停并报告
- 保留用户已有修改；发现未知改动先报告
- 不执行删除、远端推送、生产设备修改、目标板擦写/烧录或其他不可逆操作，除非用户明确授权

### 个人+AI 特则

- **不要求**共享团队 R0–R2 门禁或固定评审角色
- A3 仍必须：设计冻结 + 独立 AI 复审 + 人对关键路径/证据审查
- 板端 Flash 擦写/烧录必须显式用户授权；任务卡已写明目标/范围/次数时在授权内执行

## 5. A3 嵌入式红线（本项目重点）

以下任一变化必须先冻结设计（不得静默决定）：

- **BootLoader / OTA**：分区地址、向量、镜像有效条件、校验顺序、安装时机、回滚（含 LastGood）、版本/文件名规则、`APP_IMAGE_MAX`、三旗标语义
- **Flash / Meta**：片内 Meta 布局、整页事务、CRC 覆盖范围（含间断 `board_id`）、半写规则
- **GT32 / SPI**：引脚复用、时钟、命令集；**硬性禁止 Chip Erase（`60h`/`C7h`）**；时钟初期 **≤ 9 MHz**；引脚在 `board_gt32.h`，**未确认前不得编造引脚号**
- **ISR / DMA**：USART IDLE、单缓冲 Peek/Release、缓冲区所有权；ISR **仅**冻结轮次标志（禁止 Flash/YMODEM/GT32）
- **栈 / SRAM**：Boot 栈 ≥ **`0x800`**；DMA RX 必须 **static 2048**（禁止大栈 VLA）
- **通信 / RS485**：线上字节、半双工 DE、Reply/SendPair、超时与催发、最终 ACK ≠ 升级成功
- **看门狗**：FWDGT 配置、喂狗位置（主循环/段擦/等待）、交接时保持运行
- **资源/时序**：Boot ≤16 KiB、App ≤112640、Meta 页 1 KiB、Secondary/LastGood 各 128 KiB；预擦等待默认 30 s；GT32 WIP：PP **100 ms** / SE **500 ms** / BE **2000 ms**

日志/解析一旦影响上述行为，也按 A3。

## 6. 修改前必须确认（A2/A3）

编码前报告：仓库/分支/提交与未提交修改；实际源码树；板卡/BOM；工具链与优化等级；受影响构建矩阵；修改前构建/告警/ROM/RAM/已有测试；已确认事实 / AI 推断 / 仍待实测。

## 7. 完成交付

- 每切片保持目标构建通过（或遵守已批准的已知失败基线规则）
- 每个验收项：`PASS / FAIL / 未验证` + E0–E4 + 证据 ID
- 证据索引关联源码状态、构建配置、产物；E3/E4 关联板卡与测试条件；PC/Mock 故障注入最高 E2
- 单列“需求未明确但 AI 补充的逻辑”
- 报告修改前基线与修改后增量
- 编译成功只能称“目标构建通过”；无实机证据必须写“实机尚未验证”
- A3 未达冻结卡规定的 E3/E4 时，只能称“实现完成，发布验收未完成”
- A /（本项目不强制的）R / E 三者不得互相替代

## 8. 项目专属规则（来自 R02）

- 不允许静默修改：R02 正文行为、分区常数语义、禁止 CE、BKP 契约、三旗标置位规则
- Boot **不得**链接字库/高层 GT32 库；仅 `gt32_raw` 薄驱动
- `boot.c` 唯一 OTA 阶段机；`drv_ymodem` 只判帧不发送不碰 DMA；`drv_usart` 独占 USART/DMA；`gt32_raw` 独占 SPI 命令
- 传输过程不写 Meta；会话关闭后才整页提交 `IMAGE_READY`；READY 成功前不擦 App；LastGood 备份成功前不擦 App；BKP2 清 0 早于 `APP_VALID`
- 传输与安装各最多 3 次，失败计数仅 RAM，复位归零
- v1：**不做** BKP2≥5 时 LastGood→App 自动恢复（策略 B）
- Meta CRC：按升序地址喂入 `0x00..0x43` 再 `0x54..0x57`（`board_id`），合计 72 字节，间断覆盖（poly `0xEDB88320`）
- LastGood「当前 App 有效」= MSP/Reset 向量 OK（**不**要求 Meta `APP_VALID`）；向量 NOT OK 则跳过 LastGood
- AppOta API：`AppOta_RequestUpgrade` / `AppOta_ConfirmRunning`；**明确不做** Modbus FC
- SPI 引脚：**待原理图确认**，写入 `board_gt32.h`；不得 invent 引脚号
- 构建入口：Keil/GCC — **待工程确认**
- 已发生/须防止的错误（预置）：编造 SPI 引脚；对 GT32 发 Chip Erase；用 Mock 掉电冒充 E4；静默改 R02 时序或失败出口；把最终 ACK 当成升级成功
