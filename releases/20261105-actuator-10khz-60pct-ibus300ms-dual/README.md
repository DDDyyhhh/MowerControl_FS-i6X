# 恢复两路推杆10kHz／60%＋300ms：完整双端包

草袋TIM2 CH3/CH4与升降TIM3 CH3/CH4恢复10kHz，正反转及归位60%，停止0%。48V母线下理想平均约28.8V。300ms遥控超时和原机械计时保持。

| 设备 | BIN | 地址 | 字节数 |
|---|---|---|---:|
| STM32F407底盘 | [control-chassis-actuator-10khz-60pct-ibus300ms.bin](control-chassis-actuator-10khz-60pct-ibus300ms.bin) | `0x08000000` | 90412 |
| FS-i6X遥控器 | [fs-i6x-actuator10khz60pct-paired.bin](fs-i6x-actuator10khz60pct-paired.bin) | `0x08000000` | 130872 |

[双端刷写与测试说明](刷写与测试说明.md) · [ST-Link故障读取说明](ST-Link读取说明.md) · [发布元数据](release.json)

两端均附HEX、ELF和MAP。底盘BIN与20261104原10kHz/60%版本逐字节一致。遥控器功能源码未改，与上一版仅构建时间标记不同，已装配套0x36遥控器时可只更新底盘；本包提供完整双端用于完整部署或恢复。

`rollback-15khz-40pct/`保存本次更新前15kHz/40%/300ms的完整双端镜像。历史包未改；本轮尚未刷写，尖啸改善和实际运行效果需现场确认。
