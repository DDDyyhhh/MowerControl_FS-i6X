# 割草机正式发布版 v1（2026-10-08）

状态：用户报告历轮交付复测全部通过，并于2026-10-08明确批准合入发布版代码。实机验收来自用户反馈；agent完成源码整理、回归与双端发布构建，没有进行硬件刷写。当前已刷且通过复测的设备不需要重复刷写。

## 发布代码与构建入口

本轮修改已在主工作区发布源码中，统一配置为 `mower-ok-release`；`tools/build_blade_ok.sh` 构建双端发布固件。保留原配置名以兼容已有工具；发布标签已从candidate更新为accepted release。普通debug/release与实验台架配置保持独立。

正式配置：底盘 `MOWER_BLADE_OK=ON`，两项TICKET03 HIL关闭；遥控器真实MOWER UI和Blade OK启用，mock、测试模型、EXP3/PWM HIL关闭。没有新增生产.c，也没有复制第二套状态机。

```bash
./tools/build_blade_ok.sh
./tools/svn_preflight.sh
```

## 定版功能

- 异常摇杆校准要求重新完整校准，异常轴归中；有效校准保留。
- CH1连续转向；CH2达到1000选择后退、达到2000选择前进，中间位置无前后运动，自定义端点/反向配置有效。
- CH10在1000至2000连续设定前进上限0.4至1.8m/s；倒车沿用80%系数。MOWER与DATA显示SETm/s，与ACT实际速度区分。
- 取消期间新CH4回中→左打即时提示；取消成功前禁用OK，忙期间OK不缓存，取消后仍需新的物理OK。
- 完整发送结束后，同身份Cancel可取代旧Confirm的回执等待；取消最多重传2次，60秒截止保持闭锁，匹配CANCELLED才能解除等待。10秒无成功取消回执显示STOP NO REPLY，不保证无线事务在10秒内结束。
- 每次开机在必要校准与安全检查后进入MOWER首页。
- 急停、碰撞、断链、重新授权与刀盘原确认流程保留。输出使能回显不表示实际刀盘转速或机械停转。

## 固定交付包与验收范围

正式包：`releases/20261008-mower-v1/`。

| 文件 | 来源 | 验收 |
| --- | --- | --- |
| fs-i6x-mower-v1.bin | cancel-recovery-home-20261008遥控器原件 | 用户全部复测通过 |
| control-chassis-mower-v1.bin / .hex / .elf | ch2-endpoint-speed-20261008底盘原件 | 用户全部复测通过，兼容后续遥控器 |
| build-verification/ | 当前源码重新构建的双端产物、验证日志与源码哈希 | host/ARM验证通过；并非本次实机刷写原件 |

正式包顶层保留实际通过实机复测的原件，只重命名，字节及哈希不变。遥控器SHA256 `15669696631c00aa848792156e4a597926dfe5ecfef14d26d92fd370a6f790b1`；底盘BIN SHA256 `a107f6f8f9929297c6d4bca69210822971e3aeacfcd2ef0928e876d76b1de91f`。

重新构建的遥控器只有编译时间3字节变化，大小仍128312 B，静态RAM15692 B。底盘重新构建包含后续共享协议模块修改，BIN与原件不同（81516 B对81364 B）；不把新构建产物标记为已经实机验收。顶层包用于本次已验收发布，build-verification仅用于源码构建追溯。今后使用重新构建的底盘产物作为新交付时，需对该产物另做实机验收。

## 验证与刷写

本次 `tools/build_blade_ok.sh` 成功，包含全套host、底盘debug构建及双端发布构建；SVN preflight生产清单通过，保留3类既有工作区告警。源码和日志见build-verification。

遥控器DFU刷写使用顶层 `fs-i6x-mower-v1.bin`，对应Internal Flash起始地址0x08000000；底盘ST-Link刷写使用顶层 `control-chassis-mower-v1.elf`。连接、校准和安全恢复步骤见随包刷写复测说明。已通过复测的设备无需重复刷写。

## 版本控制范围

2026-10-08用户进一步授权提交SVN，并要求完善批注。本次提交经过筛选的发布代码、必要依赖、回归工具、验收记录及固定固件包；旧RC记录和旧固件保留用于追溯。原工作副本数据库存在E155004/sqlite I/O错误，使用临时工作副本提交，不执行cleanup或回退原工作区。提交范围与验证批注详见 `docs/releases/svn-submit-20261008.md`。

本包build-verification是先前完整工作区构建记录，SOURCE-SHA256SUMS是该历史工作区指纹；它不是本次筛选后SVN源码的全量哈希。筛选后的独立构建结果见svn-submit-20261008记录。
