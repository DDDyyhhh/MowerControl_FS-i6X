# 固件刷写指南

本文放所有发布包共用的步骤和排障。每个包内的 `刷写与复测说明.md`（或同类文件）只写该版本的文件名、命令、配套要求、复测项和回退方式，可以脱离本仓库单独使用。

| 设备 | 工具 | 连接 | 写入地址 |
|---|---|---|---|
| STM32F407 底盘 | OpenOCD 或 STM32CubeProgrammer | ST-Link（SWD） | `0x08000000` |
| FS-i6X 遥控器（APM32） | dfu-util 或 STM32CubeProgrammer | USB DFU，VID:PID `314b:0106` | `0x08000000` |

## 刷写前

1. 固定设备，隔离行走、割刀和推杆动力，只给逻辑板和接收机供电。具体隔离要求以各包说明为准。
2. 确认这次要刷哪一端，以及是否必须两端配套。协议版本不一致的固件不能混用。
3. 两端 BIN 的起始地址相同，**不能交叉写入**：底盘镜像以 `control-chassis` 开头，遥控器镜像以 `fs-i6x` 或 `openi6x` 开头。
4. 不做全片擦除，不改 Option Bytes，不初始化外部 EEPROM，保留遥控器模型、校准、绑定、通道和失控保护配置。
5. 建议先备份设备当前固件，回退时优先使用：

```bash
st-flash --reset --freq=100 read chassis-before.bin 0x08000000 0x20000
dfu-util -a 0 -s 0x08000000:131072 -U radio-before.bin
```

底盘备份会复位 MCU，只在动力断开时使用，故障现场取证不要用。备份只含程序区前 128 KiB，不含外部 EEPROM 和全部持久配置。

## 刷底盘

接线：SWDIO→PA13，SWCLK→PA14，GND→GND，按原接法接目标电压参考。关闭其他占用 ST-Link 的程序；ST-Link 接整车时，电脑最好用电池供电、不接充电器。

ELF 和 HEX 自带地址，BIN 需要指定地址：

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c 'adapter speed 100' -c 'reset_config none' \
  -c 'program <底盘镜像>.elf verify reset exit'
```

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c 'adapter speed 100' -c 'reset_config none' \
  -c 'program <底盘镜像>.bin 0x08000000 verify reset exit'
```

必须看到 `Verified OK`，下载进度到 100% 不算成功。

也可以用 STM32CubeProgrammer：SWD 连接，选 HEX，或选 BIN 并填地址 `0x08000000`，下载后执行 Verify，只擦除镜像覆盖的扇区。

连不上目标时，先查接线、供电和板卡型号。接了 NRST 的话可以试 Connect Under Reset。

## 刷遥控器

1. 按现场既有方法进入 USB DFU。正常开机后的 Joystick/Storage 菜单不是 DFU。
2. 执行 `dfu-util -l`，确认只有目标遥控器，并找到 Internal Flash 对应的 alt。以下命令假定 alt 为 0，不是就换成实际值。不要选 Option Bytes（alt 1）。多台设备时用 `-d VID:PID -S 序列号` 限定。
3. 写入：

```bash
dfu-util -a 0 -s 0x08000000:leave -D <遥控器镜像>.bin
```

`:leave` 让设备写完后退出 DFU。要做读回时去掉它，先留在 DFU，读回后再拔 USB。

必须看到 `File downloaded successfully`。`Invalid DFU suffix signature` 警告是裸 BIN 不带后缀造成的，不影响写入。

也可以在 STM32CubeProgrammer 的 USB DFU 连接中写入 BIN，地址 `0x08000000`，下载后校验。

## 读回比对（可选）

OpenOCD 的 `verify` 已经校验过底盘写入。需要独立确认，或者要核对遥控器时，在包目录执行。两端镜像都不超过 128 KiB，底盘命令会暂停并复位 MCU，遥控器要保持 DFU：

```bash
readback=$(mktemp -d /tmp/mower-readback.XXXXXX)
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c 'adapter speed 100' -c 'reset_config none' \
  -c "init; reset halt; dump_image $readback/chassis.bin 0x08000000 131072; reset run; shutdown"
dfu-util -a 0 -s 0x08000000:131072 -U "$readback/radio.bin"
```

```bash
compare() { head -c "$(( $(wc -c < "$2") ))" "$1" | cmp - "$2" && echo "PASS: $2"; }
compare "$readback/chassis.bin" <底盘镜像>.bin
compare "$readback/radio.bin" <遥控器镜像>.bin
```

读回只比较程序字节，不包括持久设置和外部 EEPROM。失败时检查目标和连接后重试，不要拼接不同次的读回文件。

## 回退

优先使用刷写前从设备备份的镜像。包内 `rollback/` 是上一版的候选镜像，不是设备备份，也不保证是设备原来的版本。回退后的行为差异写在各包说明里，回退完要重新验证开机锁定、解锁和遥测。

## 给新发布包写说明

放在固件同目录，按这个顺序写，命令里的文件名必须和包内一致，使用相对路径：

1. 这个包改了什么、状态如何（实机验证到哪一步）
2. 文件表：设备、文件、写入地址
3. 刷写前的隔离要求，以及是否必须两端配套
4. 两端各自的完整命令
5. 复测表，未实测的项目标为待测
6. 已知限制
7. 回退方法，以及回退后会失去什么

通用内容（接线细节、备份、读回、排障）指向本文，不要在各包重复。软件验证通过不等于硬件验收。
