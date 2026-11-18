# 移除 0x31 台架配置

`experiments/ticket03/` 原先有两种构建形态：正式的 `MOWER_BLADE_OK`（报文版本 0x36），和早期用于台架/HIL 验证的 0x31 配置（底盘 `exp3-hil`、`exp3-pwm-hil` 预设，遥控器 `PCBI6X_EXP3`、`PCBI6X_EXP3_PWM_HIL` 选项）。两种形态在同一批源文件里用 `#if MOWER_BLADE_OK_ENABLED` 交错，台架分支已经出现引用未声明变量、打开就编译不过的片段，却仍要占用每次改动的阅读和测试成本。决定只保留正式形态：`EXP3_ENABLED=1` 必须与 `MOWER_BLADE_OK_ENABLED=1` 同时出现，否则 `exp3.h` 以 `#error` 拒绝；`EXP3_WIRE_VERSION` 固定为 0x36。

不保留台架配置作为调试手段，是因为正式流程已有 host 测试和实机验收路径，而 0x31 固件与正式固件不能混刷，继续维护只会制造第三种需要配套刷写的组合。`EXP3_PWM_PROBE_ENABLED`、`EXP3_AUTO_SESSION_ENABLED` 两个宏暂时保留，它们在正式构建中恒为 1，折叠留待以后。

正式 0x36 的报文格式、会话与授权安全条件不变；移除前后 `mower-ok-release` 底盘固件逐字节一致。原 `run_host.sh` 的测试中，协议核心、传输、自动会话、割刀适配和 SERVO 传输门测试改在正式宏下运行（并入 `tools/verify_blade_ok_host.sh`）；只断言 0x31 台架界面流程和调试器邮箱流程的测试（`test_rf.c`、`test_auto_board.c`、`test_sens.c`，以及 `test_probe.c` 中的 9 个界面用例）随配置一并删除，对应的正式行为由 `test_release_board.c`、`test_release_radio.c`、`test_release_sens.c` 和 `tools/test_blade_ok_ui.py` 覆盖。旧的 0x31 台架固件此后无法从主线重新构建，需要时从 SVN 历史或 `releases/` 中的冻结包取得。
