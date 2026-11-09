# MowerControl FS-i6X

遥控割草机双端固件：`control_chassis/` 是 STM32F407 底盘主控，`OpenI6X/FS-I6X/opentx/` 是 FS-i6X 遥控器固件，`experiments/ticket03/` 是两端共享的协议实现。

## 编译方式

**本项目用 CMake 编译，正式固件的构建入口只有 CMake。** 仓库里的 Keil（`MDK-ARM/`）和 EIDE（`Eide/`）工程只保留并同步源文件清单，不用来出正式固件。`remote_lvgl/` 是旧版 433MHz 手持遥控器，只能用 Keil/EIDE，不在这条构建链路里。

编译产物（`build/`、`build-*`）不进版本库，需要时在本地生成。

底盘工程的更多细节（Windows 环境、OpenOCD 参数、指定 ST-Link 序列号等）见 [control_chassis/CMAKE_BUILD.md](control_chassis/CMAKE_BUILD.md)。

## 环境要求

| 工具 | 要求 | 用途 |
|---|---|---|
| Arm GNU Toolchain | GCC **15.3.1**，构建脚本会检查版本 | 两端 |
| CMake | 3.20 或更高 | 两端 |
| Ninja | 任意较新版本 | 底盘 |
| Make | 系统自带即可 | 遥控器 |
| Python 3 + Pillow | 没有 Pillow 时遥控器字库和位图生成会失败 | 遥控器 |
| Python 包 unicorn | 只有 `build_blade_ok.sh` 的 ARM 遥测检查需要 | 双端正式构建 |

```bash
python3 -m pip install Pillow unicorn
```

设置工具链，从仓库根目录执行，每个新终端都要做一次。Linux 可以用 `./tools/install_gcc15.sh` 安装到默认路径，就不用设置 `ARM_GNU_TOOLCHAIN_ROOT`。macOS 按实际安装位置设置：

```bash
export ARM_GNU_TOOLCHAIN_ROOT=/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi
```

```bash
source tools/gcc15_env.sh
```

`gcc15_env.sh` 会把这套工具链放到 `PATH` 最前面。不执行它的话，CMake 可能选中系统里别的 `arm-none-eabi-gcc`。

## 编译底盘

在 `control_chassis/` 下执行：

```bash
cmake --preset mower-ok-release
```

```bash
cmake --build --preset mower-ok-release
```

产物在 `control_chassis/build/gcc15/<预设名>/`，文件名是 `control_chassis.elf`、`.bin`、`.hex`、`.map`。

| 预设 | 用途 |
|---|---|
| `mower-ok-release` | **正式交付配置** |
| `mower-ok-ibus-300ms` | 与正式配置相同，显式指定 300 ms i-BUS 超时 |
| `mower-ok-ibus-100ms` | 100 ms i-BUS 超时的回退配置 |
| `debug`、`release` | 开发配置，不含割刀 OK 流程（未定义 `MOWER_BLADE_OK_ENABLED`） |
| `exp3-hil`、`exp3-pwm-hil` | Ticket03 台架配置，不是正式固件 |

`debug`、`release` 编译通过不代表正式固件能编译。改动割刀 OK 流程或 `experiments/ticket03/` 后，要构建 `mower-ok-release`。

`MOWER_IBUS_TIMEOUT_MS` 只接受 100 或 300，其它非法的宏组合会被 `#error` 拒绝。

## 编译遥控器

遥控器工程的 CMake 参数较多，请用脚本，从仓库根目录执行：

```bash
./tools/build_mower_user_ui.sh
```

产物是 `OpenI6X/FS-I6X/opentx/build-mower-user-ui-gcc15/firmware.bin`。Python 不是默认的 `python3` 时，用 `MOWER_UI_PYTHON` 指定；构建目录可以用 `MOWER_RADIO_BUILD_DIR` 改。

## 一次编译两端并跑回归

```bash
ASAN_OPTIONS=detect_leaks=0 ./tools/build_blade_ok.sh
```

脚本先运行 `tools/verify.sh all`，再构建底盘 `mower-ok-release` 和遥控器（输出到 `build-mower-ok-gcc15`）。它只构建，不刷写任何板子。

macOS 上运行回归测试前，还要设置下面两项，否则 Apple clang 的 `-Werror` 和缺少 GNU `--wrap` 会让 host 测试失败。只编译 ARM 固件时不需要：

```bash
export CC="$PWD/releases/20261106-lifter-15khz/validation/macos-host-cc.py"
```

```bash
export PATH="$PWD/releases/20261106-lifter-15khz/validation/macos-bin:$PATH"
```

## 刷写

见 [docs/flashing.md](docs/flashing.md)。每个 `releases/` 发布包内另有该版本的刷写与复测说明。软件验证通过不等于硬件验收。

底盘也可以用 CMake 的 `flash` 目标经 ST-Link 烧录，**命令会实际写板**，例如 `cmake --build --preset flash-debug`。
