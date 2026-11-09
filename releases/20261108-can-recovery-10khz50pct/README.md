# FS-i6X 双端固件：CAN 自动恢复与快速握手

基于 SVN r109 加未提交的工作副本改动构建，协议 0x36。源码差异见 `validation/source-changes.patch`，新增文件在 `validation/new-files/`。

刷写步骤见[刷写与复测说明](刷写与复测说明.md)。

## 镜像

| 设备 | 文件前缀 | BIN 起始地址 | BIN 字节数 |
|---|---|---|---:|
| STM32F407 底盘 | `control-chassis-can-recovery-10khz50pct` | `0x08000000` | 90892 |
| FS-i6X 遥控器 | `fs-i6x-fast-handshake-paired` | `0x08000000` | 131000 |

底盘提供 `.bin`、`.hex`、`.elf`、`.map`，遥控器提供 `.bin`、`.hex`、`.elf`。两端须配套刷写。

## 本版改动

- **CAN 自动恢复：** 开启 `AutoBusOff`，行走和遥测邮箱增加 20 ms 发送超时，新增 `g_can_bus_off_events` 计数。
- **开机握手提速：** 遥控器在会话缺失时每 400 ms 重发，底盘新绑定后 6 秒内每 500 ms 重发，其余仍为 2 秒。决策见 `validation/new-files/0003-handshake-retry-pacing.md`。
- **推杆：** 升降 TIM3 和草袋 TIM2 均为 10 kHz、50% 占空比（ARR=8399，CCR=4200），48 V 母线下平均约 24 V。启动时 400 ms 内从 10% 线性升到目标值，升速期间高度积分按实际占空比折算。
- **割刀转向：** FTESC CAN 的 ERPM 符号取反（`BLADE_FTESC_ERPM_DIRECTION=-1`）。

正式配置保留草袋 PD10 连接检查，不含测试用的连接绕过。

## 验证状态

- 软件：`verify.sh all`、`mower-ok-release` 底盘构建和遥控器构建通过，日志在 `validation/`。macOS 环境下 host 测试使用 `releases/20261106-lifter-15khz/validation/` 中的适配脚本。
- 实机：底盘镜像已刷写并校验，遥控器镜像已刷写。解锁速度、割刀转向和 bus-off 后恢复控制是用相同改动的台架固件验证的。
- 未完成：PWM 波形未用示波器实测，高度未重新标定，推杆干扰未做硬件整改。

## 镜像来源

- 底盘镜像由主目录 `mower-ok-release` 构建，SHA256 开头 `b7fca2be`。
- 遥控器镜像是实机已刷写的版本，构建于测试副本；与主目录重新构建的结果只差 6 个字节的构建时间戳，相关源码相同。
