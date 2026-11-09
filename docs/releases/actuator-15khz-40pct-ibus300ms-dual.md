# 两路推杆15kHz／40%：300ms主程序及FS-i6X双端固件

2026-11-04按用户最终要求，将草袋和升降PWM从10kHz提升为15kHz，并将两路运行占空比从60%调整为40%。正反转及升降归位均40%，停止0%。48V母线下理想平均约19.2V，瞬时PWM幅度仍接近母线电压。

TIM2和TIM3的APB1定时器输入均为84MHz，预分频保持0，ARR由8399改为5599；84MHz/5600=15kHz，40%比较值2240，周期约66.67µs、有效PWM高电平约26.67µs。同步更新CubeMX concrol.ioc，避免重新生成时恢复旧频率。

当前割刀生产配置为FTESC CAN，不使用TIM2 PWM通道。若另行切换旧PWM割刀配置，TIM2 CH2与草袋通道共用周期，也会成为15kHz，需按对应电调要求另行验证。

主程序继续使用300ms有效i-BUS帧超时、原协议及授权流程。升降归位10秒、草袋动作20秒、高度时间表、上升补偿0.8和关门检测屏蔽14秒保持，需按15kHz/40%实测速度和行程后标定；不新增500ns死区或限流。

新增主机回归执行实际SystemClock_Config、MX_TIM2_Init、MX_TIM3_Init及EG2104驱动，验证频率、CubeMX一致性、两路正反转比较值、启动及停止清零；既有推杆及完整双端回归保留。

完整构建入口（临时目录中需有Unicorn 2.1.4）：

```bash
PYTHONPATH=/tmp/mower-60pct-test-deps ASAN_OPTIONS=detect_leaks=0 ./tools/build_blade_ok.sh
```

交付目录为`releases/20261104-actuator-15khz-40pct-ibus300ms-dual/`，包含STM32F407底盘及FS-i6X遥控器BIN/HEX/ELF/MAP、原10kHz/60%/300ms双端回退镜像、校验、刷写、独立读回及ST-Link只读诊断说明。本轮不实际刷写或重新进行机械标定。
