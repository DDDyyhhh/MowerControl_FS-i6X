# Issue 跟踪方式：本地 Markdown

本仓库的 issue 与 spec（spec 也常称为 PRD）以 Markdown 文件保存在 `.scratch/` 下。

本仓库使用 SVN（现有配置记录的仓库地址为 `http://192.168.9.5:8080/svn/MowerControl/trunk`），没有 GitHub/GitLab remote，因此不使用 `gh` 或 `glab`。

## 约定

- 一个功能一个目录：`.scratch/<feature-slug>/`
- spec 路径：`.scratch/<feature-slug>/spec.md`
- 实现类 issue 路径：`.scratch/<feature-slug>/issues/<NN>-<slug>.md`
- 每个 ticket 使用独立文件，编号从 `01` 开始；不要把多个 ticket 合并到一个文件
- 状态写在文件顶部附近的 `Status:` 行
- 分流角色另写在顶部附近的 `Triage:` 行；标签映射及使用规则见 `docs/agents/triage-labels.md`
- 评论与讨论追加到文件末尾的 `## Comments` 下
- 涉及板级上下文时，添加 `Context: control_chassis` 或 `Context: remote_lvgl`；同时涉及两块板时写两个值

## SVN

`.scratch/` 当前尚不存在，也未纳入版本控制。是否提交由用户决定：

- 团队共享：创建后执行 `svn add .scratch`，再按正常流程提交
- 仅作本地草稿：通过 SVN 属性忽略 `.scratch/`

除非用户明确要求，agent 不得主动执行 `svn add`、设置忽略属性或提交 `.scratch/`。

## 当 skill 要求“发布到 issue tracker”

在 `.scratch/<feature-slug>/` 下创建对应文件；目录不存在时按需创建。

## 当 skill 要求“读取相关 ticket”

读取用户引用的文件路径。用户通常会直接提供路径或 issue 编号。

## Wayfinding 操作

`/wayfinder` 使用一个 map 文件和每个 ticket 对应的子文件：

- Map：`.scratch/<effort>/map.md`，包含 Notes、Decisions-so-far 与 Fog
- 子 ticket：`.scratch/<effort>/issues/NN-<slug>.md`
- `Type:`：`research`、`prototype`、`grilling` 或 `task`
- `Status:`：`claimed` 或 `resolved`
- `Blocked by: NN, NN`：列出的 ticket 全部为 `resolved` 后解除阻塞
- Frontier：选择未关闭、未阻塞、未认领且编号最小的 ticket
- 认领：开始工作前将 `Status:` 设为 `claimed`
- 解决：在 `## Answer` 下追加答案，将 `Status:` 设为 `resolved`，并向 `map.md` 的 Decisions-so-far 追加要点与链接

## 任务完成标准 (Definition of Done)

所有 Issue / Ticket 在标记为完成（`Status: resolved` 或通过验收）时，必须严格满足对应类别的完成标准：

### 1. Software Implementation 默认完成标准
- **对应 host harness PASS**：新增或修改功能对应的测试用例通过（可单独运行或通过 `./tools/verify.sh host` 统一执行）。
- **相关既有 harness regression PASS**：已有相关 host harnesses 全部回归通过，无破坏性断言。
- **ARM target build 0 errors**：Cortex-M4 目标固件编译 0 Error（通过 `./tools/verify.sh chassis` 或 `./tools/verify.sh all`）。
- **不新增 warnings**：本次修改引入的代码与头文件不得产生新的编译警告。
- **新增生产 .c 已加入目标工程**：新增的生产代码文件必须同时加入 EIDE (`eide.yml`) 与 Keil (`concrol.uvprojx`) 的源码清单，且必须通过 `./tools/svn_preflight.sh` 验证。
- **不要求消除仓库历史无关 warnings**：对于代码库历史遗留的未触碰警告，不设为当前 ticket 的卡点。

### 2. Hardware Validation 默认完成标准
- **关键实测参数完成**：测量并记录关键电气与通信参数（如电平、波特率、周期、时延、门限）。
- **安全/配置决策冻结**：涉及引脚功能、上拉下拉、安全联锁逻辑的决策在记录中明确冻结。
- **Markdown 测量记录存在**：在对应 issue 文件的 Comments 或 Verification 章节给出文字与数据记录。
- **波形与图片证据契约**：
  - 示波器截图、接线图、测试照片等二进制/图片附件**默认 optional**。
  - **仅当 Ticket 本身的验收标准（Acceptance Criteria）明确要求波形/时序图证据时**，图片文件才设为 **mandatory**。
