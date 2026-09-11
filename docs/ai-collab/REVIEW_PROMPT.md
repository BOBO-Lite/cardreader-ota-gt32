# REVIEW_PROMPT — EAI-DP §11 独立 AI 复审（针对 YMODEM-GT32-D1-R02）

> 用法：在**独立上下文**粘贴下方提示；以本次任务 diff + 冻结卡/R02 为中心只读审查。  
> 复审**不提升** E0–E4，不能代替人的设计批准或板端验收。  
> 方案标识：`YMODEM-GT32-D1-R02`（含 **R02.1 补丁**，2026-09-11）。

---

```text
以本次任务的 diff 为审查对象，不修改代码。
允许沿调用链只读查看核对结论所必需的关联源码、头文件和工程配置；
不要把与本次变化无关的既有问题算作本次缺陷。

对照：
- 行为权威 ../YMODEM-GT32-D1-R02.md（含 R02.1）
- ai-collab/DESIGN_FREEZE.md
- 当前切片任务卡 / 交付卡 / IMPLEMENTATION_PLAN 对应切片
- 验收标准 AC-YM-01～22（按本片涉及项）

依次检查：

1. 范围漂移和计划外行为
   - 是否编造 SPI 引脚号或 Keil/仓库路径？
   - 是否引入 Modbus FC/寄存器映射（R02.1 明确不做）？
   - 是否对 GT32 出现 Chip Erase（60h/C7h）或可调用 CE API？
   - 是否静默改分区、三旗标、BKP、时序、失败出口、发送端等待？

2. 状态机、调用链、数据所有权和生命周期
   - boot.c 是否仍为唯一 OTA 阶段机？drv_ymodem 是否只判帧、不发送、不碰 DMA？
   - DMA RX 是否 static 2048（非栈 VLA）？Boot 栈是否 ≥ 0x800？
   - IDLE/DMA ISR 是否仅冻结轮次标志（无 Flash/YMODEM 解析/GT32）？
   - 传输中是否写 Meta？READY 成功前 / LastGood 失败路径是否擦了 App？

3. 超时、错误、复位、掉电和恢复路径
   - GT32 WIP：PP≤100ms / SE≤500ms / BE≤2000ms；超时是否归入传输或安装失败出口？
   - LastGood：向量 OK → 整槽 112640 必须成功；向量 NOT OK → 跳过；是否错误依赖 Meta APP_VALID？
   - 五类失败出口是否混用？传输/安装计数是否仅 RAM、复位归零？
   - 掉电关键点（接收中/Meta 事务/LastGood/搬运/BKP2 已清 VALID 未提交/FAIL/GT32 半写）是否仍可叙述？

4. 协议兼容性、Flash/RAM/栈/时序/功耗预算
   - Meta CRC：升序喂入 0x00..0x43 再 0x54..0x57（72 字节），poly 0xEDB88320？
   - AppOta_RequestUpgrade / AppOta_ConfirmRunning 契约是否与 Boot 读 BKP1/BKP2 一致？
   - Secondary 预擦墙钟与发送端等 C 默认 30s：超限是否走设计变更而非静默改协议？
   - Boot≤16KiB、APP_IMAGE_MAX=112640、SPI 初期≤9MHz、板卡从 v0.1？

5. 验证证据是否足以支持每个 PASS
   - E 等级是否与 DESIGN_FREEZE §9 / 本片要求匹配？
   - Mock/主机是否冒充 E3/E4？证据是否可追溯到源码/构建/产物（及 E3/E4 硬件标识）？
   - 实现策略是否体现“移植 R10/v0.3.1 boot.c”，而非无必要的协议绿场重写？

只报告会影响正确性、兼容性、安全或验收的缺口；
把风格建议和未批准的改进列为可选项。
```

## 复审输出建议结构

1. **阻塞缺口**（必须修才可继续/合并）  
2. **非阻塞风险**（记录但不阻塞本片）  
3. **证据不足 / 过度宣称**  
4. **可选项**（风格或未批准增强）  
5. **结论**：可否进入人审关键路径 / 是否需设计变更
