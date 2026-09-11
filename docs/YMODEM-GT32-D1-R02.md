# ymodem bootloader 升级方案（GT32 新板）

适用版本：新板 BootLoader（含 GT32L32S0140 外挂）。正文版本：`YMODEM-GT32-D1-R02`（**R02.1 补丁**，2026-09-11）。状态：**完整落地终稿 + R02.1 补丁**。

本文是新板 BootLoader 升级的唯一行为规范，供实现核对与后续审核。范围限于**新板**（有 GT32L32S0140，**板卡从 v0.1 起**）的 BootLoader 及配套升级、出厂工具；App 日常通信可为 Modbus（或其它主机协议），本文只约定 App↔Boot 的 **AppOta C API（BKP）** 契约，**不规定 Modbus 功能码/寄存器映射**（Modbus FC 不在范围）。发送器本轮不改。旧板（无外挂）不在本文范围。

读者无需参照旧稿 `YMODEM-031-D1-R10`；本文自洽。实现策略：**优先移植** `CardReaderBootLoader-v0.3.1` / R10 `boot.c` 状态机到 GT32 布局（非协议绿场重写）；仓库路径仍 **待工程确认**。

### 相关手册（截至 2026-09-11）

| 器件/文档 | 引用 |
| --- | --- |
| GT32L32S0140 | **VER1.0I_N**（截至 2026-09-11 已知最新） |
| GD32F103 用户手册 | **截至 2026-09-11 最新版**（不编造假修订号；实现以工程确认的手册 PDF 为准） |

## 1. 设计目标

- 标准单文件 YMODEM 接收原始 App BIN，文件名携带版本。
- 单个 DMA 接收缓冲，串口 IDLE 中断交接数据。
- Secondary（GT32 外挂）在收数据前整区擦完；擦除函数一次完成，内部分段喂狗，不边擦边写。
- 下载写入 Secondary；接收信息在 RAM，传输过程不写 Meta。
- 传输与安装各最多 3 次，失败次数只存 RAM，复位后归零。
- 会话正常结束且 Meta 回读通过后，才提交 `IMAGE_READY`；之后才允许擦写 App。
- 安装前：向量 OK 则必须将当前 App 整槽备份到 LastGood（失败则本轮安装失败、不擦 App）；向量 NOT OK 则跳过 LastGood。
- App 校验通过、BKP2 清零并回读后，才提交 `APP_VALID`。
- 上电只根据片内三个 Meta 状态字选择 OTA、安装或跳转。出厂使用含有效 Meta 的片内 128 KiB 整包；GT32 出厂可空白。
- 不能确认 App 有效时不跳转；Boot 必须保留 OTA 入口，不以无响应空转代替升级。
- Boot 内仅链接 GT32 薄 SPI 驱动（读/页写/扇区擦/块擦/RDSR）；**不链接字库库**。
- 硬性禁止对 GT32 发 Chip Erase（`60h`/`C7h`）。

## 2. 总体流程

### 2.1 Flash / 外挂分区

分区固定，不随 BIN 长度移动。地址见 `ota_layout.h`。

#### 2.1.1 片内（GD32F103CB，128 KiB）

| 分区 | Flash 起始 | 结束（含） | 容量 | 用途 |
| --- | --- | --- | --- | --- |
| Boot | `0x08000000` | `0x08003FFF` | 16 KiB | BootLoader + YMODEM + gt32_raw |
| App | `0x08004000` | `0x0801F7FF` | 110 KiB | 运行中的 App |
| Meta/DATA | `0x0801F800` | `0x0801FFFF` | 2 KiB | 镜像信息与三旗标 |

说明：

- **无片内 Backup/DOWNLOAD。** Secondary 与 LastGood 均在 GT32。
- Meta 物理区域 2 KiB，但 **整页事务只擦写第一页** `0x0801F800`–`0x0801FBFF`（1 KiB）；`0x0801FC00`–`0x0801FFFF` 预留，v1 不使用、不擦写。
- 片内 FMC 中等密度页大小为 1 KiB。

#### 2.1.2 GT32L32S0140 用户区（`0x000000`–`0x07FFFF`，512 KiB）

| 外挂偏移 | 容量 | 用途 |
| --- | --- | --- |
| `0x00000` | 128 KiB | Secondary（YMODEM 落盘区） |
| `0x20000` | 128 KiB | LastGood（安装前备份当前 App） |
| `0x40000` 起 | 余下 | 配置 / 资源等；**v1 不做外挂 OTA meta**，主旗标仅以片内 Meta 为准 |

- Secondary 会被新 OTA 覆盖，不是旧版本回滚区；回滚由 LastGood 承担（v1 仅安装前备份，BKP2 耗尽不自动恢复，见 3.5 / 3.7）。
- OTA 只擦 Secondary / LastGood 所属扇区或块；**禁止 Chip Erase**。

### 2.2 Boot 启动

开备份域写、配看门狗后，**只读片内三个 Meta 状态字**。文件名、版本、Meta CRC、`board_id` 不参与选路。上电选路**不依赖** GT32；仅进入 OTA 收包或安装搬运时才初始化 SPI/GT32。

`IMAGE_READY` 与 `APP_VALID`：完整 `0x00000000` 才算置位，其它值（含 `0xFFFFFFFF`、半写）一律未置位，不补写、不看最低位。`MATE_OTA_FAIL`：仅 `0xFFFFFFFF` 为未置位，其它任何值（含 `0` 和半写）为置位，半写不补写成 `0`。

| 判断结果 | 下一步 |
| --- | --- |
| `IMAGE_READY` 未置位 | OTA，不跳 App |
| READY 置位，VALID 未置位，FAIL 未置位 | 安装阶段（3.5）。不开 UART、不发 C |
| READY 置位且 FAIL 置位 | OTA，不再安装这份 Secondary |
| READY、VALID 均置位，FAIL 未置位 | 再看 BKP1/BKP2 |
| VALID 与 FAIL 同时置位 | 不跳 App，OTA |

待安装优先于 BKP1 和跳转。安装时用 Meta 中的大小和镜像 CRC 校验 Secondary/App，这是操作数据，不是选路条件。

READY+VALID 且 FAIL 未置位之后：

1. BKP1 为 `0x3344`：消费为 `0xAABB`，进入 OTA。
2. 否则 `BKP2 >= 5`：进入 OTA，不再试启动同一 App。`0xFFFF` 等大于 5 的值同样禁止跳转，不能加一回卷后放行。**v1 不做 LastGood 自动恢复**（策略 B）；LastGood→App 自动回滚留二期。
3. `BKP2 < 5`：统一跳转（`BKP2++` → 回读 → 硬件交接 → 喂狗 → App）。计数或回读失败不得跳转。

BKP2 为 16 位裸数值，初值 0，最多 5 次连续未确认启动。不编码魔数、不识别旧 `0xB100`/`0xA55A`。App 在关键初始化成功后写 0 确认，不能刚进 main 或刚喂狗就清零。新镜像在提交 `APP_VALID` 之前将 BKP2 清 0 并回读；提交后不再清。传输失败路径不清 BKP2。发起 OTA 时由 App 清 BKP2（应用侧实现）。不使用 BKP3。

统一跳转还必须：等待发送结束并释放 RS485 方向；关闭 Boot 的 DMA、串口及其中断、SysTick 和其他计时源；清理 NVIC 与 SysTick/PendSV 挂起；关中断后设 App `VTOR`、同步屏障、交接主栈后进入 App 复位入口。看门狗交接前喂狗并保持运行，由 App 接手。不能只改函数指针跳转。

`LOAD_A` 因不够格（无 VALID、`BKP2 >= 5`、镜像检查失败、加一回读失败）而返回：上电选路进入 OTA。已经完成硬件交接后返回，或安装成功后的跳转失败：立即软件复位，不进传输 15 秒函数。

GT32 probe 失败：若处于待安装 → 走安装失败出口；若处于 OTA → 无法擦写 Secondary，按传输/介质失败处理（归入对应阶段计数，见 3.6）。

### 2.3 一次正常 OTA

开始前按 5.1.4 让总线上其他设备断电或隔离，直到新 App 启动确认。

1. App 调用 `AppOta_RequestUpgrade()`（清 BKP2、置 BKP1=`0x3344`，回读后复位）。主机协议（如 Modbus）仅需在 ACK 主机后调用该 API；**本方案不规定 Modbus FC**。
2. Boot 发 `C`，收并检查文件头。
3. 头有效后先 ACK，再阻塞擦完整个 Secondary（GT32，内部按扇区/块分段，段尾喂狗）。擦除期间不回主循环、不解析输入。擦除时长不计入两个协议计时。
4. 擦完发 `C` 且不 `RxRelease()`；之后只逐包写入，不再擦 Secondary。
5. 收完声明字节，长度、向量、整镜像 CRC32 通过。
6. 双 EOT、结束空 Block0、最终 ACK，固定 5 秒收尾后关闭会话。
7. 整页事务提交片内 Meta 并确认 `IMAGE_READY`（3.4.5）。传输过程不写 Meta。三次事务失败则留 Boot 发 `C`，不装 App、不复位。
8. 提交成功后进入安装阶段（3.5）：复核 Secondary → 向量 OK 则整槽 112640 复制到 LastGood（必须成功，否则本轮失败、不擦 App；向量 NOT OK 则跳过）→ 擦 App → Secondary→App → 校验 → BKP2 清 0 → 提交 `APP_VALID` → 统一跳转。最多 3 次；第 1、2 次失败等 5 秒从头再装；第 3 次条件满足时提交 `MATE_OTA_FAIL`。
9. 跳转返回立即软件复位。新 App 在关键初始化成功后调用 `AppOta_ConfirmRunning()`。上位机确认 App 正常（业务应答）及目标版本后，才认定升级成功。

传输与安装各 3 次，不共用额度。

## 3. 详细设计

### 3.1 固件文件与接收信息

OTA 文件名：`cardreader-v<主>.<次>.<修订>.bin`，例如 `cardreader-v0.3.1.bin`。

- 只接收小写 `cardreader` 前缀且完整格式正确的名称。
- 版本从文件名提取，仅记录，无签名、无防降级。
- 内容为原始 App BIN，不追加 Meta 或其它封装。
- 实际大小 `8～112640` 字节（`APP_IMAGE_MAX = 112640`，即 110×1024，等于 App 槽 `0x08004000`–`0x0801F7FF` 整段）。
- 文件名、版本、声明大小、累计字节、包号、运行 CRC32 保存在 RAM。
- 写入 Meta 的字符串编码见 3.4.2。
- Meta 含固定 `board_id = 0x0001`（小端 uint32）。接收路径不要求文件头携带 board_id；提交 Meta 时由 Boot 写入该常量。误刷防护由文件名前缀与 board_id 共同约束；v1 只发新板包。

### 3.2 DMA 单缓冲接收

串口 **9600 8N1**，RS485 半双工。DMA 单个 **2048 字节静态缓冲**（`static`，**禁止**在栈上开大 VLA/大数组），普通模式、按字节传输，不用循环或半传输交接。

**Boot 栈预算：** 链接脚本 / 启动文件中 Boot 主栈 ≥ **`0x800`（2048 字节）**。

1. IDLE 或 DMA 满缓冲进入 `Usart0_RxRoundEnd()`；**ISR 内仅冻结本轮标志**（就绪/长度/停 DMA 等）。中断内**禁止**：Flash 擦写/编程、YMODEM 解析、GT32 SPI 操作、阻塞发送。
2. `实收长度 = 2048 − 剩余计数`。长度为 0 的 IDLE 不停 DMA、不置就绪，只清 IDLE 标志。
3. 长度大于 0 时置就绪并停 DMA。本轮已发布后又进中断：只保证 DMA 保持停止，不覆盖已冻结轮次。
4. 主循环只按本次 `length` 取数，不扫整块缓冲。
5. `RxRelease()`：清标志，重装地址和计数，再开 DMA。不清零 2048 字节缓冲。
6. 持有冻结轮次的应答走 `Boot_OtaReply()`（先 Release 再发）；催发、开场 C、擦完 C 走 `Boot_OtaSendPair()`（只发，不 Release）。以串口 TC 确认发送完成后恢复接收方向。

`RxRelease()` 假定 CHEN 已由本轮发布清掉。同一轮只交接一次。满 2048 字节整轮丢弃并重传，不截取其中 133/1029 字节冒充完整帧。

#### 3.2.1 职责划分

`boot.c` 是唯一 OTA 阶段机：读 DMA、释放轮次、擦除、发 ACK/NAK/C/CAN、收尾、重试或记失败。跨循环只保留 `BootOtaPhase_t`。每轮最多解析一轮冻结输入。

`drv_usart` 独占 USART/DMA，以 `RxPeek()`/`RxRelease()` 只向 `boot.c` 交接。`drv_ymodem` 不得重装 DMA，只返回帧判定，不发送、不推进阶段。

`gt32_raw` 独占 SPI/GT32 底层命令；`boot.c` / `ota_image.c` 经它访问 Secondary 与 LastGood，不直接操作 SPI 寄存器。

`Usart_SendBytes()` 阻塞至 TC 成功并恢复接收方向。整帧预算 = 5 ms + 字节数 × 3 ms。每条响应最多 3 次，三次失败立即软件复位，不追加 CAN。ACK+C、CAN+CAN 按完整两字节一起重试。本地 TC 成功不能证明对端已收到。

不启用串口错误中断：一包是否可用只由帧长、包号反码和 CRC16 判定。本板 RS485 的 DE 与 /RE 相连，发送时接收器被关断，无需过滤本机回波。

长度或 CRC 不符：立即丢弃并 NAK。迟到尾部作为下一次 IDLE 的独立数据，不与已丢弃数据拼接。

### 3.3 YMODEM 会话

#### 3.3.1 帧校验

| 帧类型 | 数据区 | 完整帧长 |
| --- | --- | --- |
| SOH | 128 | 133 |
| STX | 1024 | 1029 |

先比 DMA 实收长度与期望长度，再读包号、反码、CRC16。长度不对立即 NAK，不拼下一轮、不等 5 秒。

无效包不写 Secondary、不累计、不推进包号。有效重复包只补 ACK。首个数据包号为 1，按 8 位递增，`255 → 0 → 1`。数据阶段的包号 0 仍是数据包，必须结合业务阶段判断文件头或结束空 Block0。

CRC16 覆盖整包数据区（含末包填充）。写 GT32 和镜像 CRC32 只计声明长度内的有效字节。超过声明长度的新包不得写入。

固件长度非 4 字节整倍时，最后编程字剩余字节补 `0xFF`；Secondary 与 App 同一规则。补齐不计入大小和镜像 CRC，不得越界读源或越过分区。这与 YMODEM 包填充不是一事。

GT32 页编程粒度为 256 字节：不足一页时，按芯片页写规则仅编程本页内有效范围（或读-改-写同页其余字节为 `0xFF` 后整页程序——实现任选其一，但不得改写声明长度之外且已属于下一镜像逻辑区的旧数据语义；推荐预擦后页内未用字节保持 `0xFF`，直接页程序时对不足页补 `0xFF`）。Secondary 预擦已保证槽内为 `0xFF`。

结束空 Block0 仍是完整数据帧。EOT、CAN 独立解析。连续两个 CAN 取消会话；单个 CAN 不另建计数或超时。

保留单缓冲、不拼残包。包内意外停顿按错误重传。需要同一缓冲分次收包时另行设计。

#### 3.3.2 Secondary 预擦除

文件头有效后先在 RAM 保存信息并 ACK，此时不发允许传数据的 C。主循环一次调用 `GT32_EraseSecondarySlot()` 擦完整个 Secondary 槽（128 KiB，外挂偏移 `0x00000` 起）；**固定擦满 Secondary 槽**，不按声明大小部分擦，避免空洞旧数据干扰。内部按扇区/块分段，段尾喂狗。擦除返回前不 Peek、不应答。

底层命令见 3.8：允许 `20h` 扇区擦（4 KiB）与 `D8h` 块擦（64 KiB）加速；**禁止 Chip Erase（`60h`/`C7h`）**。某扇区/块失败立即结束本次传输，Close→Open 后走传输失败函数。

擦除时长按 3.3.4 从两个协议计时中扣除。擦完发 C 且不 Release。预擦阻塞，主循环不 Peek；按发送端约定这段时间只能空闲或重发 Block0。若擦除期间已冻住相同完整 Block0，进入 `RECEIVE_DATA` 且尚未收首包时补 ACK+C。不同文件头不得覆盖 RAM 信息，也不重擦。

擦除时长按实板登记；手册/样机数值写入 AC-YM-05。发送端头 ACK 后等 C 的时限**默认 30 秒**；实板不够则以 AC-YM-05 登记后双方一起上调。

#### 3.3.3 握手与收尾

| 接收阶段 | Boot 动作 |
| --- | --- |
| 等待文件头 | 发 C |
| 有效首个 Block0 | 查文件名和大小，保存 RAM，ACK；`Boot()` 看见 `ERASE_SECONDARY` 后 `continue` 到擦除，不插入 10 ms |
| Secondary 擦完 | 发 C，不 Release |
| 期待的数据包 | 校验写入，Release 后 ACK |
| 预擦除中 | 阻塞，不解析 |
| Secondary 已擦完、尚未收首包，收到相同有效文件头 | 补 ACK+C，不重擦 |
| 上一已接受包的有效重传 | 补 ACK，不重复写 |
| 第一次 EOT | 完整镜像检查通过后 NAK |
| 第二次 EOT | ACK+C，等结束空 Block0 |
| 等空头时再收到 EOT | 再 ACK+C |
| 有效结束空 Block0 | 最终 ACK，以首次 TC 成功为基准进入固定 5 秒收尾 |
| 收尾窗口内重复有效空头 | 补 ACK，不续期、不重复提交 |
| 收尾结束 | 关闭会话，之后才写 Meta |

重复文件头必须与已接受头一致。完整镜像检查：接收长度=声明长度、向量合法、Secondary 独立 CRC32=过程累计 CRC32。失败不得关成成功会话，不得提交 `IMAGE_READY`。

收尾固定 5 秒，不因补 ACK 续期；非空头 NAK。主循环先消费再检查截止；进入 `SESSION_DONE` 后不再 Peek。15 分钟总期限优先。最终 ACK 只表示文件接收结束。发送器按 5 秒重发最终 ACK 可能晚于 Boot 截止，本版接受发送器报超时而设备继续关闭，不改发送器、不窗口外补答。

#### 3.3.4 两个协议计时

1 ms SysTick、32 位差值，处理回卷。总期限在首次发 C 前记录；阶段基准在该次 TC 成功后记录。擦除窗口显式扣除：擦前记毫秒，返回后把进展基线刷到当前，并把总会话基准前移该增量。不增加第三个协议计时器。

| 计时 | 时长 | 到期 |
| --- | --- | --- |
| 进展 | 5 秒 | 未完成则按阶段催发；已在收尾则按固定截止关闭。催发走 `SendPair()`，不 Release |
| 总会话 | 15 分钟 | 未进 `DONE_GRACE`：关闭并按传输失败。已进收尾：正常关闭 |

有效新数据或合法握手推进才刷新进展；整区预擦完成也刷新。无效、重复包、重复头/EOT、收尾补 ACK 不刷新。任何刷新都不重置总会话。先消费再催发。`ERASE_SECONDARY` 与 `SESSION_DONE` 不催发。

| 等待位置 | 5 秒动作 |
| --- | --- |
| 首个文件头 | 重发 C |
| 预擦除 | 不催发；擦完发 C 并刷新进展 |
| 已擦完、尚未首包 | 重发 C |
| 后续数据，或已收齐等第一次 EOT | NAK |
| 等第二次 EOT | NAK |
| 等结束空 Block0 | 重发 C |
| 收尾 | 正常关闭 |

#### 3.3.5 终止与再同步

- 主动终止（镜像/Flash/GT32/未收尾总超时）：发 CAN+CAN，TC 后释放方向，清本次接收上下文。
- 对端双 CAN：结束本次尝试，不把取消当残包 NAK。
- UART 三次发送失败：立即复位，不追加 CAN。
- 普通异常关闭不清传输次数、BKP2、已有 Meta，然后走 3.6 传输失败（先等 15 秒）。
- 发送端收到双 CAN 后必须开新会话，不能把随后的 C 当成旧包 ACK。
- Meta 提交失败不向已结束会话发 CAN，不复位，重新发 C。提交成功后进入安装。

### 3.4 Meta（片内）

#### 3.4.1 布局

物理基址 `0x0801F800`。整页事务只覆盖第一页 `0x0801F800`–`0x0801FBFF`（1 KiB）。有效逻辑记录前 88 字节（含 `board_id`），其余本页字节保持 `0xFF`；第二页 `0x0801FC00`–`0x0801FFFF` 预留不用。按固定偏移序列化，不直接写带填充的 C 结构体。

| 字段 | 偏移（相对 Meta 基址） | 长度 | 编码 |
| --- | --- | --- | --- |
| 文件名 | `0x00` | 32 | ASCII，末尾补 0x00 |
| 版本 | `0x20` | 16 | ASCII，末尾补 0x00 |
| 大小 | `0x30` | 16 | 十进制 ASCII，末尾补 0x00 |
| 固件 CRC32 | `0x40` | 4 | 小端 |
| Meta CRC32 | `0x44` | 4 | 小端 |
| IMAGE_READY | `0x48` | 4 | 独立状态字 |
| APP_VALID | `0x4C` | 4 | 独立状态字 |
| MATE_OTA_FAIL | `0x50` | 4 | 独立状态字 |
| board_id | `0x54` | 4 | 小端 uint32，固定 `0x0001` |
| 保留 | `0x58` | 本页余下 | `0xFF` |

说明：文件名/版本/大小/固件 CRC/Meta CRC/三旗标的相对偏移与旧板 84 字节布局一致；在原保留区起始处插入 `board_id`（4 字节），总有效头扩展为 88 字节。

#### 3.4.2 字符串

长度含终止符。未用字节填 `0x00`，不是字符 `0`。禁止越过字段边界找结尾。

| 字段 | 示例 | 最大有效文本 |
| --- | --- | --- |
| 文件名 | `cardreader-v0.3.1.bin`（21 字节） | 31 |
| 版本 | `v0.3.1`（6 字节） | 15 |
| 大小 | `112640`（6 字节） | 15，解析后须在 `8～112640` |

版本须与文件名中的版本一致。大小只允许十进制，检查溢出。写入 Meta 时按 RAM 已接受内容编码，不再当选路条件。`board_id` 由 Boot 写入常量 `0x0001`，不从文件名解析。

#### 3.4.3 两个 CRC32

上电路由不读、不校验 Meta CRC；该字段仍须写入，供出厂核对。安装和 `LOAD_A` 读取大小、镜像 CRC 时不再验 Meta CRC：`IMAGE_READY` 提交时已编程并回读信息区。

| 项 | 对象 | 不含 |
| --- | --- | --- |
| 固件 CRC32 | 原始 App 实际字节 | YMODEM 填充、分区补齐、Boot、另一份镜像、Meta |
| Meta CRC32 | 偏移 `0x00～0x43`（68 字节）**与** `0x54～0x57`（4 字节 `board_id`），合计 **72 字节** | Meta CRC 自身、三个状态字、`0x58` 起保留区 |

算法：反射、多项式 `0xEDB88320`，初值/终值异或 `0xFFFFFFFF`。样例 ASCII `123456789` → `0xCBF43926`。实现向 CRC32 引擎按**升序地址**依次喂入：先偏移 `0x00..0x43`（68 字节），再 `0x54..0x57`（4 字节 `board_id`），合计 72 字节（间断拼接，同一算法）。状态字不纳入 Meta CRC，后续提交 VALID/FAIL 不重算、不重擦 Meta。CRC 不是签名。

#### 3.4.4 状态字

| 字 | 未置位 | 置位 |
| --- | --- | --- |
| IMAGE_READY | 非完整 `0` | 仅完整 `0`。Secondary 有效、会话结束、Meta 回读通过，允许安装 |
| APP_VALID | 非完整 `0` | 仅完整 `0`。App 已安装且校验通过 |
| MATE_OTA_FAIL | 仅 `0xFFFFFFFF` | 其它任何值（含 `0` 和半写）。本轮安装持续失败，重启后改走新 OTA |

READY/VALID 是镜像事实，不是业务健康。FAIL 未置位不证明上次正常掉电。禁止 VALID 与 FAIL 同时置位。FAIL 半写视为已置位：不跳 App、不待安装，上电走 OTA。

#### 3.4.5 IMAGE_READY 提交

传输全程不写 Meta。会话关闭后用 RAM 一次性构造记录落盘，不是读改写。擦 Meta 页不作废 App 向量。

以**整页事务**提交，最多 3 次。每次：

1. 擦 Meta 第一页 `0x0801F800`–`0x0801FBFF`（仅 1 KiB；不擦第二页预留区）。
2. 编程偏移 `0x00～0x47` 的 72 字节（信息区 + Meta CRC）并回读；编程偏移 `0x54～0x57` 的 `board_id=0x0001` 并回读。也可一次编程连续 `0x00～0x57`（88 字节），其中三旗标位置写入 `0xFFFFFFFF`（与擦后一致）。
3. 向相对偏移 `0x48`（绝对 `0x0801F848`）写完整 `0` 并回读。

擦后三旗标均为 `0xFFFFFFFF`。不编程 VALID/FAIL。

任一步失败：结束本轮，不得在脏页或半写上续写；下一轮从整页擦除开始。字已是目标值则成功，不重复编程。半写当本轮失败，禁止补写。

三次都失败：不复位、不发 CAN、不等 15 秒、不计传输/安装次数、不装 App。Secondary 仍在，但没有 READY，不得擦 App。留 Boot 发 C；新文件头 ACK 之后才擦 Secondary。

信息区已是本次 RAM 内容、`board_id` 已为 `0x0001`、READY 已为 `0`、且 FAIL 为 `0xFFFFFFFF`：视为已提交，不重擦。FAIL 已置位（含半写）时即使信息区相同也必须整页重建以清掉失败旗标。只有下一份 Secondary 验证通过且会话正常结束，才允许重建 Meta。

#### 3.4.6 状态字编程

三字都只在自己的 4 字节上从 `0xFFFFFFFF` 写成 `0x00000000`。

| 回读 | READY / VALID | FAIL |
| --- | --- | --- |
| 完整 `0` | 已置位，不重复编程 | 已置位，不重复编程 |
| 完整 `0xFFFFFFFF` | 未置位。READY 按 3.4.5 本轮写一次；VALID 由安装阶段提交 | 未置位，由安装阶段提交 |
| 其它（半写） | 当未置位，不补写 | 当已置位，不补写 |

跨复位不恢复半次提交。VALID 与 FAIL 互斥：一方已置位时不得写另一方。`CommitOtaFail` 见半写 FAIL 视为已成功。

### 3.5 Secondary 安装到 App

仅当 READY 已提交、VALID 与 FAIL 均未置位，才进入安装。上电待安装先设 NVIC 分组 3 再开 SysTick（供 5 秒延时），不开 UART、不发 C。`SESSION_DONE` 在 `IMAGE_READY` 成功后进入同一安装阶段。

安装失败次数独立 RAM 计数，最多 3；`StartAttempt` 的 memset 不得清它。进入 `SESSION_DONE` 时传输与安装计数都清零。复位后归零。不承诺看门狗在一次安装中途反复复位也能累到 3 次。

每次尝试（`Boot_InstallOnce()`）顺序：

1. **已是 `APP_VALID`**：不再擦 App、不清 BKP2、不写 FAIL，直接走跳转（与统一跳转路径相同）。
2. **不是待安装**：结束，不计数。
3. **`ReadImageInfo` + 复核 Secondary**：取大小和镜像 CRC，校验 Secondary（长度、向量、CRC32）。失败不擦 App，计本轮安装失败。
4. **LastGood 备份（“当前 App 有效”判定）**：
   - **向量 OK 条件**（仅此，**不**要求 Meta `APP_VALID`——READY 提交会清 VALID）：MSP 在 RAM 且 8 字节对齐，**并且** Reset 带 Thumb 位且落在 App 链接范围（`0x08004000` 起、不超过 `APP_IMAGE_MAX`）。
   - 向量 **OK** → App 向量可信 → **必须**成功完成整槽 LastGood 拷贝（`0x08004000` 起 **112640** 字节到 GT32 `0x20000`）；固定整槽长度，不依赖旧 App 的 Meta 大小字段。拷贝失败 = **本轮安装失败**，**不得擦 App**，不得继续安装。
   - 向量 **NOT OK** → **跳过** LastGood（无可信内容可保），继续安装；若可能，在 RAM 记录原因/日志。此时无回滚可用。
5. **擦 App**（片内 FMC，每段最多 30 KiB、段尾喂狗）→ 从 Secondary 搬运到 App：先写主体再写向量；Secondary 只读。不写 Boot 或 Meta。
6. **从 App 检查**长度、向量、CRC32。
7. **BKP2 写成 0 并回读**；失败不提交 VALID、不跳转。
8. **提交 `APP_VALID`** 并确认已置位。
9. **统一跳转**：`BKP2 < 5` 才 `+1` 回读，然后硬件交接。提交 VALID 之后不再清 BKP2。

BKP2 清零必须早于 VALID。两步之间复位仍待安装。VALID 已提交后复位，不得继承旧 App 耗尽的启动计数。

重试不重下文件、不重建 Meta、不保存搬运偏移；每次先复核 Secondary 再从头覆盖。向量检查（与上步「当前 App 有效」及镜像校验共用）：MSP 在 RAM 且 8 字节对齐；Reset 带 Thumb 位且落在 App 链接地址范围（`0x08004000` 起、不超过 `APP_IMAGE_MAX`）。**不**以 Meta `APP_VALID` 作为 LastGood 前置条件。只检查向量不能代替整镜像 CRC。

| 安装失败 | 行为 |
| --- | --- |
| 第 1、2 次 | 等 5000 ms（喂狗），确认仍待安装，从头再装。不发 C、不擦 Secondary、不改 Meta |
| 第 3 次且仍待安装 | `CommitOtaFail()`。成功则再等 5000 ms 复位；复位后 2.2 见 FAIL 则 OTA |
| `CommitOtaFail` 失败 | 不复位、不跳未经确认的 App。留 Boot 发 C。若 FAIL 仍为 `0xFFFFFFFF`，复位后仍待安装；若已半写，FAIL 视为已置位，上电走 OTA、不再擦 App |
| `CommitImageReady` 失败 | 不是安装失败，见 3.4.5 |
| 已 VALID 之后的 `LOAD_A` 失败 | 立即复位，不计安装次数、不写 FAIL |
| LastGood 备份失败 | 本轮安装失败（计入上表次数），**不擦 App** |
| GT32 probe/读写失败（安装阶段） | 本轮安装失败 |

### 3.6 失败出口

不得混用。

| 出口 | 范围 | 行为 |
| --- | --- | --- |
| UART 本地发送 | 同一响应连续 3 次无 TC | 立即复位。不发 CAN，不等待，不计传输/安装 |
| 传输尝试 | 协议错误、Secondary 擦写、镜像校验、取消、未进收尾的总会话超时、OTA 阶段 GT32 介质失败；不含 Meta 落盘 | 每次先等 15 秒。第 1、2 次从 Block0 重收；第 3 次等完复位。不置 FAIL |
| Meta 擦写 | `IMAGE_READY` 整页事务 | 最多 3 次事务，脏页不续写。用尽不复位、不装 App，留 Boot 发 C |
| 安装尝试 | READY 之后的复核、LastGood 备份、擦写、搬运、校验、BKP2 清零、`CommitAppValid`、安装阶段 GT32 失败 | 第 1、2 次等 5 秒从头装；第 3 次写 FAIL 后延时复位；FAIL 写不进则留 OTA |
| 跳转失败 | 硬件交接后 `LOAD_A` 返回；安装成功后的跳转失败 | 立即复位。上电「不够格跳转」返回则进 OTA，不是本出口 |

单包 NAK、重复包、5 秒催发不累计阶段失败。传输与安装各 3 次，是两个阶段各自上限，不是 6 次完整 OTA。

介质类（GT32 probe 失败、扇区擦写超时、WIP 异常）→ 归入**传输尝试**（收包阶段）或**安装尝试**（搬运/备份阶段），不发明第六套计数。

复位后 RAM 计数从 0 开始。阶段内重试不清零。`SESSION_DONE` 清零两个计数后再提交 Meta。RAM 归零不等于清 FAIL 或清 BKP2。

### 3.7 LastGood（v1）

| 触发 | 行为 |
| --- | --- |
| 安装前 | 见 3.5 步骤 4：向量 OK 则必须整槽备份成功，失败则本轮安装失败、不擦 App；向量 NOT OK 则跳过 LastGood |
| 运行中 BKP2 耗尽 | **v1 策略 B**：只进 OTA，**不**自动 LastGood→App 恢复 |
| LastGood 区无效 / 跳过备份 | 视为无回滚 |

LastGood 不参与 YMODEM；不参与三旗标选路主路径。自动恢复留二期。

### 3.8 GT32 薄驱动（`gt32_raw`）

Boot **不得**链接字库或其它 GT32 高层库；仅链接本薄驱动。

| 命令 | 码 | 用途 |
| --- | --- | --- |
| Read Data | `03h` | 读 Secondary / LastGood |
| Fast Read | `0Bh` | 可选加速读 |
| Write Enable | `06h` | 页写/擦除前 |
| Write Disable | `04h` | 操作后可选 |
| Page Program | `02h` | 页写，页长 256 B |
| Sector Erase | `20h` | 4 KiB 扇区擦 |
| Block Erase 64KB | `D8h` | 允许用于 Secondary 整槽预擦加速 |
| Read Status (RDSR) | `05h` | 轮询 WIP |

**禁止**：Chip Erase `60h` / `C7h`。OTA 与安装路径不得调用。

SPI 引脚与片选：见 `board_gt32.h` / 原理图（占位，待原理图确认后补全；**不得编造引脚号**）。SPI 时钟**初期 ≤ 9 MHz**；实板稳定后再评估提高。初始化失败返回明确错误码，由上层归入传输或安装失败出口。

**MCU 侧 WIP 轮询超时（硬限）：**

| 操作 | 超时 |
| --- | --- |
| Page Program WIP | 轮询至清除，超时 **100 ms** |
| Sector Erase 4KB WIP | **500 ms** |
| Block Erase 64KB WIP | **2000 ms** |

Secondary 整槽预擦墙钟时间须与发送端「头 ACK 后等 C」默认 **30 s** 兼容；实板时长登记于 AC-YM-05。若实测超过，**设计变更上调发送端等待**——不得在代码中静默改协议。

`GT32_EraseSecondarySlot()`：擦外挂 `0x00000` 起 128 KiB（可用两次 `D8h` 或若干 `20h`），段间喂狗。  
LastGood 写入前按需擦对应扇区/块（同样禁止 CE）。

## 4. 出厂完整固件包

PC 读取 Boot BIN 与原始 App BIN，生成固定 **片内 128 KiB** 整包。

1. Boot ≤ 16 KiB；检查 App 文件名、大小（`8～112640`）、向量（Reset 落在 `0x08004000` 范围）。超 16 KiB 由体积优化处理，本文不另开分支。
2. 131072 字节初值 `0xFF`。
3. 写入 Boot（`0x00000`）、App（包内偏移 `0x04000`）；**无片内 Secondary 副本**。
4. 按 3.4 构造 Meta（基址对应包内偏移 `0x1F800`）：含 `board_id=0x0001`，按 3.4.3 计算 Meta CRC。
5. 出厂：READY=`0`，VALID=`0`，FAIL=`0xFFFFFFFF`，`board_id=0x0001`。
6. 反向解析核对偏移、填充、向量、字符串、两个 CRC、`board_id`。
7. 从 `0x08000000` 烧录并回读后再启动。

**GT32**：出厂可全 `0xFF`（空白）；或产线另写资源区。Secondary/LastGood 建议空。不在工厂片内包中嵌入外挂内容。

字符串字段内部补 `0x00`；分区空余和 Meta 保留区补 `0xFF`。Meta 文件名是原始 App 名。工厂包建议 `factory-cardreader-v….bin`，**禁止用 YMODEM 发送 128 KiB 工厂整包**。

产线 BKP1=`0xAABB`、BKP2=0。烧录中断损坏 Boot 只能用烧录器重刷。

可选：工厂工具另生成 GT32 资源 bin；验收与片内包分开回读。

## 5. 参数与验收

### 5.1.1 Boot 参数

| 项目 | 取值 |
| --- | --- |
| 串口 | 9600 8N1，RS485 半双工，发送以 TC 为准 |
| DMA | 单 2048 字节普通模式；零字节 IDLE 不停 DMA；Release 不清缓冲 |
| 应答 | 消费后 `Reply()`；催发/开场 C/擦完 C 走 `SendPair()` |
| 发送预算 | 5 ms + 字节数 × 3 ms；每响应最多 3 次，失败立即复位 |
| Secondary 擦除 | 头 ACK 后同轮整槽 128 KiB 擦完；`20h`/`D8h`，禁止 CE；段尾喂狗 |
| 进展 / 总会话 / 收尾 | 5000 ms / 900000 ms / 最终 ACK TC 后固定 5000 ms |
| 看门狗 | FWDGT，IRC40K，div256，重装 4095，标称约 26.2 s |
| 喂狗 | 主循环每轮、长操作前、擦除每段后、失败等待期间。不在中断无条件喂狗 |
| 传输失败 | 15 s；第 1、2 次重收，第 3 次复位。`SESSION_DONE` 清零传输/安装计数 |
| Meta | 基址 `0x0801F800`；只擦 1 KiB 页；整页事务最多 3 次；失败留 OTA；`board_id=0x0001` |
| Flash 等待 | 切 108 MHz 前 `WSEN` + 2 wait state |
| App 链接 | `0x08004000`；`APP_IMAGE_MAX = 112640` |
| `Usart_Close` | `usart0_open==0` 立即返回 |
| 待安装恢复 | NVIC 分组 3，只开 SysTick，不开 UART |
| 安装 | 独立 3 次；间隔 5000 ms；第 3 次写 FAIL；LastGood 备份强制成功 |
| BKP1 | 请求 `0x3344`，已消费 `0xAABB` |
| BKP2 | 裸数值，门限 5；VALID 前清 0；跳转前 `<5` 才 +1；≥5 仅进 OTA（v1 无自动恢复） |
| 出厂寄存器 | BKP1=`0xAABB`，BKP2=0；不用 BKP3 |
| SPI | 引脚见 `board_gt32.h` / 原理图；时钟初期 ≤ 9 MHz |
| DMA RX 缓冲 | **static 2048** 字节（非栈 VLA） |
| Boot 栈 | ≥ **`0x800`（2048）** |
| GT32 WIP 超时 | PP **100 ms**；SE 4KB **500 ms**；BE 64KB **2000 ms** |
| 板卡 | **从 v0.1 起** |
| App↔Boot | `AppOta_RequestUpgrade` / `AppOta_ConfirmRunning`（§5.1.3）；**不做** Modbus FC 映射 |

看门狗按 IRC40K 标称 40 kHz，非精密时间。不能连续擦完 128 KiB Secondary 才喂狗。

### 5.1.2 发送端等待

| 位置 | 时限 |
| --- | --- |
| 开始前等 C | 30 s |
| 文件头后等 ACK | 5 s |
| 头 ACK 后等允许传数据的 C | 从 ACK 起 **30 s**（默认；实板不够按 AC-YM-05 上调），不与上一 5 s 共用 |
| 数据包 / EOT / 空 Block0 | 每次 5 s，超时重发当前项 |

头 ACK 后等 C 必须独立默认 30 秒。最终 ACK 的 5 秒重发不保证落在 Boot 窗口内。

### 5.1.3 App↔Boot 契约（AppOta C API；Modbus FC 不在范围）

Boot 侧仍只读 BKP1/BKP2（语义不变）。App 链接下列头文件中的契约；实现写备份域并复位。**本方案不设计 Modbus 寄存器/功能码映射**；主机协议（含日后 Modbus）在 ACK 主机后调用 API 即可。

```c
/* app_ota.h — App links this; implementation writes BKP + reset */
int AppOta_RequestUpgrade(void);   /* BKP2=0, BKP1=0x3344, verify, system reset; return 0 only if reset not taken */
int AppOta_ConfirmRunning(void); /* after critical init OK: BKP1=0xAABB, BKP2=0, verify; return 0 on OK */
```

- `AppOta_RequestUpgrade()`：等价原 EnterOta——BKP2=0、BKP1=`0x3344`，回读校验后系统复位；若复位已发生则调用方通常不可见返回；仅在复位未执行时返回非成功语义（实现约定：成功路径以复位为准，`return 0` 仅当复位未发生）。
- `AppOta_ConfirmRunning()`：关键初始化成功后调用——BKP1=`0xAABB`、BKP2=0 并回读；成功返回 0。不能刚进 `main` 或刚喂狗就调用。
- 初始化失败不调用 Confirm、不清除 BKP2。
- OTA 期间（设备进入 Boot 后）无业务从站；总线交给 YMODEM。
- 新 Boot 的 BKP2 规则与旧板旧 App 确认写法不兼容，不得混用发布。

### 5.1.4 RS485 与升级成功

OTA 期间同一总线仅上位机与目标设备。该条件覆盖传输、重试、复位、收尾、安装，直到 App 正常运行。Boot 因异常自动进 OTA 时同样适用。

上位机收到最终 ACK 只显示传输完成；确认 App 业务应答和目标版本后才显示升级成功并恢复其他设备。Boot 不增加成功应答帧。**最终 ACK ≠ 升级成功。**

### 5.2 验收

| 编号 | 范围 | 必须覆盖 |
| --- | --- | --- |
| AC-YM-01 | 字符串与格式 | 完整文件名/版本、终止与补零、超长拒绝、十进制溢出、112640 接受与超容量拒绝；`board_id=0x0001` 写入与核对 |
| AC-YM-02 | Flash/GT32 末字节 | 非 4 倍数字长补 `0xFF`；Secondary 页写不越界；长度与镜像 CRC 不含补齐 |
| AC-YM-03 | Meta 布局 | 基址 `0x0801F800`；88 字节有效头；Meta CRC 覆盖 72 字节（`0x00～0x43` + `board_id`）；上电只看三旗标；只擦 1 KiB 页 |
| AC-YM-04 | Meta 擦写 | 整页事务最多 3 次，脏页不续写；用尽不复位、不装 App，发 C；半写不补写 |
| AC-YM-05 | Secondary 预擦 | 一次调用擦满 128 KiB；`20h`/`D8h`；禁止 CE；头 ACK 后同轮擦、不插入 10 ms；擦完才发 C；积压相同头补 ACK+C；失败立即退出；喂狗间隔与擦除时长实板登记 |
| AC-YM-06 | 协议与 DMA | 单缓冲不拼残包；Reply/SendPair 分工；SOH/STX、少收多收、重复包、双 CAN/EOT、空 Block0、包号回卷 |
| AC-YM-07 | 计时 | 5 s 阶段催发；擦除窗口显式扣除；先消费再催发 |
| AC-YM-08 | 发送端 | 初始 C 30 s，头 ACK 后等 C 默认 30 s，其余 5 s；预擦期间不得提前发数据 |
| AC-YM-09 | 再同步 | 本地发送 3 次失败立即复位；中途终止双 CAN；Meta 失败不向旧会话发 CAN；跳转失败立即复位 |
| AC-YM-10 | 看门狗 | 26.2 s 配置；主循环/段擦/15 s 与 5 s 等待喂狗；真实卡死能复位 |
| AC-YM-11 | 收尾 | 最终 ACK TC 后固定 5 s；不续期；`SESSION_DONE` 不再 Peek；关闭后才写 Meta |
| AC-YM-12 | 传输计数 | 最多 3 次；先等 15 s；`SESSION_DONE` 清零；Meta 失败不计传输次数 |
| AC-YM-13 | 复位与恢复 | RAM 计数复位归零；上电只看三旗标；待安装走安装不发 C；FAIL 已置位走 OTA |
| AC-YM-14 | 提交时序 | 关会话前不擦 Meta；回读前不提交 READY；READY 成功前不擦 App；LastGood 成功前不擦 App；BKP2 清 0 早于 VALID |
| AC-YM-15 | 掉电 | 接收中、Meta 事务中、LastGood 备份中、搬运中、BKP2 已清而 VALID 未提交、FAIL 提交中、GT32 写到一半 |
| AC-YM-16 | BKP2 | 0～4 允许 +1 后跳；≥5 进 OTA（v1 无 LastGood 自动恢复）；不识别旧魔数 |
| AC-YM-17 | 跳转交接 | 释放 RS485，关 DMA/串口/计时，清挂起，交接向量与栈；看门狗连续运行；VTOR=`0x08004000` |
| AC-YM-18 | 故障出口 | READY 三次失败留 OTA；FAIL 写不进留 OTA；FAIL 半写视为已置位，上电 OTA 不反复擦 App；传输 3 次复位后能否回 App 取决于三旗标 |
| AC-YM-19 | 出厂整包 | 片内 128 KiB（Boot+App+Meta），无片内 Secondary 副本；READY/VALID=0，FAIL=`0xFFFFFFFF`，`board_id=0x0001`；BKP1/BKP2 初值；首次按三旗标启动；GT32 可空白 |
| AC-YM-20 | 连续升级 | 其它设备隔离条件下完成一次及连续两次升级；最终 ACK 不等于升级成功 |
| AC-YM-21 | LastGood | 向量 OK 则整槽备份必须成功才允许擦 App；向量 NOT OK 则跳过 LastGood；不要求 Meta APP_VALID；备份失败不破坏当前 App；v1 不测 BKP2 耗尽自动恢复 |
| AC-YM-22 | GT32 缺失/SPI 失败 | probe 或读写失败时：OTA 阶段走传输失败；待安装走安装失败；**不破坏片内已有 App**；不上电选路因 GT32 缺失而误擦 App |

协议测试与 Keil 构建不能代替掉电注入、RS485 时序、看门狗、GT32 SPI 和 App 启动的实板验证。

## 6. 代码入口（审核对照）

行为以本文为准。实现主要落在：

| 职责 | 文件 |
| --- | --- |
| 上电选路：待安装 → BKP1 → `LOAD_A` → OTA | `code/USER/SYSTEM/main.c` |
| YMODEM 阶段机、安装阶段、BKP2、统一跳转 | `code/USER/DRIVERS/src/boot.c`、`boot.h` |
| Meta 布局编解码（含 `board_id`） | `ota_meta_codec.c` / `.h` |
| Meta 擦写与三旗标 | `ota_meta.c` / `.h` |
| Secondary/App 校验、LastGood 备份、先主体后向量的搬运 | `ota_image.c` / `.h` |
| 分区常量（片内 + GT32 偏移） | `ota_layout.h` |
| 帧判定（不发送、不碰 DMA） | `drv_ymodem.c` |
| DMA Peek/Release 与阻塞发送 | `drv_usart.c` |
| 1 ms 时基与 `SoftDelayMs` | `drv_time.c` |
| 片内 FMC（Boot/App/Meta） | `fmc.c` |
| GT32 薄驱动 | `gt32_raw.c` / `.h` |
| 板级 SPI 引脚与时钟 | `board_gt32.h`（及对应 `.c`） |
| App↔Boot OTA API | `app_ota.h`（及 App 侧实现：写 BKP + 复位） |

实现策略：优先移植 `CardReaderBootLoader-v0.3.1` / R10 `boot.c` 状态机到本布局；路径 **待工程确认**。

安装阶段入口：`Boot_RunInstallStage()`（上电恢复与 `SESSION_DONE` 共用）。一次尝试：`Boot_InstallOnce()`。`IMAGE_READY`：`OtaMeta_CommitImageReady()` 整页事务（只擦 1 KiB Meta 页）。进入 `SESSION_DONE` 时清零传输/安装 RAM 计数。FAIL 写不进与 READY 三次失败均 `Boot_OtaStartAttempt()`，不复位。待安装恢复：`NVIC_SetPriorityGrouping(3)` 后 `Drv_Time_Init()`，不开 UART。Secondary 预擦：`GT32_EraseSecondarySlot()`。
