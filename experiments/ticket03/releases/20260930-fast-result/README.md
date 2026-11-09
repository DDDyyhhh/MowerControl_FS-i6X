# 2026-09-30 PWM 台架短回执镜像

双端都需更新。本镜像用于已有 PWM 台架功能的回执延迟复测；按原台架边界保持刀盘和电调物理断开，观察割刀接口或 PA1 的 PWM。它仍不是正式割刀固件。

- 遥控器：`openi6x-exp3-pwm-fast-result.bin`，129160 B。
- 底盘：`control-chassis-exp3-pwm-fast-result.hex`，229215 B。
- 本目录执行 `sha256sum -c SHA256SUMS` 核对固定副本。

底盘烧录：

```bash
cd "/media/dyh/工作/STM32/HAL_STM32Project/phld/MowerControl_FS-i6X/experiments/ticket03/releases/20260930-fast-result"
sha256sum -c SHA256SUMS
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "adapter speed 1000" -c "program control-chassis-exp3-pwm-fast-result.hex verify reset exit"
```

遥控器按已有 APM32 USB DFU 流程刷写本目录的新 BIN。

变更：仅 PWM 台架控制器将满足固定台架上下文/目标、request=action、action/generation<=65535 的 RESULT 无损压缩为 17 个下行字。完整 32-bit 双端启动身份和完整原消息 CRC32 保留；遥控器解压后继续做完整 CRC 和全部身份字段匹配。不符合压缩条件时使用原 90 字格式，不截断身份。上行 Confirm/Cancel 和下行每字 250 ms 节拍保持原值。

已验证：真实 STM32 适配器配合 120 ms 下行采样的主机回归，Confirm RTT 5884 ms、Cancel RTT 5924 ms；此前同场景无法在 7 秒内完成匹配。实际遥控器 AA/AC 接收路径通过短回执回归。损坏、缺字、重复、乱序、混合消息、旧身份/代数/命令及超时均测试；宽身份回退和独立 Python CRC 黄金向量通过。双端 ARM 编译及仓库完整验证通过。这是主机/构建证据，现场延迟尚待复测。

复测：

1. 双端更新后等 `E3 READY`，在割刀页 CH4 回中至少一秒，再左打至约 988。
2. 短按一次 OK，计时至 `WAIT RESULT` 结束，正常预计约 6–7 秒。PWM 会在底盘收到有效 Confirm 后先输出，而不是等遥控器回执收齐后才输出。
3. CH4 回中，输出应立即归零；计时至 `WAIT CANCEL` 结束，正常预计同样约 6–7 秒。
4. 再次左打并按 OK，应仍能启动；验证 CH7 三档及安全拦截停止。记录对应串口 `id/gen/confirm/reject/deny/ccr2`，不得因延迟优化重复授权。

RF 丢字或调度延迟仍可能使回执超过上述时间；60 秒失败超时保持不变。若仍需更短的实际延迟，应先测量 RF 下行刷新/丢字，再决定新载体或节拍，不从本次主机结果推断实机上限。
