# 恢复两路推杆10kHz／60%：300ms主程序及FS-i6X双端固件

2026-11-05按用户要求，将草袋和升降PWM从15kHz／40%恢复为10kHz／60%。正反转及升降归位均60%，停止0%。48V母线下理想平均输出约28.8V，PWM瞬时幅度仍接近母线电压；声音和实际运行效果需现场短程复测。

TIM2和TIM3均保持84MHz输入、PSC=0，将ARR从5599恢复为8399；84MHz/8400=10kHz，60%比较值5040，周期100µs、有效高电平60µs。同步更新CubeMX concrol.ioc。保留实际时钟、定时器和EG2104驱动的O0/O2回归。

300ms有效i-BUS帧超时、协议、授权、安全逻辑及原升降归位10秒、草袋20秒、高度时间表、上升补偿0.8和关门检测屏蔽14秒不变。本轮不新增500ns死区或限流，也不声称已经完成机械标定或尖啸改善验收。

当前割刀为FTESC CAN。旧PWM割刀配置的TIM2 CH2与草袋通道共用周期，恢复后同为10kHz；本轮不切换割刀驱动。

完整双端构建入口（Unicorn 2.1.4位于临时依赖目录）：

```bash
PYTHONPATH=/tmp/mower-60pct-test-deps ASAN_OPTIONS=detect_leaks=0 ./tools/build_blade_ok.sh
```

发布目录`releases/20261105-actuator-10khz-60pct-ibus300ms-dual/`包含STM32F407底盘及FS-i6X遥控器BIN/HEX/ELF/MAP、15kHz／40%双端回退镜像、校验、刷写/独立读回和ST-Link只读诊断说明。底盘BIN须与原已验证的10kHz／60%发布逐字节一致。历史包保持不变，本轮不实际刷写。
