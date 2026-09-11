# ymodem bootloader 升级方案 — 新板 GT32 修订提纲

- 基线旧稿：`YMODEM-031-D1-R10`（`CardReaderBootLoader-v0.3.1`）
- 本稿版本：`YMODEM-GT32-D1-R01`（提纲）
- 日期：2026-09-11
- 范围：**仅新板**（有 GT32L32S0140）；旧板（无外挂）本次不做
- 传输：OTA 阶段 **YMODEM**（仍在 Boot）；APP 日常 **Modbus**（只约定：EnterOta → 写 BKP 后复位）
- 标记：`[留]` 行为与旧稿一致 · `[改]` 相对旧稿必须改 · `[增]` 旧稿没有、新板加入 · `[删]` 本次不做/作废表述

本文是把 R10 迁到 GT32 新板的**修订提纲**，供写成终稿与实现对照。未标改的细节默认沿用 R10。

---

## 0. 相对 R10 的总变更一览

| 项 | R10 | 新板 GT32 |
| --- | --- | --- |
| Boot | 7 KiB @ `0x08000000` | `[改]` 16 KiB @ `0x08000000`–`0x08003FFF` |
| App | 60 KiB @ `0x08001C00` | `[改]` 110 KiB @ `0x08004000`–`0x0801F7FF` |
| Backup/Secondary | 片内 60 KiB @ `0x08010C00` | `[改]` GT32 用户区 128 KiB @ 外挂 `0x00000` |
| LastGood | 无（Backup 非回滚） | `[增]` GT32 `0x20000` 起 128 KiB |
| Meta | 片内 1 KiB @ `0x0801FC00` | `[改]` 片内 2 KiB DATA @ `0x0801F800`–`0x0801FFFF`（布局仍用前 ~84 B） |
| 擦除粒度（Backup） | 片内 1 KiB 页 | `[改]` GT32 4 KiB 扇区（可用 `D8` 64 KiB 块擦加速）；**禁止 Chip Erase** |
| 镜像大小 | 8～61440 | `[改]` 下限仍 8；上限 ≤ App 槽可用（建议 ≤ 108 KiB，终稿钉死常数） |
| YMODEM 所在 | Boot | `[留]` Boot（已在 7 KiB 跑通，16 KiB 更宽裕） |
| 三旗标 / BKP / 安装出口 | 见 R10 | `[留]` |
| 出厂包 | 128 KiB 含 Boot+App+Backup+Meta | `[改]` 片内 128 KiB（Boot+App+Meta）；GT32 出厂可空或另刷 |

---

## 1. 设计目标

`[留]` 下列目标不变：

- 标准单文件 YMODEM 接收原始 App BIN，文件名携带版本。
- 单个 DMA 接收缓冲，串口 IDLE 中断交接。
- Backup/Secondary **在收数据前整区擦完**；擦除一次完成、分段喂狗，**不边擦边写**。
- 下载写入 Backup/Secondary；接收信息在 RAM；**传输过程不写 Meta**。
- 传输与安装各最多 3 次，失败次数只存 RAM，复位归零。
- 会话正常结束且 Meta 回读通过后，才提交 `IMAGE_READY`；之后才允许擦写 App。
- App 校验通过、BKP2 清零并回读后，才提交 `APP_VALID`。
- 上电只根据三个 Meta 状态字选路；不能确认 App 有效时不跳转；Boot 保留 OTA 入口。

`[改]` 表述修正：

- 「Backup 会被新 OTA 覆盖，不是旧版本回滚区」→ Secondary 仍可被覆盖；**回滚改由 LastGood 承担**（见 3.5 / 3.7）。

`[增]`：

- Boot 内仅链接 **GT32 薄 SPI 驱动**（读/页写/扇区擦/块擦/RDSR）；**不链接高通字库库**。
- APP 日常 Modbus；升级入口为 Modbus 命令触发的 BKP 写入（接口见 5.1.3）。

---

## 2. 总体流程

### 2.1 Flash / 外挂分区

`[改]` 分区表替换 R10 §2.1。

#### 2.1.1 片内（GD32F103CB，128 KiB）

| 分区 | Flash 起始 | 容量 | 用途 |
| --- | --- | --- | --- |
| Boot | `0x08000000` | 16 KiB | BootLoader + YMODEM + gt32_raw |
| App | `0x08004000` | 110 KiB | 运行中的 App |
| Meta/DATA | `0x0801F800` | 2 KiB | 镜像信息与三旗标（有效载荷仍约 84 B，余 `0xFF`） |

- **无片内 Backup/DOWNLOAD。**
- 地址常数集中在 `[改] ota_layout.h`。

#### 2.1.2 GT32L32S0140 用户区（`0x000000`–`0x07FFFF`，512 KiB）

| 外挂偏移 | 容量 | 用途 |
| --- | --- | --- |
| `0x00000` | 128 KiB | Secondary（原 Backup，YMODEM 落盘） |
| `0x20000` | 128 KiB | LastGood（安装前备份当前 App） |
| `0x40000` | 4 KiB | OTA 外挂 meta 镜像（可选；主旗标仍以片内 Meta 为准） |
| `0x41000` | ~252 KiB | 配置 / 日志 / 资源 |

`[留]` 分区固定，不随 BIN 长度移动。  
`[增]` 硬性禁止对 GT32 发 Chip Erase（`60h`/`C7h`）。OTA 只擦 Secondary/LastGood/外挂 meta 所属扇区。

### 2.2 Boot 启动

`[留]` 开备份域写、配看门狗后，**只读片内三个 Meta 状态字**选路。文件名、版本、Meta CRC 不参与选路。

`[留]` 三旗标判定表（READY / VALID / FAIL）与 R10 §2.2 完全一致，包括：

- READY/VALID：仅完整 `0x00000000` 为置位
- FAIL：仅 `0xFFFFFFFF` 为未置位，其它（含半写）为置位
- VALID 与 FAIL 同时置位 → 不跳 App，进 OTA
- READY 置位且 FAIL 置位 → OTA，不再安装这份 Secondary

`[留]` READY+VALID 且 FAIL 未置位之后：

1. BKP1=`0x3344` → 消费为 `0xAABB`，进 OTA  
2. 否则 `BKP2 >= 5` → OTA（含 `0xFFFF` 等）  
3. `BKP2 < 5` → 统一跳转（`BKP2++` → 回读 → 硬件交接 → 喂狗 → App）

`[增]` 可选增强（终稿二选一，默认建议 A）：

- **A（推荐）**：`BKP2 >= 5` 且 LastGood 校验通过 → 先 LastGood→App 恢复，清 BKP2，再跳；恢复失败才进 OTA  
- **B**：保持 R10，仅进 OTA，LastGood 只在安装失败路径使用

`[留]` 统一跳转必须：释放 RS485；关 Boot DMA/串口/中断/SysTick；清 NVIC 挂起；设 App `VTOR`、屏障、交接 MSP；看门狗交接前喂狗并保持。

`[改]` 上电选路**不依赖** GT32；仅进入 OTA 收包或安装搬运时才初始化 SPI/GT32。probe 失败：若处于待安装 → 走安装失败出口；若处于 OTA → 无法擦写 Secondary，按传输/介质失败处理。

### 2.3 一次正常 OTA

`[留]` 主流程骨架与 R10 §2.3 相同，仅 Backup→Secondary、介质改为 GT32：

1. App（经 Modbus）清 BKP2、置 BKP1=`0x3344`，回读后复位。  
2. Boot 发 `C`，收并检查文件头。  
3. 头有效后先 ACK，再**阻塞擦完整个 Secondary**（GT32 扇区/块擦，段尾喂狗）。擦除期间不回主循环。擦除时长不计入两个协议计时（扣除规则同 R10）。  
4. 擦完发 `C` 且不 `RxRelease()`；之后只逐包写入。  
5. 收完声明字节；长度、向量、整镜像 CRC32 通过。  
6. 双 EOT、结束空 Block0、最终 ACK，固定 5 秒收尾后关闭会话。  
7. 整页事务提交片内 Meta 并确认 `IMAGE_READY`。传输过程不写 Meta。  
8. **安装阶段**（见 3.5）：`[增]` 可选先 App→LastGood；复核 Secondary；搬运到 App；BKP2 清 0；提交 `APP_VALID`；统一跳转。  
9. 上位机确认 App 业务（Modbus）与版本后，才认定升级成功。

`[留]` 传输与安装各 3 次，不共用额度。  
`[留]` RS485 总线上其他设备隔离要求见 5.1.4。

---

## 3. 详细设计

### 3.1 固件文件与接收信息

`[留]` 原始 App BIN，不追加 Meta 封装；信息在 RAM；写入 Meta 的字符串编码同 R10。

`[改]` 文件名约定：可继续 `cardreader-v<主>.<次>.<修订>.bin`，或产品改名；规则「小写前缀 + 完整版本」保留。

`[改]` 实际大小：`8 ～ APP_IMAGE_MAX`（替换 61440；建议 `APP_IMAGE_MAX = 110KiB - 余量`，终稿写死，例如 `110592` 或 `0x1B000` 内可用值）。

`[增]` 建议在 RAM/Meta 增加 `board_id`（新板常量，如 `0x0001`），防止误刷；无该字段的旧包直接拒绝（本次只发新板包）。

### 3.2 DMA 单缓冲接收

`[留]` 整节保留：9600 8N1、RS485 半双工、DMA 单 2048、IDLE/`RxPeek`/`RxRelease`、`Boot_OtaReply`/`Boot_OtaSendPair`、发送预算、不启用错误中断、DE 与 /RE 相连等。

`[改]` 若新板串口参数有变，仅改波特率常数；协议状态机不动。（默认仍 9600，与 R10 一致，除非硬件另定。）

#### 3.2.1 职责划分

`[留]` `boot.c` 唯一 OTA 阶段机；`drv_usart` 独占 DMA；`drv_ymodem` 只判帧不发送。

`[增]` `gt32_raw.c`：Secondary/LastGood 擦写读；`boot.c` / `ota_image.c` 经它访问外挂，不直接啃 SPI 寄存器。

### 3.3 YMODEM 会话

#### 3.3.1 帧校验

`[留]` SOH 133 / STX 1029、长度先判、包号、CRC16、重复包补 ACK、声明长度内才写 Flash、末字补 `0xFF` 规则、双 CAN、单缓冲不拼残包——全部保留。

`[改]` 「写 Flash」→「写 GT32 Secondary」；4 字节对齐补齐规则对 Secondary 编程同样适用（页 256 B，不足页按芯片页写规则处理，终稿补一小节）。

#### 3.3.2 Secondary 预擦除（原 Backup 预擦除）

`[留]` 头有效 → RAM 存信息 → ACK → **同轮阻塞整区擦完** → 发 C；擦除中不 Peek；失败 Close→Open 走传输失败。

`[改]` 擦除目标：GT32 `0x00000` 起 128 KiB（或按声明大小向上对齐到 4 KiB，但 **推荐固定擦满 Secondary 槽**，避免空洞旧数据干扰）。  
`[改]` 底层：`20h` 扇区擦 4 KiB；允许 `D8h` 64 KiB 块擦加速；每段末喂狗；**禁止 CE**。  
`[改]` 时长预算：按实板重测；手册/样机登记进 AC-YM-05；发送端「头 ACK 后等 C」**仍先按 30 s**，不够则双方一起加长。  
`[留]` 擦除窗口从两个协议计时中显式扣除。

#### 3.3.3～3.3.5 握手收尾 / 两计时 / 终止再同步

`[留]` 阶段表、5 s 进展 / 15 min 总会话 / 固定 5 s 收尾、催发表、双 CAN、UART 三次发送失败立即复位、Meta 提交失败不向旧会话发 CAN——全部保留。

### 3.4 Meta（片内）

#### 3.4.1 布局

`[改]` 物理基址：`0x0801F800`（2 KiB 页；若 FMC 页仍为 1 KiB，则整页事务覆盖 Meta 所用页，终稿按 GD32F103 中等密度 **1 KiB 页**写清：可只擦写含 Meta 的 1 KiB，或固定擦 2 KiB 内两页）。

`[留]` 逻辑布局前 84 字节字段表（文件名/版本/大小/固件 CRC/Meta CRC/三旗标）与 R10 一致。

`[增]` 可选：在保留区占用 4 字节 `board_id`（若加入，Meta CRC 覆盖范围同步扩展，并升版本号）。

#### 3.4.2～3.4.6

`[留]` 字符串规则、两 CRC32 算法（`0xEDB88320`）、样例、三旗标语义、半写规则、`IMAGE_READY` 整页事务最多 3 次、状态字只从 `0xFFFFFFFF`→`0`、VALID/FAIL 互斥——全部保留。

`[改]` 「大小须在 8～61440」→「8～APP_IMAGE_MAX」。  
`[改]` 擦 Meta 页大小按新基址与 FMC 页大小调整；**仍不作废 App 向量**。

### 3.5 Secondary 安装到 App（原 Backup→App）

`[留]` 仅 READY 已提交且 VALID/FAIL 未置位才安装；上电待安装只开 SysTick，不开 UART；独立安装计数 3；先复核 Secondary 再擦 App；先主体后向量；BKP2 清 0 早于 VALID；失败重试表与 R10 §3.5 一致。

`[增]` 每次尝试建议顺序：

1. 已 `APP_VALID` → 直接跳转（同 R10）  
2. `ReadImageInfo` + 复核 Secondary（长度/向量/CRC）  
3. **（新增）** 若当前 App 向量/CRC 看起来有效 → 整镜像复制到 LastGood（失败可记日志但仍允许继续，或视为本轮安装失败——终稿钉死；推荐：**LastGood 失败则本轮安装失败**，避免无回滚就覆盖唯一好 App）  
4. 擦 App（片内，分段 ≤30 KiB、喂狗）→ 从 Secondary 搬运 → 校验 App  
5. BKP2=0 回读 → 提交 `APP_VALID` → 统一跳转  

`[改]` 校验/搬运读源从片内 Backup 改为 `gt32_raw_read`。  
`[留]` 不写 Boot 或 Meta 数据区以外的状态字规则。

### 3.6 失败出口

`[留]` UART / 传输 / Meta / 安装 / 跳转五类出口表与 R10 §3.6 一致。

`[增]` 介质类：GT32 probe 失败、扇区擦写超时、WIP 异常 → 归入**传输尝试**（收包阶段）或**安装尝试**（搬运阶段），不发明第六套计数。

### 3.7 LastGood 回滚 `[增]`

| 触发 | 行为 |
| --- | --- |
| 安装前 | 见 3.5 步骤 3 |
| 运行中 BKP2 耗尽且选策略 A | LastGood→App，校验通过则清 BKP2 并跳；失败进 OTA |
| LastGood 区无效 | 视为无回滚，行为退化为 R10 |

LastGood 不参与 YMODEM；不参与三旗标选路主路径（除非采用策略 A 作为 VALID 路径上的分支）。

---

## 4. 出厂完整固件包

`[改]` 不再制作「含片内 Backup 副本」的单一逻辑；改为：

1. **片内 128 KiB 工厂包**：Boot（≤16 KiB）+ App + Meta（READY=0，VALID=0，FAIL=`0xFFFFFFFF`）。空余 `0xFF`。  
2. **GT32**：出厂可全 `0xFF`；或产线另写资源区；Secondary/LastGood 建议空。  
3. `[留]` 禁止用 YMODEM 发送工厂整包。  
4. `[留]` 产线 BKP1=`0xAABB`，BKP2=0。  
5. `[改]` 体积门禁：Boot ≤ 16 KiB；App ≤ APP_IMAGE_MAX；向量落在 `0x08004000` 范围。

可选：工厂工具生成「片内 bin + GT32 资源 bin」双文件；验收分开回读。

---

## 5. 参数与验收

### 5.1.1 Boot 参数

`[留]` 绝大多数：9600、DMA 2048、Reply/SendPair、发送预算、进展/总会话/收尾、FWDGT≈26.2 s、传输 15 s×3、Meta 事务×3、安装×3、BKP1/BKP2 常数。

`[改]` Backup 擦除 → Secondary 擦除（GT32；段策略按 4 KiB/64 KiB 重写）。  
`[增]` SPI：时钟 ≤45 MHz（建议启动用较低分频，实板再提）；CS/SPI 引脚以原理图为准写入 `ota_layout`/`board.h`。  
`[改]` Flash wait state 等与 108 MHz 相关项保留；App 链接地址改为 `0x08004000`。

### 5.1.2 发送端等待

`[留]` 初始 C 30 s；头 ACK 5 s；**头 ACK 后等 C 独立 30 s**（预擦可能变长，实板不够则修订为 45/60 s）；数据包/EOT 5 s。

### 5.1.3 App 接口

`[留]` 发起 OTA：BKP2=0、BKP1=`0x3344`，回读后复位。  
`[留]` 确认启动：关键初始化成功后 BKP1=`0xAABB`、BKP2=0。  
`[增]` Modbus：提供保持寄存器/功能码映射到上述写入；**OTA 期间关闭 Modbus 从站**，总线交给 YMODEM（Boot 阶段本无 Modbus）。  
`[留]` 本轮若 App 已符合 BKP 契约，可只加 Modbus 封装不改确认逻辑。

### 5.1.4 RS485 与升级成功

`[留]` 整节保留：隔离其他设备；最终 ACK ≠ 升级成功；以 App 业务应答+版本为准。

### 5.2 验收

`[留]` AC-YM-01～20 结构保留，改为：

| 编号 | `[改]/`[增]` 要点 |
| --- | --- |
| AC-YM-01 | 上限改为 APP_IMAGE_MAX；可选 board_id |
| AC-YM-02 | 写入目标为 GT32 Secondary |
| AC-YM-03 | Meta 基址 `0x0801F800` |
| AC-YM-05 | GT32 整槽预擦、禁止 CE、喂狗间隔实板登记 |
| AC-YM-15 | 增：GT32 写到一半掉电、LastGood 备份中掉电 |
| AC-YM-19 | 工厂包不再含片内 Backup |
| AC-YM-21 `[增]` | LastGood 备份/恢复/损坏退化 |
| AC-YM-22 `[增]` | GT32 缺失或 SPI 失败时不破坏片内 App（待安装失败出口正确） |

---

## 6. 代码入口（审核对照）

`[留]` 模块划分精神不变，路径按新工程调整：

| 职责 | 文件（建议） | 相对 R10 |
| --- | --- | --- |
| 上电选路 | `main.c` | `[留]` |
| YMODEM 阶段机、安装、BKP、跳转 | `boot.c` / `boot.h` | `[留]` + 调安装步骤 |
| Meta 编解码 | `ota_meta_codec.*` | `[留]` 基址/可选 board_id |
| Meta 擦写与旗标 | `ota_meta.*` | `[改]` 基址 |
| 镜像校验与搬运 | `ota_image.*` | `[改]` 读源改 GT32；`[增]` LastGood |
| 分区常量 | `ota_layout.h` | `[改]` 全面替换 |
| 帧判定 | `drv_ymodem.*` | `[留]` |
| DMA USART | `drv_usart.*` | `[留]` |
| 时基 | `drv_time.*` | `[留]` |
| 片内 FMC | `fmc.*` | `[留]` 仅 App/Meta/Boot |
| GT32 薄驱动 | `gt32_raw.*` | `[增]` |
| 板级 SPI 引脚 | `board_gt32.*` | `[增]` |

安装入口名可仍为 `Boot_RunInstallStage()` / `Boot_InstallOnce()`。  
`IMAGE_READY`：`OtaMeta_CommitImageReady()`。  
Secondary 预擦：`GT32_EraseSecondarySlot()`（替代 `GD32_EraseFlashSafe` 对 Backup 的调用）。

---

## 7. 终稿前待钉死清单（提纲 → 正文）

1. `APP_IMAGE_MAX` 精确字节数  
2. Meta 物理擦除：1 KiB×1 页还是 2 KiB 内两页  
3. LastGood：安装前强制成功 vs 失败可降级；BKP2 耗尽是否自动恢复（策略 A/B）  
4. 头 ACK 后等 C：30 s 是否上调  
5. 串口波特率是否仍 9600  
6. SPI 引脚与时钟分频  
7. 文件名前缀是否仍 `cardreader`  
8. 是否在 Meta 增加 `board_id`  
9. 外挂 `0x40000` meta 镜像做不做（建议 v1 **不做**，减复杂度）

---

## 8. 修订结论

> **行为规范以 R10 为唯一骨架；存储从「片内 Backup」改为「GT32 Secondary + 可选 LastGood」；片内改为 Boot 16 KiB + App 110 KiB + Meta 2 KiB；YMODEM/DMA/三旗标/BKP/安装出口/跳转交接尽量原样保留。**

下一步：按本提纲把 R10 正文替换分区与擦写段落，生成 `YMODEM-GT32-D1-R02` 完整落地稿，或按 §6 开始改 `ota_layout.h` + `gt32_raw` 移植。

---

## 9. 终稿待钉死项 — 已拍板（2026-09-11）

| # | 项 | 决议 |
| --- | --- | --- |
| 1 | `APP_IMAGE_MAX` | **112640**（110×1024，App 槽 `0x08004000`–`0x0801F7FF` 整段） |
| 2 | Meta 擦除 | **只擦 1 KiB 页** `@0x0801F800`–`0x0801FBFF`；`0x0801FC00`–`0x0801FFFF` 预留不用 |
| 3 | LastGood | 安装前备份失败 → **本轮安装失败，不擦 App**；BKP2 耗尽 → **v1 策略 B（只进 OTA）**；LastGood 自动恢复留二期 |
| 4 | 头 ACK 后等 C | **默认 30 s**；实板不够则以 AC-YM-05 登记上调 |
| 5 | 波特率 | **9600 8N1** |
| 6 | SPI 引脚/时钟 | `board_gt32.h` 占位；时钟先 **≤9 MHz**；引脚待原理图后补 |
| 7 | 文件名前缀 | **`cardreader-vX.Y.Z.bin`**（小写前缀 + 完整版本） |
| 8 | Meta `board_id` | **要**：固定 `0x0001`；保留区 4 字节；Meta CRC 覆盖含该字段 |
| 9 | 外挂 `0x40000` meta | **v1 不做**；仅片内 Meta 三旗标 |

状态：可进入 `YMODEM-GT32-D1-R02` 完整落地正文，或按 §6 开始代码骨架（SPI 引脚可后补）。
