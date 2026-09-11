# 【设计冻结卡】YMODEM-GT32-D1-R02（R02.1 补丁同步）

> **批准说明（文首）**：用户批准行为终稿 [`../YMODEM-GT32-D1-R02.md`](../YMODEM-GT32-D1-R02.md) **并且** 批准本设计冻结卡后，即构成本项目编码前的 **设计冻结**。此后仅允许在已批准切片上按 A3 流程实施；AI 不得静默改变 OTA 行为。  
> 本节第 4 章「用户批准」**留空**，须由用户亲自填写；AI 不得 invent「已批准」。

---

## 1. 标识与目标

| 字段 | 内容 |
| --- | --- |
| 方案标识/版本 | `YMODEM-GT32-D1-R02` + **R02.1 补丁**（2026-09-11） |
| 任务级别 | **A3**（BootLoader / OTA / Flash / SPI / WDT / RS485） |
| 目标 | 新板（GD32F103CB + GT32L32S0140，**板卡从 v0.1 起**）BootLoader：YMODEM 收原始 App BIN → Secondary（GT32）→ 安装前 LastGood 备份 → App；片内 Meta 三旗标选路；App 经 `AppOta_*` API 写 BKP 进 OTA / 确认运行 |
| 范围 | 新板 BootLoader + 配套升级/出厂工具；App 仅 AppOta BKP 契约；**不含**旧板、**不改**发送器本轮、**不改** App 业务、**不做** Modbus FC/寄存器映射 |
| 行为权威 | [`../YMODEM-GT32-D1-R02.md`](../YMODEM-GT32-D1-R02.md)（≡ FEATURE_SPEC） |
| 提纲背景 | [`../YMODEM-GT32-D1-R01-提纲.md`](../YMODEM-GT32-D1-R01-提纲.md) |
| 当前仓库/提交/源码目录 | 待工程确认（优先移植 CardReaderBootLoader-v0.3.1 / R10 `boot.c`；路径待确认） |
| 板卡/PCB/模块/BOM | 新板 **v0.1 起** + GT32L32S0140；具体 PCB/BOM 细节待工程确认 |
| 编译器/SDK/RTOS/优化 | 裸机；Keil/GCC 入口 **待工程确认**（不得编造路径） |
| 相关手册 | GT32L32S0140 **VER1.0I_N**；GD32F103 用户手册 **截至 2026-09-11 最新版**（不编造假修订号） |
| 修改前基线 | 新板 OTA 骨架待建；基线构建命令 **待工程确认** 后在首切片采集 |

## 2. 不做什么 / 必须保持不变

### 2.1 不做什么（本次）

- 旧板（无 GT32）适配与双板共存发布
- 修改 YMODEM 发送器本轮行为
- App 业务功能改造（仅 `AppOta_RequestUpgrade` / `AppOta_ConfirmRunning` BKP 契约）
- **Modbus 功能码/寄存器映射**（明确不做；主机协议日后自行调用 AppOta API）
- v1 外挂 OTA meta（GT32 `0x40000`）
- v1 在 BKP2≥5 时自动 LastGood→App 恢复（策略 B：只进 OTA）
- 对 GT32 使用 Chip Erase（`60h`/`C7h`）
- 边擦边写 Secondary；传输过程写 Meta
- 编造 SPI 引脚号或未确认的 Keil 工程路径

### 2.2 必须保持不变（冻结后）

- 分区地址与容量（Boot 16 KiB / App 110 KiB / Meta 页事务 1 KiB / Secondary·LastGood 各 128 KiB）
- 三旗标语义与上电选路表（R02 §2.2）
- YMODEM 单缓冲 DMA、Reply/SendPair、两协议计时、预擦扣除、收尾 5 s
- 传输/安装各 3 次及五类失败出口（R02 §3.6）
- BKP1/BKP2 契约、`AppOta_*` API 与统一跳转交接要求
- Boot 栈 ≥ `0x800`；DMA RX **static 2048**；ISR 仅冻结轮次标志
- GT32 WIP 超时：PP 100 ms / SE 500 ms / BE 2000 ms
- LastGood「当前 App 有效」= 向量 OK（不要求 Meta APP_VALID）
- 文件名 `cardreader-v*`、`board_id=0x0001`、`APP_IMAGE_MAX=112640`
- Meta CRC 间断 72 字节覆盖规则
- 「最终 ACK ≠ 升级成功」与 RS485 隔离要求

## 3. 接口、状态与数据所有权

### 3.1 模块职责（R02 §6）

| 职责 | 所有者 |
| --- | --- |
| 上电选路 | `main.c` |
| OTA 阶段机 / 安装 / BKP / 跳转 | `boot.c`（唯一阶段机） |
| Meta 编解码（含 board_id） | `ota_meta_codec.*` |
| Meta 擦写与三旗标 | `ota_meta.*` |
| Secondary/App 校验、LastGood、搬运 | `ota_image.*` |
| 分区常量 | `ota_layout.h` |
| 帧判定（不发送、不碰 DMA） | `drv_ymodem.*` |
| DMA Peek/Release、阻塞发送 | `drv_usart.*` |
| 时基 | `drv_time.*` |
| 片内 FMC | `fmc.*` |
| GT32 薄驱动 | `gt32_raw.*` |
| SPI 引脚/时钟 | `board_gt32.h`（及对应 `.c`） |
| App↔Boot OTA API | `app_ota.h` + App 侧实现 |

### 3.2 状态机（摘要）

**上电选路（只看片内三旗标，不依赖 GT32）：**

1. `IMAGE_READY` 未置位 → OTA  
2. READY 置位，VALID 未置位，FAIL 未置位 → 安装（不开 UART、不发 C）  
3. READY 置位且 FAIL 置位 → OTA（不再安装这份 Secondary）  
4. READY、VALID 均置位，FAIL 未置位 → 看 BKP1/BKP2  
5. VALID 与 FAIL 同时置位 → 不跳 App，OTA  

READY+VALID 且 FAIL 未置位后：BKP1=`0x3344` → 消费为 `0xAABB` 进 OTA；否则 `BKP2≥5` → OTA（v1 无 LastGood 自动恢复）；否则统一跳转（`BKP2++`→回读→硬件交接）。

**OTA 会话阶段（boot.c）：** 等待文件头 → ACK 后 `ERASE_SECONDARY`（阻塞整槽预擦）→ 收数据 → 双 EOT/空 Block0 → 固定 5 s 收尾 → `SESSION_DONE` → 提交 `IMAGE_READY` → 进入安装。

**置位规则：** READY/VALID 仅完整 `0x00000000` 为置位；FAIL 仅 `0xFFFFFFFF` 为未置位（半写视为已置位）。

### 3.3 数据所有权

- DMA 单缓冲：中断发布冻结轮次；主循环按 `length` 消费；`RxRelease()` 重装 DMA；`drv_ymodem` 不得重装 DMA  
- 接收信息（文件名/版本/大小/包号/运行 CRC）：**仅 RAM**，传输中不写 Meta  
- Secondary/LastGood：仅经 `gt32_raw`；Boot 不链接字库库  
- Meta：整页事务只擦写第一页 1 KiB；第二页预留不擦写  

## 4. 正常 / 错误 / 掉电路径

### 4.1 正常路径（一次成功 OTA）

1. App 调用 `AppOta_RequestUpgrade()`（BKP2=0、BKP1=`0x3344`，回读后复位）  
2. Boot 发 C → 收文件头 → ACK → **阻塞**擦满 Secondary 128 KiB → 发 C  
3. 逐包写入 Secondary → 长度/向量/镜像 CRC 通过 → 双 EOT → 空 Block0 → 最终 ACK → 5 s 收尾关会话  
4. 整页提交 Meta + `IMAGE_READY`（最多 3 次事务）  
5. 安装：复核 Secondary →（向量 OK 则）整槽 112640 备份到 LastGood（必须成功，否则本轮失败、**不擦 App**；向量 NOT OK 则跳过）→ 擦 App → Secondary→App → 校验 → BKP2=0 回读 → `APP_VALID` → 统一跳转  
6. 新 App 调 `AppOta_ConfirmRunning()`；上位机确认业务应答与目标版本后，才认定升级成功  

### 4.2 错误 / 超时 / 复位路径（摘要，详见 R02 §3.6）

| 出口 | 行为 |
| --- | --- |
| UART 本地发送连续 3 次无 TC | 立即复位；不发 CAN；不计阶段次数 |
| 传输尝试（含 OTA 阶段 GT32 失败） | 先等 15 s；第 1、2 次重收；第 3 次复位；不置 FAIL |
| Meta `IMAGE_READY` 事务 | 最多 3 次；脏页不续写；用尽不复位、不装 App，留 Boot 发 C |
| 安装尝试（含 LastGood/搬运/GT32） | 第 1、2 次等 5 s 再装；第 3 次写 FAIL 后延时复位；FAIL 写不进则留 OTA |
| 跳转失败（交接后返回） | 立即复位 |

单包 NAK、重复包、5 s 催发不累计阶段失败。传输与安装额度不共用。

### 4.3 掉电路径（必须可测）

至少覆盖：接收中；Meta 事务中；LastGood 备份中；搬运中；BKP2 已清而 VALID 未提交；FAIL 提交中；GT32 写到一半。原则：不能确认 App 有效则不跳转；READY 成功前不擦 App；LastGood 成功前不擦 App；半写 FAIL 视为已置位走上电 OTA。

## 5. 兼容性

- 仅新板包；`board_id=0x0001`；文件名小写 `cardreader` 前缀  
- 新 Boot 的 BKP2 规则与旧板旧 App 确认写法**不兼容**，不得混用发布  
- 出厂片内 128 KiB 整包（Boot+App+Meta）；GT32 出厂可空白；**禁止**用 YMODEM 发送工厂整包  

## 6. 资源与时序预算

| 项 | 预算 |
| --- | --- |
| Boot 镜像 | ≤ 16 KiB |
| App 镜像 | ≤ 112640 字节（`APP_IMAGE_MAX`） |
| Meta 事务页 | 1 KiB（`0x0801F800`–`0x0801FBFF`） |
| GT32 Secondary | 128 KiB |
| GT32 LastGood | 128 KiB |
| UART | 9600 8N1；发送预算 5 ms + 字节×3 ms；每响应最多 3 次 |
| 进展 / 总会话 / 收尾 | 5 s / 15 min / 最终 ACK TC 后固定 5 s |
| 头 ACK 后等 C（发送端） | 默认 **30 s**（实板不够按 AC-YM-05 上调） |
| FWDGT | IRC40K，div256，重装 4095，标称约 26.2 s |
| SPI 时钟 | 初期 ≤ **9 MHz** |
| Boot 栈 | ≥ **`0x800`（2048）** |
| DMA RX | **static 2048** 字节 |
| GT32 WIP | PP **100 ms**；SE 4KB **500 ms**；BE 64KB **2000 ms** |

## 7. 受影响构建配置矩阵

| 配置 | 说明 |
| --- | --- |
| Boot | 含 YMODEM + `gt32_raw`；无字库库 |
| App | 链接 `0x08004000`；`AppOta_RequestUpgrade` / `AppOta_ConfirmRunning`（Slice 8） |
| 出厂工具 | 片内 128 KiB 打包（A2 切片） |
| Debug/Release、Keil/GCC | **待工程确认** |

## 8. 任务切片与检查点（高层）

详情见 [IMPLEMENTATION_PLAN.md](./IMPLEMENTATION_PLAN.md)。高层顺序：

1. `ota_layout.h` + Meta codec/`board_id`（无 YMODEM）— E1  
2. `gt32_raw` probe/读/写/擦 — E1→E3  
3. Secondary 预擦 + 写路径单元 — E2/E3  
4. YMODEM 收包入 Secondary — E2→E3  
5. Meta `IMAGE_READY` 整页事务 — E2；掉电 E4 后续  
6. 安装：LastGood + Secondary→App + VALID — E3/E4  
7. Boot 选路 + BKP1/BKP2 跳转 — E3  
8. AppOta API + ConfirmRunning（BKP/复位，A3；**不做** Modbus FC）  
9. 出厂 128 KiB 打包工具 — A2  

**检查点**：每个切片结束后停止，输出【交付卡】；未经批准不得连续越片实施高风险面。用户可在任务卡中明确「允许连续实施的切片」以合并汇报，但仍须逐片列出改动与证据。

## 9. 验收与证据计划（AC-YM-01～22 → E）

| 验收项 | 范围摘要 | 所需证据等级 | 说明 |
| --- | --- | --- | --- |
| AC-YM-01 | 字符串/格式/`board_id`/`APP_IMAGE_MAX` | E2（主机）→ E3（实板收包） | 协议解析可先 E2 |
| AC-YM-02 | 末字节补齐、页写不越界、CRC 不含补齐 | E2→E3 | |
| AC-YM-03 | Meta 布局与 CRC 72 字节间断覆盖 | E1/E2 | 布局/编解码 |
| AC-YM-04 | Meta 整页事务×3、半写不补写 | E2；掉电见 AC-YM-15→E4 | |
| AC-YM-05 | Secondary 预擦 128 KiB、禁 CE、喂狗与时长登记 | E3（实板计时） | 30 s 等待依赖实板 |
| AC-YM-06 | DMA/协议帧、重复包、双 CAN/EOT | E2→E3 | 单元协议 → E2 |
| AC-YM-07 | 两计时与擦除窗口扣除 | E2→E3 | |
| AC-YM-08 | 发送端等待约定 | E2/E3 | 与上位机联调 |
| AC-YM-09 | 再同步、发送失败复位、Meta 失败不向旧会话发 CAN | E3 | |
| AC-YM-10 | 看门狗配置与真实卡死复位 | E3/E4 | |
| AC-YM-11 | 收尾 5 s、关会话后才写 Meta | E2→E3 | |
| AC-YM-12 | 传输计数×3 | E2→E3 | |
| AC-YM-13 | 复位与三旗标恢复 | E3 | |
| AC-YM-14 | 提交时序（READY/LastGood/BKP2/VALID） | E3；关键中断点 E4 | |
| AC-YM-15 | **掉电**（接收/Meta/LastGood/搬运/BKP2–VALID/FAIL/GT32 半写） | **E4** | 必须在目标板真实掉电注入 |
| AC-YM-16 | BKP2 0～4 / ≥5；无旧魔数；v1 无自动恢复 | E3 | |
| AC-YM-17 | 跳转交接（RS485/DMA/VTOR/栈/WDT） | E3 | |
| AC-YM-18 | 故障出口留 OTA / FAIL 半写 | E3/E4 | |
| AC-YM-19 | 出厂 128 KiB 整包 | E1/E2→E3 烧录回读 | |
| AC-YM-20 | 连续升级与 RS485 隔离 | **E3/E4** | 真实总线时序与隔离 |
| AC-YM-21 | LastGood 备份强制成功；损坏退化；不测自动恢复 | E3/E4 | |
| AC-YM-22 | GT32 缺失/SPI 失败不破坏片内 App | E3/E4 | |

补充：单元级协议/编解码 → **E2**；RS485 半双工与实板收发 → **E3/E4**；掉电一致性 → **E4**。PC/Mock 故障注入最高记 **E2**。

## 10. 仍未确认的事实（开放项）

1. **确切 Keil（或 GCC）工程路径与构建命令** — 待工程确认；源码仓库路径（含 CardReaderBootLoader-v0.3.1 移植源）同样待确认  
2. **实板 Secondary 128 KiB 预擦墙钟时长** — 决定发送端「头 ACK 后等 C」是否维持默认 30 s（AC-YM-05 登记）；超限则设计变更上调发送端等待，不得静默改协议  

**已关闭（R02.1）：** 板卡从 **v0.1** 起；手册引用（GT32 VER1.0I_N；GD32 截至 2026-09-11 最新版）；Modbus FC **明确不做**（改为 AppOta API）；LastGood 向量判定；Boot 栈/`static` DMA；Meta CRC 升序喂入；GT32 WIP 超时；移植非重写策略。  

**已关闭（SPI 引脚，2026-09-12）：** 来源 **用户确认**。SPI2：SCK=`PB13`、MISO=`PB14`、MOSI=`PB15`（与屏共用）；GT32 CS=`PB9`；屏 CS=`PB12` **仅文档**（Boot **不驱动**）。已写入 `board_gt32.h`，`BOARD_GT32_PINS_CONFIRMED=1`。**引脚确认 ≠ E3**；实板读写擦与烧录仍须另授。不得再编造其它引脚。  

## 11. 方案偏离时的停止条件

出现以下任一情况必须暂停受影响实现并提交设计变更：计划外生产文件/状态/重试/超时/恢复；改批准范围外的协议字段、Flash 布局、链接地址；资源超预算；冻结卡要求的关键验证无法执行且会依赖猜测；为修问题必须无关重构。

## 12. AI 建议但不实施（默认）

- BKP2 耗尽自动 LastGood 恢复（二期）  
- 外挂 OTA meta  
- 提高 SPI 时钟超过 9 MHz（实板稳定后再评估）  
- 改发送器或 App 业务  

## 13. 用户批准

> **本节只能由用户填写。** AI 整理文档时不得根据上下文自行填写「批准」。批准来源须可追溯（原话 / Issue / PR / 其他记录）。

```text
对应方案标识/版本：YMODEM-GT32-D1-R02（含 R02.1）+ 本 DESIGN_FREEZE
批准状态：调整后批准（双角色自动流水线）
批准来源：用户原话 2026-09-11 — 开架构官+实现AI自动从头到尾跑项目；板卡v0.1；手册最新；App仅OTA接口；其余按最优项
批准人/日期：BO BO / 2026-09-11

调整：
1. 以「OTA架构官」代理日常切片放行与交付审查；「OTA实现AI」仅实施已批准切片
2. Modbus FC 不在范围；App 使用 AppOta_RequestUpgrade/ConfirmRunning
3. SPI 引脚已于 2026-09-12 用户确认并写入 board_gt32.h；Keil/GCC 路径仍可主机侧推进；实机 E3/E4 标未验证；禁止目标板擦写/烧录直至另授

允许连续实施的切片：Slice 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8(AppOta API) → 9；每片结束向架构官交交付卡，架构官可通过后自动放行下一片（高风险偏离须暂停）
汇报检查点：每切片结束交付卡；架构官抽检关键路径；整仓主机测试通过后汇总
允许的烧录、擦写或其他不可逆操作：无（未另授权前禁止目标板擦写/烧录）
```

批准本冻结卡 + R02 后，即允许在「允许连续实施的切片」范围内开始编码；未列出的切片仍须另行批准或在本栏补登。
