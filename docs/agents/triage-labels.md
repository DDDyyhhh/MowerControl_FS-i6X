# Triage 标签

本文件将工程 skills 的五个分流角色映射到本仓库使用的标签。

| 角色 | 本仓库标签 | 含义 |
| --- | --- | --- |
| `needs-triage` | `needs-triage` | 等待维护者评估 |
| `needs-info` | `needs-info` | 等待报告者补充信息 |
| `ready-for-agent` | `ready-for-agent` | 规格完整，可交给 agent 实现 |
| `ready-for-human` | `ready-for-human` | 需要人工实现 |
| `wontfix` | `wontfix` | 不予处理 |

当 skill 要求应用某个分流角色时，使用表中的本仓库标签。

本地 Markdown issue 在文件顶部附近使用独立的 `Triage: <label>` 行记录分流角色；`Status:` 继续记录任务执行状态，包括现有 wayfinding 的 `claimed` / `resolved`。分流操作只更新 `Triage:`。

以后可直接编辑表中的“本仓库标签”列，以匹配项目使用的标签名称。
