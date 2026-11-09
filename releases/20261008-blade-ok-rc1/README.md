# CH4 / OK / CH7 发布候选镜像 RC1

2026-10-08。必须同时更新遥控器和底盘。候选版版本0x32与ticket03台架版本0x31不可混刷。

- 遥控器：`openi6x-blade-ok-rc1.bin`，Flash 127312 B，静态RAM 15668 B（余716 B）。
- 底盘：`control-chassis-blade-ok-rc1.hex`，另提供同内容BIN。
- 运行行为、构建入口和现场验收见 [集成说明](../../docs/releases/blade-ok-release-candidate.md)。

## 底盘 ST-Link / OpenOCD

在本目录运行：

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "adapter speed 1000" \
  -c "program control-chassis-blade-ok-rc1.hex verify reset exit"
```

## 遥控器 DFU

遥控器进入既有DFU刷写模式，在本目录运行：

```bash
dfu-util -a 0 -s 0x08000000:leave -D openi6x-blade-ok-rc1.bin
```

镜像不自动创建或选择测试模型；使用之前已验证的正确通道模型。CH11/CH12用于协议承载，不能再由旋钮或其他混控覆盖。首次实测仍沿用刀盘、电调物理断开的隔离台架。

CH4回中再左打，按一次OK；安全门允许后，WAIT RESULT结束直接显示主页BLADE OUTPUT ENABLED和CH7档位，不显示TEST ACCEPTED。停止显示OFF，无新鲜控制器快照显示UNKNOWN；反复OK不能再次授权。

主机和构建验证已通过，尚未刷写这对候选镜像，也未实测新增addr10传感器和LCD布局。它不是已经完成最终发布验收的固件。
