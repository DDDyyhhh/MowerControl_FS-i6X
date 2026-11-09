# 10kHz／60%／300ms底盘：锁定后只读诊断

仅在实际自行锁定后抓取，保持供电、未重新解锁，关闭其他占用ST-Link的程序。需要OpenOCD、支持ARM和Python的GDB及arm-none-eabi-objcopy。

```bash
python3 read_remote_stop_diag.py \
  --elf control-chassis-actuator-10khz-60pct-ibus300ms.elf \
  --bin control-chassis-actuator-10khz-60pct-ibus300ms.bin \
  --gdb /usr/bin/gdb-multiarch \
  --output actuator10khz60pct-stop-01.json
```

必须显式指定对应ELF/BIN，不能省略或混用旧版本符号。脚本先核对本地ELF/BIN，再短暂停CPU逐字节核对目标Flash和两次一致的冻结SRAM，最后恢复CPU；不下载、不复位、不解锁。成功应确认cpu_resumed=true。读取失败时确认CPU恢复运行，不能用Reset或Download恢复而清掉现场。

结合首次授权撤销原因、帧年龄、UART错误增量、安全输入及复位标志解释；正常上电也可置BOR/POR，不能仅凭标志认定掉压。调试暂停不可用于实测300ms保护时延。回退后改用rollback-15khz-40pct内配套底盘ELF/BIN。
