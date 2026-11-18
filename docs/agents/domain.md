# 领域文档

工程类 skills 在浏览代码前，按本文件读取领域文档。

本仓库采用 multi-context 布局：`control_chassis` 与 `remote_lvgl` 是两套独立固件，通过通信协议形成共享契约。

## 探索前读取

- `control_chassis/CONTEXT.md`：底盘控制板核心子系统、架构边界与路径导航
- 根 `docs/adr/`：跨板级协议、报文 ID、时序、安全状态机等共享决策（按需创建/读取）
- `<context>/docs/adr/`：仅影响单块板的局部决策

只读取与当前任务相关的上下文和 ADR。上述文件或目录不存在时静默继续；由 `/domain-modeling` 在术语或决策真正需要固化时按需创建，不预建空文件。

## 目录结构

```text
/
├── docs/adr/
├── control_chassis/
│   ├── CONTEXT.md
│   └── docs/adr/
└── remote_lvgl/
    └── docs/adr/
```

## 跨上下文改动

CAN、无线通信及其他跨板级报文的格式、ID、超时和时序是双方共享契约。修改这些契约时：

- 同时读取两个上下文的 `CONTEXT.md`
- 同时检查两侧实现和调用点
- 将决策记录到根 `docs/adr/`，而不是任一板级 ADR 目录
- 协议手册描述当前接口；ADR 记录选择该设计的原因，两者不得混用

## 使用领域词汇

issue 标题、方案、假设和测试名应使用相关 `CONTEXT.md` 定义的术语，不得漂移到词汇表明确排除的同义词。

若所需概念尚未定义，先判断是否误用了项目外术语；确有领域缺口时交由 `/domain-modeling` 记录。

## ADR 冲突

产出与现有 ADR 冲突时必须明确指出，不得静默绕过或覆盖。
