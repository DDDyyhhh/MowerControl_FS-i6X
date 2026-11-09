# control_chassis CMake 编译与烧录

本工程使用 CMake + Ninja + Arm GNU Toolchain 编译 STM32F407 固件，并使用
ST-Link + OpenOCD 通过 SWD 烧录。EIDE 工程保留作历史参考，CMake 是新的命令行构建入口。

## 依赖

确保以下命令在 `PATH` 中：

```bash
cmake --version
ninja --version
arm-none-eabi-gcc --version
openocd --version
```

Ubuntu/Debian 可安装：

```bash
sudo apt install cmake ninja-build gcc-arm-none-eabi openocd
```

Windows 可安装 CMake、Ninja、Arm GNU Toolchain 和 OpenOCD，并将各自的 `bin`
目录加入 `PATH`。后续命令在 PowerShell、CMD 或 Git Bash 中均可执行。

## 编译

在 `control_chassis` 目录执行：

```bash
# Debug：-Og -g3，适合调试
cmake --preset debug
cmake --build --preset debug

# Release：-Os，适合发布
cmake --preset release
cmake --build --preset release
```

Debug 输出位于 `build/debug/`：

- `control_chassis.elf`：调试和推荐烧录文件
- `control_chassis.hex`：Intel HEX 固件
- `control_chassis.bin`：裸二进制固件，起始地址 `0x08000000`
- `control_chassis.map`：链接映射文件
- `compile_commands.json`：编辑器/clangd 编译数据库

仓库统一验证入口也已切换为 CMake：

```bash
../tools/verify.sh chassis
../tools/verify.sh all
```

## ST-Link 接线

至少连接 `SWDIO`、`SWCLK`、`GND` 和目标板 `3.3V/Vref`；建议同时连接 `NRST`，
以便异常固件占用调试引脚时仍能连接。不要让 ST-Link 和外部电源同时向目标板反向供电。

## OpenOCD 烧录

连接 ST-Link 后，在 `control_chassis` 目录执行：

```bash
# 自动先编译 Debug，再烧录、校验、复位并运行
cmake --preset debug
cmake --build --preset flash-debug

# 烧录 Release
cmake --preset release
cmake --build --preset flash-release
```

等价的 OpenOCD 原生命令是：

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "program build/debug/control_chassis.elf verify reset exit"
```

若连接了多个 ST-Link，可在配置时指定序列号：

```bash
cmake --preset debug -DSTLINK_SERIAL=你的STLINK序列号
cmake --build --preset flash-debug
```

若 OpenOCD 无法连接，可先检查 USB 权限、线序和目标板供电，再尝试在 OpenOCD
命令中加入 `-c "adapter speed 1000"` 降低 SWD 时钟。
