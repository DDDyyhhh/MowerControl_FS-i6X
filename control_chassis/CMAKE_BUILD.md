# control_chassis CMake 编译与烧录

本工程使用 CMake + Ninja + Arm GNU Toolchain 15.3.Rel1（GCC 15.3.1）编译 STM32F407 固件，并使用
ST-Link + OpenOCD 通过 SWD 烧录。EIDE 工程保留作历史参考，CMake 是新的命令行构建入口。

## 依赖

确保 CMake、Ninja 和可选的 OpenOCD 在 `PATH` 中：

```bash
cmake --version
ninja --version
openocd --version
```

Ubuntu/Debian 可安装：

```bash
sudo apt install cmake ninja-build openocd
```

Linux x86_64 在仓库根目录执行一次：

```bash
./tools/install_gcc15.sh
```

安装器下载 Arm 官方 15.3.Rel1 归档并校验固定 SHA256，安装到
`~/.local/opt/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi`。重复执行会检查并复用安装；也可将已下载的官方 `.tar.xz` 路径作为第一个参数。构建不会自动联网安装。

底盘与 FS-i6X Linux CMake 工程共享根目录 `cmake/arm-gnu-toolchain.cmake`，固定使用 GCC 15.3.1 及同套 objcopy/size 等工具。脚本自动加载 `tools/gcc15_env.sh`；直接使用 CMake 也会定位默认安装，不需要手动修改系统 PATH。

自定义安装位置可设置 `ARM_GNU_TOOLCHAIN_ROOT` 环境变量，或配置时传入 `-DARM_GNU_TOOLCHAIN_ROOT=/path/to/installation`（指向包含 `bin` 的安装根目录）。Windows 在本机安装对应的 Arm GNU 15.3.Rel1 后，用同一 CMake 参数指定根目录。Keil/ARMCC 和 EIDE 配置沿用各自工具链。

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

Debug 输出位于 `build/gcc15/debug/`，Release 位于 `build/gcc15/release/`：

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

正式 FS-i6X 双端固件（含 CH4/OK、CH5 双向首帧锁定）在仓库根目录执行：

```bash
ASAN_OPTIONS=detect_leaks=0 ./tools/build_blade_ok.sh
```

正式底盘输出为 `control_chassis/build/gcc15/mower-ok-release/control_chassis.{elf,hex,bin,map}`；遥控器为 `OpenI6X/FS-I6X/opentx/build-mower-ok-gcc15/firmware.{elf,bin}`。仅重建遥控器可运行 `tools/build_mower_user_ui.sh`，默认输出到 `build-mower-user-ui-gcc15/`。`MOWER_RADIO_BUILD_DIR` 仍可指定遥控器新构建目录。

旧 `build/debug`、`build/release`、`build/mower-ok-release` 及旧遥控器输出保留，不能当作本次 GCC15 新镜像。切换工具链使用新构建目录；检测到缓存旧编译器时会报错，不会静默复用。Debug/普通 Release 仍是原调试配置，正式车辆交付使用 `mower-ok-release`。

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
  -c "program build/gcc15/debug/control_chassis.elf verify reset exit"
```

若连接了多个 ST-Link，可在配置时指定序列号：

```bash
cmake --preset debug -DSTLINK_SERIAL=你的STLINK序列号
cmake --build --preset flash-debug
```

若 OpenOCD 无法连接，可先检查 USB 权限、线序和目标板供电，再尝试在 OpenOCD
命令中加入 `-c "adapter speed 1000"` 降低 SWD 时钟。
