# 24V推杆及300ms遥控超时：主程序默认

2026-11-04按用户要求，将已交付的24V推杆/300ms配置合入当前主程序。两路推杆50%占空比已在主源码中，本轮保留该实现，将有效i-BUS帧超时的头文件和常规CMake构建默认值从100ms提升为300ms。

| 构建预设 | 有效i-BUS帧超时 | 用途 |
|---|---:|---|
| `mower-ok-release` | 300ms | 主程序生产构建 |
| `mower-ok-ibus-300ms` | 300ms | 保留已有显式300ms构建入口 |
| `mower-ok-ibus-100ms` | 100ms | 显式回退构建 |

`debug`和`release`预设也显式选择300ms，以覆盖旧构建目录中的100ms缓存。自行直接复用CMake缓存时，以缓存参数或命令行`-DMOWER_IBUS_TIMEOUT_MS=300`为准。Keil/EIDE选择FlySky后端且未显式覆盖时使用共享头文件默认300U；本轮不切换已有工程的后端或协议配置。

从仓库根目录构建主程序：

```bash
source tools/gcc15_env.sh
cmake --preset mower-ok-release -S control_chassis
cmake --build control_chassis/build/gcc15/mower-ok-release --parallel 8
```

回退构建将两处`mower-ok-release`替换为`mower-ok-ibus-100ms`。不需要变更遥控器固件或协议。

50%占空比在48V母线下对应约24V平均输出，瞬时PWM幅度仍接近母线，不能作为恒压或限流电源。归位10秒、草袋动作20秒、高度时间表、上升补偿系数及检测屏蔽时间保留，机械标定仍需现场验证。500ns死区未包含在此次合入中。

300ms相对100ms最多多保留旧命令约200ms；急停、碰撞、CH5停车及上位机CAN超时规则不变。持续无有效帧达到配置期限仍撤权锁定，恢复收帧后仍需重新合法授权和新的割刀OK。

接收机接地整改后的“不再超时”为用户现场反馈，原始ST-Link记录和分析位于`.scratch/pushrod-loaded-lock-capture-20261104/`。本轮不重新刷写设备，也不改变硬件接线。
