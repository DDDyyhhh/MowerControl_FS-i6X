# CH6 集草袋底盘固件 — 2026-10-09

状态：软件验证通过，硬件待复测。没有刷写设备或提交 SVN。

## 使用行为

- CH6≈1000 打开，CH6≈2000 关闭；当前默认 SB→CH6，保存模型可能有自定义映射，请先检查。
- CH6 两端 ±120μs 消抖，必须连续 3 个不同有效帧。i-BUS 全局合法范围仍为 900..2100μs，因此实际接受打开 900..1120、关闭 1880..2100。
- 仅手动模式、遥控稳定在线、解锁且安全门允许、草袋连接时执行。上电、重新授权、通信恢复、草袋重接和返回手动模式先建立基线，不自动跟随档位动作。
- 当前档位保持不重触发；新稳定目标可在动作中换向，每次新方向计时 20 秒。换向先将两路 PWM 清零。
- 急停、碰撞/倾斜保护、上锁、断链、草袋断开或控制模式切换会中止动作；中止保留上次完成状态，新拨动才启动 CH6 动作。
- RK3588 的 Byte6 仍为幂等目标。API 模式下 CH6 无效；API 仍需正常进入运行且持续有效心跳，草袋连接与物理安全检查同样适用。模式切换先停止旧动作，后续周期 CAN 目标可启动 API 动作。
- 无物理限位反馈，20 秒后的开/关状态是时间估算；中断后新指令从当前机械位置重新计满 20 秒，必须核实机构堵转和全行程是否允许该时间。

## 固件文件

只需刷底盘。遥控器沿用已验证的 MOWER/OK 固件；本次双端构建成功不意味着需要刷写新遥控器。

| 文件 | SHA256 |
| --- | --- |
| control-chassis-grass-bag-ch6.bin | `af6a4af72b85c5f44b73d74a4a8e2191a65158221631930e4184f22e198677c6` |
| control-chassis-grass-bag-ch6.hex | `66b7dc2180da96e5fce711d1d92347f817f19bdafa19cc0f4790dac2f85dc10d` |
| control-chassis-grass-bag-ch6.elf | `90223924ce7ddfe5bd859c194310696f75906abebca189f1e358b61039d95fc7` |

包内 `SHA256SUMS` 校验交付文件；`validation/SOURCE-SHA256SUMS` 是本次当前工作区源码指纹，包含已有修改，不代表独立 SVN 提交范围。原 `releases/20261008-mower-v1/` 保持不变并通过其哈希检查。

完整刷写步骤和硬件测试表见 `刷写与复测说明.md`。

## 软件验证

- `./tools/verify.sh all`：全套主机回归和底盘 Debug ARM 构建通过。
- `./tools/build_blade_ok.sh`：再次执行全套回归，底盘 mower-ok-release 和遥控器发布构建通过。
- 新增测试：CH6 目标映射、重复帧、中间值、基线、权限丢失、实际 i-BUS 超时及重新授权、急停输入、保持档位、计时边界与回绕、换向、中止不误报完成、互斥保护、分配失败闭锁、保存高度期间新指令的时序。
- 相关主机测试编译使用 `-Wall -Wextra -Werror`，实际 Lifter 用替身模拟 PWM/RTOS/Flash，并非硬件证据。
- `svn_preflight.sh`：通过，生产清单 PASS，3 类告警为工作区编译垃圾、原有 app_Comm 行尾/空白提示、高风险文件修改提示（本次仅为 Keil 新源文件登记，其余提示涉及已有修改）。未自动清理或回退工作区。
- 当前增量构建日志无新增编译警告；项目全量构建的历史告警不在本次清理范围。

## 构建配置和接口

CH6 草袋只在 `MOWER_GRASS_BAG_CH6_ENABLED=1` 且 FlySky＋`MOWER_BLADE_OK_ENABLED=1` 配置启用。CMake 的 mower-ok-release 自动启用；普通 Debug/历史配置保留旧 CH6 刀盘适配器，草袋功能关闭。不得通过开启草袋宏来运行历史 CH6 刀盘确认方案。

`FlySkyIbusConfig_t` 增加草袋索引/语义配置；`RemoteCommand_t` 增加 raw、configured 和 stable target。`RemoteBackend_AdvanceGrassBag()` 由 Motion 单消费者调用，完整权限由 Motion 传入。`Lifter_SetGrassBagPermission()`、`Lifter_StopGrassBag()` 与目标接口、周期更新共用带优先级继承的互斥锁。

新增生产 `grass_bag_control.c` 已同步登记 CMake、Keil、EIDE。未改变遥控器固件源码、无线帧格式、CAN 字段或刀盘授权流程。
