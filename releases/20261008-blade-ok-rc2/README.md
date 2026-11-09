# 割刀流程 RC2：UNKNOWN 启动门与 2 秒等待弹窗

2026-10-08，协议版本仍为0x32。底盘镜像与RC1逐字节相同；若底盘已刷RC1，本次只需更新遥控器BIN。不适用于旧ticket03台架底盘0x31。

- UNKNOWN时CH4左打不弹START BLADE?，OK不发Confirm。即使弹窗已出现，一旦快照失效也关闭并禁用OK。
- 在UNKNOWN时左打后，恢复新鲜反馈仍需回中再左打，不自动补弹/补授权。
- WAIT RESULT与WAIT CANCEL最多显示2000ms（加一次GUI刷新），随后返回主页，事务继续后台处理。回执早到则提前结束。
- 弹窗消失不代表成功或取消完成，不缩短无线实际往返。主页仅按当前控制器快照显示ENABLED/OFF/UNKNOWN；后台未结束时重复OK不提交第二次启动。
- 原安全门、停止、CH7三档及回执匹配保持既有行为。

## 遥控器更新

进入既有DFU模式，在本目录执行：

```bash
dfu-util -a 0 -s 0x08000000:leave -D openi6x-blade-ok-rc2.bin
```

## 底盘（仅尚未更新到RC1时需要）

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "adapter speed 1000" \
  -c "program control-chassis-blade-ok-rc2.hex verify reset exit"
```

## 验证

全量主机回归、双端ARM构建通过，哈希检查通过。遥控器Flash127456B，静态RAM15684B，剩余700B。RC1原流程实机验证由用户报告通过；RC2上述改动尚待实测。

保持原隔离台架条件验证：UNKNOWN中回中/左打/OK不弹窗不启动；恢复反馈但保持左打仍无弹窗，回中再左打正常；两种等待弹窗均在约2秒内退出，后台完成后可重新启动；弹窗退后重复OK不再次授权；停止/急停归零且主页OFF/断流UNKNOWN。
