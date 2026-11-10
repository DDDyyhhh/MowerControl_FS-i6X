# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 仓库组成

遥控割草机的双端固件。代码同时放在两处：公司 **SVN**（`^/branches/MowerControl_FS-i6X`，主版本库）和 GitHub `DDDyyhhh/MowerControl_FS-i6X` 的 `main` 分支（镜像）。

- 本目录是 SVN 工作副本，没有 `.git`，日常提交走 SVN。
- 同步 GitHub 时，在另外的 git 克隆里覆盖内容后再提交，不要强推。
- GitHub 的 `.gitignore` 不收 `Eide/build`、`*.uvguix.*` 等本地产物。OpenTX 文件在 SVN 中是 CRLF，在 git 中是 LF，只是换行符不同。
- 旧的 CH8 控制源开发线（协议 0x33）只保存在 GitHub 标签 `ch8-mode-import`，SVN 里没有。

| 目录 | 内容 |
| --- | --- |
| `control_chassis/` | 底盘主控：STM32F407 + FreeRTOS（CMSIS-RTOS2）+ HAL，C99 |
| `OpenI6X/FS-I6X/opentx/` | FS-i6X 遥控器固件（OpenTX 分支，C++）；割草机界面在 `radio/src/gui/128x64/mower_*.h` 和 `view_main.cpp` |
| `experiments/ticket03/` | 名为实验，实为**双端正式代码**：`MOWER_BLADE_OK` 构建会把它编入底盘和遥控器，是两端共享的协议实现 |
| `tools/` | 统一验证入口、构建脚本、`test_*.py` 回归测试 |
| `docs/protocols/`、`docs/adr/` | 跨板协议契约（当前接口）与设计决策（选择原因），两者不混写 |
| `remote_lvgl/` | 旧版 433MHz 手持遥控器（仅 Keil/EIDE）；CMake 构建固定 `REMOTE_BACKEND_FLYSKY_IBUS=1`，不走这条链路 |
| `releases/`、`variants/fs-i6x-en/` | 交付包与冻结快照，只作记录，不在其中做功能修改 |

## 常用命令

全部从仓库根目录执行。

```bash
./tools/verify.sh host      # 全部 host harness（约 1 分钟）
./tools/verify.sh chassis   # 底盘 ARM 构建（仅 debug preset）
./tools/verify.sh all       # 以上两项
./tools/svn_preflight.sh    # 提交前只读自检：冲突、产物、换行、高危文件、源码清单
```

**在 macOS 上必须先设置以下环境**，否则 host 测试因 Apple clang 的 `-Werror` 和缺少 GNU `--wrap` 而失败，`gcc15_env.sh` 也找不到工具链（默认路径是 Linux x86_64）：

```bash
export ARM_GNU_TOOLCHAIN_ROOT=/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi
export CC="$PWD/releases/20261106-lifter-15khz/validation/macos-host-cc.py"
export PATH="$PWD/releases/20261106-lifter-15khz/validation/macos-bin:$PATH"
```

运行单项测试：

```bash
python3 tools/test_can_tx_scheduler.py    # tools/test_*.py 可单独运行（遵循上面的 CC）
./tools/verify_blade_ok_host.sh           # 正式 OK 流程整组
```

`host/*_harness.c` 没有单项入口：到 `tools/verify.sh` 里复制对应的编译命令，其中的宏定义是测试的一部分，不要省略。失败日志在 `build/logs/host_*.log`。

底盘构建（在 `control_chassis/` 下，工具链固定 GCC 15.3.1）：

```bash
cmake --preset mower-ok-release && cmake --build --preset mower-ok-release   # 正式交付配置
cmake --preset debug && cmake --build --preset flash-debug                   # 构建并经 ST-Link 烧录
```

`verify.sh chassis` 只编译 `debug`，而 `debug`/`release` **不定义** `MOWER_BLADE_OK_ENABLED`。改动 `#if MOWER_BLADE_OK_ENABLED` 内的代码、`experiments/ticket03/` 或正式流程时，还要构建 `mower-ok-release`。

双端正式构建（先跑 `verify.sh all`，不刷写任何板子）：

```bash
ASAN_OPTIONS=detect_leaks=0 ./tools/build_blade_ok.sh   # 另需 Python 包 unicorn、Pillow
./tools/build_mower_user_ui.sh                          # 仅遥控器
```

烧录只通过显式的 `flash` target（底盘 OpenOCD）或 `dfu-util`（遥控器，APM32，VID:PID `314b:0106`）。通用步骤见 `docs/flashing.md`，版本相关的文件名、命令和复测项见各发布包的刷写说明。

## 系统架构

控制与回传链路：

```text
FS-i6X ──RF──> FS-iA10B ──SERVO i-BUS──> USART1 ──> Task_Comm ──快照──> Task_Motion(100Hz) ──> Task_CANTx ──> OID 轮电机 / FTESC 割刀
   ^                                                                        │
   └──RF── 接收机 <──SENS i-BUS── USART2 (sens_ibus_responder) <── telemetry_snapshot <─┘
```

RK3588 上位机经 CAN 接入（`0x005` 控制，`0x507` 状态，见 `control_chassis/RK3588_CAN协议手册.md`），与遥控器竞争控制权，由 SafetyOwnershipGate 仲裁。

**底盘任务**（`Core/Src/freertos.c` 建立，实现在 `Core/UserApp/`）：Motion、CANTx、CANRx 为 High；Comm、WirelessTx 为 AboveNormal；BMS_Sensor 为 Normal；Report 为 Low。分层上 `Core/Src` 是 CubeMX 外设代码，`Core/UserDev` 是设备驱动，`Core/UserApp` 是任务和策略。

**并发模型**：ISR 只采集原始字节，协议状态归任务独占。跨任务数据一律单写者发布快照（i-BUS 用短临界区，遥测用序列计数器），读者不等待发布者。Motion 根据快照年龄**独立**判断链路超时，因此 Comm 停顿时依然失效安全。

**动力许可**：`Task_Motion` 每周期调用 `RefreshSafetyOwnershipGate`，所有动力输出都由 `safety_ownership_gate.c` 放行，任何拒绝都强制零输出。安全锁存只能由合法解锁序列清除，不能重放旧命令。

**双端双向协议**（`experiments/ticket03/`）：上行把编码字放在 CH11/CH12，下行走 SENS 私有地址（9 为分字回执，10 为 E9 快速状态，12 为高度，13～15 为快速完成回执）。`EXP3_WIRE_VERSION` 当前是 **0x36**，以 `docs/protocols/startup-authorization-v36.md` 为准；它建立在 v35 的安全条件上，`CONTEXT.md` 和部分协议文档仍写 0x35。协议改动后两端必须配套刷写。界面或 Accepted 回执都不授予动力权限，CAN TXOK 也不等于电机已停，这些是分别验收的状态。

**编译期配置**决定了构建形态，非法组合由 `#error` 拒绝，`verify.sh` 还用负向编译测试锁定这些守卫：

| 宏 | 取值 |
| --- | --- |
| `REMOTE_BACKEND_A28_E34` / `REMOTE_BACKEND_FLYSKY_IBUS` | 严格二选一，正式用 FlySky |
| `BLADE_MOTOR_DRIVER_USED` | 1 = PWM，2 = FTESC CAN（默认） |
| `MOTOR_DRIVER_USED` | ODrive / MSSD / OID（默认） |
| `REMOTE_IBUS_TIMEOUT_MS` | 只接受 100 或 300（默认） |
| `MOWER_BLADE_OK_ENABLED`、`MOWER_GRASS_BAG_CH6_ENABLED`、`EXP3_ENABLED` | 由 `MOWER_BLADE_OK` 等 CMake 选项打开 |

**测试方式**：host harness 直接编译生产 `.c`，只替换 `control_chassis/host/fakes/`、`lifter_fakes/` 中的 HAL 替身。部分 `tools/test_*.py` 按文本提取生产函数体或校验源码（如 `test_api_ownership.py` 依赖 `static void RefreshSafetyOwnershipGate(void)` 的精确写法），重命名或调整这类函数时需同步更新测试。

## 改动约定

- 探索或修改领域术语、架构前，先读 `docs/agents/domain.md` 指定的文档，子系统导航在 `control_chassis/CONTEXT.md`，使用其中定义的术语。
- 改动跨板契约（报文格式、ID、超时、时序）时，同时检查两端实现，决策记入根目录 `docs/adr/`；与现有 ADR 冲突时必须明确指出。
- issue/spec 写在 `.scratch/<feature>/` 下的本地 Markdown（先读 `docs/agents/issue-tracker.md`，完成标准也在其中），分流标签见 `docs/agents/triage-labels.md`。未经要求不要 `svn add`、设置忽略属性、提交 SVN 或推送 GitHub。
- 新增生产 `.c` 要加入 `control_chassis/CMakeLists.txt` 和 `MDK-ARM/concrol.uvprojx`（preflight 强制检查），按完成标准还要加入 `Eide/.eide/eide.yml`。
- `main.c`、`freertos.c`、`stm32f4xx_it.c`、`*.ioc`、`*.ld`、`*.uvprojx` 属高危文件，preflight 会告警，改动需有明确理由。
- 定时器或外设参数要同时改 `Core/Src/*.c` 和 `concrol.ioc`。但 `.ioc` 里的 CAN 仍是过期的 875 kbit/s，实际以 `can.c` 的 500 kbit/s 为准，CubeMX 重新生成前要先核对。
- 外设用途已固定：USART1 专用 i-BUS 接收，USART2 专用 SENS，USART3 仅用于 A28/E34 后端，不要占用作调试输出。
- 软件验证通过不等于硬件验收，波形、刷写、机械结果未实测时要如实说明。
- `README_chassis.md` 的部分参数已过期（如轮径写 240mm，`chassis_control.h` 实际为 210mm），以代码为准。
