# AI 协作文档包 — GD32F103 + GT32 YMODEM OTA

本目录为 **CardReader 新板 BootLoader OTA（GT32）** 的最小 AI 协作文档集。  
开发模型：**个人+AI**。不强制共享团队 R0–R2 治理门禁；仍严格执行 EAI-DP V1.2 的 **A0–A3** 与 **E0–E4**。

## 阅读顺序（个人+AI）

1. **[AGENTS.md](./AGENTS.md)** — 项目长期规则、红线、构建/验证入口  
2. **[DESIGN_FREEZE.md](./DESIGN_FREEZE.md)** — A3【设计冻结卡】（编码前必须获用户批准）  
3. **行为权威**：[`../YMODEM-GT32-D1-R02.md`](../YMODEM-GT32-D1-R02.md) — **唯一** OTA/Boot 行为规范  
4. **[IMPLEMENTATION_PLAN.md](./IMPLEMENTATION_PLAN.md)** — 按可独立验收行为划分的切片与检查点  
5. **[DECISIONS.md](./DECISIONS.md)** — 长期架构决定（ADR 风格）  
6. **[EVIDENCE_LOG.md](./EVIDENCE_LOG.md)** — 证据索引空表（交付时填写）  
7. **[REVIEW_PROMPT.md](./REVIEW_PROMPT.md)** — A3 独立 AI 复审提示  
8. 开放标准：  
   - [嵌入式 AI 协作开发规范 EAI-DP V1.2](https://github.com/BOBOEMS/AI_Collaborative_Code_Standards/blob/main/AI%20%E5%8D%8F%E4%BD%9C%E5%BC%80%E5%8F%91%E8%A7%84%E8%8C%83/embedded-ai-development-protocol-v1.2.md)  
   - [C 语言分册](https://github.com/BOBOEMS/AI_Collaborative_Code_Standards/tree/main/C)（链接引用，勿整份复制进本仓库）

任务卡空白模板见开放标准仓库；本目录提供一份已填示例：[TASK_CARD_TEMPLATE_FILLED_EXAMPLE.md](./TASK_CARD_TEMPLATE_FILLED_EXAMPLE.md)（Slice 1）。

## 行为权威声明

> **`YMODEM-GT32-D1-R02.md` 是本项目 OTA/Boot 的唯一行为权威（≡ FEATURE_SPEC）。**  
> 含 **R02.1 补丁**（2026-09-11）：板卡 v0.1、AppOta API、LastGood 向量判定、栈/`static` DMA、Meta CRC 升序喂入、GT32 WIP 超时、移植非重写等。  
> AI **不得**静默改变 OTA 行为（分区、三旗标语义、YMODEM 时序、安装顺序、失败出口、BKP/AppOta 契约、禁止 Chip Erase 等）。  
> 发现必须偏离时：暂停切片 → 提交设计变更 → 等人确认；不得在交付卡里事后“顺便说明”。

提纲 [`../YMODEM-GT32-D1-R01-提纲.md`](../YMODEM-GT32-D1-R01-提纲.md) 仅作修订背景；实现与验收以 **R02（含 R02.1）** 为准。

其它协作文件：[EVIDENCE_LOG.md](./EVIDENCE_LOG.md)（证据索引表）；[REVIEW_PROMPT.md](./REVIEW_PROMPT.md)（EAI-DP §11 独立复审提示，针对 R02）。

## 个人+AI 门禁摘要

| 项 | 本项目做法 |
| --- | --- |
| R0–R2 团队门禁 | **不强制**；不要求共享评审角色矩阵 |
| A0–A3 | **强制**；Boot/OTA/Flash/SPI/WDT/RS485 一律 A3 |
| E0–E4 | **强制**；掉电等按冻结卡映射到 E4，不得用 Mock 冒充板端 |
| A3 编码前 | 设计冻结（R02 + 本目录冻结卡）+ 独立 AI 复审 + 人对关键路径/证据审查 |
| 板端烧录/擦写 | **必须**获得用户对目标、范围、次数的明确授权 |

批准 R02 行为终稿 **并且** 批准本目录 `DESIGN_FREEZE.md` 后，才允许在已批准切片上开始编码。
