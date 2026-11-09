# Retrospective: Repository Workflow Hardening & Agent Environment

- **日期**：2026-09-18
- **目标**：Repository Workflow Hardening（工程化环境加固，不改变产品业务逻辑）
- **适用工程**：`control_chassis`、`remote_lvgl` 及相关文档与规范

---

## 1. Context & Motivation (背景与初衷)

在近期的连续迭代中（涵盖 Remote Backend 抽象、FlySky SERVO i-BUS 接收流、CH8 双边沿授权状态机、Blade Manual Confirmation 物理确认状态机、Telemetry Snapshot 快照聚合、SENS i-BUS 响应器等特性），代码库已经沉淀了多组关键的主机端确定性回归验证套件（8 组 C Harnesses）以及成熟的 EIDE / ARM GCC 构建能力。

然而，在 AI Agent 协作与工程化流转过程中，暴露出若干环境治理与工作流短板：
1. **缺少单一验证入口**：Agent 需自行拼凑 gcc 编译选项或猜测不存在的 `eide` CLI，导致测试验证步骤易碎片化、高成本或遗漏。
2. **领域导航缺乏实存索引**：文档层残留过时引用（如不存在的 `CONTEXT-MAP.md` 和 `remote_lvgl/CONTEXT.md`），增加了上下文探索开销与漂移风险。
3. **陈旧与误导性描述**：`AGENTS.md` 仍记录“无主机单元测试”，与仓库内大量高覆盖度的确定性 C Harness 现实脱节。
4. **工程清单同步风险**：新添加的 `.c` 代码若未同时加入 EIDE (`eide.yml`) 与 Keil (`concrol.uvprojx`)，会导致构建遗漏隐患。
5. **SVN 提交流水线缺乏自检**：临时编译产物（`.d`, `.o`, `.axf` 等）、CRLF/LF 全量换行畸变以及高危文件变更缺乏自动化无损预检工具。

---

## 2. Findings (核心发现)

| 类别 | 现象与问题 | 风险等级 | 根因与影响 |
|---|---|---|---|
| **Verification** | 缺少统一运行入口，Agent 难以一次性触发全部 8 组 Host Harness 与 ARM 交叉编译 | **P0** | 编译宏定义（如 `REMOTE_BACKEND_*`, `BLADE_MOTOR_DRIVER_*`）容易传错，导致验证断层 |
| **Navigation** | 根文档引用不存在的 `CONTEXT-MAP.md`，无统一的底层架构指针 | **P0** | Agent 在无索引情况下需要盲目全局 grep/glob，浪费上下文窗口与注意力 |
| **Manifest Sync** | 新建生产 `.c` 文件后，可能只在部分工具配置中引入，导致 Keil 或 EIDE 漏编 | **P1** | 双构建工具（EIDE / Keil）并存环境下的配置漂移 |
| **SVN Hygiene** | 编译生成的临时文件大量出现在 `svn status` 中，偶发全文件换行符转换 | **P1** | 提交历史污染，潜在代码合并冲突 |
| **Outdated Docs** | `AGENTS.md` 声称“无主机测试套件”，与实际 8 组 Harness 不符 | **P2** | 误导后续任务放弃 host harness 编写与回归验证 |
| **Definition of Done** | 缺乏统一的软件完成与硬件测量标准，不同 Ticket 验收标准松紧不一 | **P2** | 示波器波形/图片证据要求模糊，增加非必要验证阻碍 |

---

## 3. Decisions & Actions Taken (决策与落实)

### 决策 1：建立规范唯一的全量验证入口 (`tools/verify.sh`)
- **动作**：创建可执行脚本 `tools/verify.sh`，支持：
  - `./tools/verify.sh host`：封装全部 8 组 C Harnesses 真实编译命令，覆盖正向用例、异常时序与编译期宏排他/安全守卫负向测试。
  - `./tools/verify.sh chassis`：自动探测本地可用的 EIDE `unify_builder` 二进制，使用 ARM GNU Toolchain 完成 `control_chassis` Cortex-M4 目标固件构建（0 Error 校验，自动检查产物 `.hex`/`.elf`）。
  - `./tools/verify.sh all`：全量一键执行 host + chassis 检查。
- **效果**：执行耗时约 1.9 秒，提供带颜色高亮、日志落盘（`build/logs/`）与失败详细输出的确定性闭环。

### 决策 2：建立渐进式领域导航索引 (`control_chassis/CONTEXT.md`)
- **动作**：按照 Progressive Disclosure 原则建立 `control_chassis/CONTEXT.md`，涵盖 10 大核心领域模块：
  1. Remote Backend
  2. FlySky i-BUS
  3. Remote Authorization
  4. SafetyOwnershipGate
  5. Blade driver seam
  6. Telemetry Snapshot
  7. SENS responder / USART2
  8. CAN / FTESC
  9. Host harness
  10. Retired/reserved resources
- **严格约束**：所有提及路径均通过仓库实存性校验，严禁引用虚构文件。

### 决策 3：实现无损 SVN 提交前预检工具 (`tools/svn_preflight.sh`)
- **动作**：创建 `tools/svn_preflight.sh`，严格遵循“只检查，不修改、不自动 revert”原则：
  1. `svn status` 检查与冲突标记（`C`）检测；
  2. 临时/构建产物（`.o`, `.d`, `.axf`, `.elf`, `.htm`, `.lnp`, `.sct`, `.stackdump` 等）报警；
  3. CRLF / LF 大范围换行符翻转报警；
  4. 高危架构文件（`main.c`, `freertos.c`, `stm32f4xx_it.c`, `*.ioc`, `*.uvprojx`, `*.ld`）修改警示；
  5. 生产 `.c` 文件 EIDE (`eide.yml`) 与 Keil (`concrol.uvprojx`) 双清单注册完备性校验。

### 决策 4：确立明确的 Definition of Done 规范
- **动作**：在 `docs/agents/issue-tracker.md` 补充验收标准：
  - **Software Implementation**：对应 host harness PASS；既有 harness regression PASS；ARM target build 0 errors；不引入新 warnings；新生产 `.c` 纳入工程清单；不强求消除历史无关 warnings。
  - **Hardware Validation**：关键实测参数完成；安全/配置决策冻结；Markdown 测量记录存在；图片/示波器原始文件**默认 optional**（仅当 Ticket 验收目标本身要求波形/时序证据时才设为 mandatory）。

### 决策 5：清理与重构 `AGENTS.md` / `CLAUDE.md` / `docs/agents/`
- **动作**：
  - 删除不存在的 `CONTEXT-MAP.md` 与 `remote_lvgl/CONTEXT.md` 引用。
  - 删除“无主机测试框架”的陈旧过时描述，替换为分层测试体系（Host Harness + Target Build + HIL）。
  - 增加 canonical verify 入口与 svn preflight 命令说明。
  - 增加新 `.c` 文件的 EIDE/Keil 双清单登记规则。

---

## 4. Implementation Checklist (实施验收清单)

- [x] **`tools/verify.sh`**：
  - [x] 支持 `./tools/verify.sh host`
  - [x] 支持 `./tools/verify.sh chassis`
  - [x] 支持 `./tools/verify.sh all`
  - [x] 涵盖 8 组 Host Harness (Blade Driver Selection, Blade Manual Control, FTESC Feedback, SENS Responder, Telemetry Snapshot, Remote Authorization, Remote Backend, Safety Ownership Gate)
  - [x] 自动创建 `build/logs`
  - [x] 自动查找 EIDE `unify_builder` 并注入 `DOTNET_ROLL_FORWARD=Major`
  - [x] 实测全量执行耗时 < 2 秒，返回码 0
- [x] **`control_chassis/CONTEXT.md`**：
  - [x] 10 大子系统 progressive disclosure 导航建立
  - [x] 全部路径 100% 通过实存检查，无死链
- [x] **`tools/svn_preflight.sh`**：
  - [x] 冲突检测
  - [x] 临时构建产物检测
  - [x] 大范围换行变化检测
  - [x] 高危文件变动提醒
  - [x] 生产 `.c` 双工程清单同步校验
  - [x] 无任何自动 revert/修改操作
- [x] **规范与文档更新**：
  - [x] `docs/agents/issue-tracker.md` (Definition of Done 纳入软件与硬件双标准)
  - [x] `docs/agents/domain.md` (清理虚构导航，对齐 `control_chassis/CONTEXT.md`)
  - [x] `CLAUDE.md` (更新验证入口、DoD、领域导航与生产代码集成规则)
  - [x] `AGENTS.md` (更新关键目录、验证命令、分层测试描述与工程清单同步规则)
- [x] **无副作用约束**：
  - [x] 未修改任何 `control_chassis` 产品业务逻辑
  - [x] 未修改 `OpenI6X` 任何代码
  - [x] 未执行 `svn revert`
  - [x] 未自动提交 SVN
