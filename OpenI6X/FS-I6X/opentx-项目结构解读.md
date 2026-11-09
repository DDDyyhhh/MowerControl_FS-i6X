# OpenI6X / opentx 项目结构解读

- 仓库：https://github.com/OpenI6X/opentx
- 本地路径：`C:\Users\20147\WorkBuddy\2026-09-14-09-34-14\opentx`
- 克隆方式：`git clone --depth 1`（3453 个文件，85 MB）
- HEAD：`f64ed45` "bump version"（2026-09-13，master）
- 许可证：GPL-2.0
- 版本号：`2.3.0`（定义于顶层 `CMakeLists.txt`，`VERSION_MAJOR/MINOR/REVISION`）

---

## 0. 这是什么

一句话：**把定制版 EdgeTX / OpenTX 移植到 FlySky i6X 等基于 STM32F0 的遥控器上。**

它是 OpenTX 2.3 分支的一个硬件移植 fork，核心增量为一块新板型支持（`PCBI6X` / `I6X`），
以及为适配 STM32F072 + 128×64 单色屏 + A7105 射频所做的裁剪与魔改。

相对原厂 i6X 固件的能力提升（来自 README）：

| 项 | 原厂 | OpenI6X |
|---|---|---|
| 通道 | 6/10 | 16 |
| 混控 | 3 | 32 |
| 协议 | AFHDS / AFHDS2A / PPM | 16Ch AFHDS2A / PPM / CRSF |
| 语音播报 | 无 | 有（需加装 DFPlayer） |
| ExpressLRS | 无 | 内建配置器（无需 LUA） |
| USB 模式 | 摇杆 | 摇杆 / 存储 / 串口（遥测镜像、调试） |

---

## 1. 顶层目录

| 目录 / 文件 | 作用 | 关键内容 |
|---|---|---|
| `radio/` | **机载固件本体**（C++ / ARM） | `src/` `targets/` `sdcard/` `util/` `data/` |
| `companion/` | **PC 上位机**（Qt5 桌面程序） | 模型编辑、固件刷写、EEPROM 读写、模拟器 |
| `tools/` | 构建与代码生成脚本 | `build-flysky.py`、`fwoptions.py`、`img2lbm.py`、`tts*.py`、协议解析脚本 |
| `cmake/` | CMake 模块 | `Macros.cmake`（含 `git_id()` 把 commit 号注入版本）、`Bitmaps.cmake`、各种 `Find*.cmake` |
| `doc/` | 文档与硬件改装图 | `flysky/`（背光改装、DFU 跳线、ST-Link 引脚、加装电源电路） |
| `sound/` | 语音合成工程 | `recorder/`、`speaker/`（VS 解决方案） |
| `CMakeLists.txt` | 顶层构建入口 | 版本定义、Qt/SDL/Fox 探测、`add_subdirectory(radio/src)` |
| `Dockerfile` | 构建环境镜像 | 一键得到完整交叉编译工具链 |
| `.circleci/config.yml`、`.travis.yml` | CI | 自动多语种多板型构建 |
| `.gitmodules` | 子模块 | 仅一个：`radio/src/thirdparty/GCS_MAVLink/include_v1.0` ← `mavlink/c_library_v1` |

---

## 2. `radio/` —— 机载固件（重点）

```
radio/
├── src/            核心源码（695 个文件、约 18.4 万行，不含 thirdparty）
├── sdcard/         烧到 SD 卡的内容：语音、位图、脚本、模型模板
├── util/           构建期工具：位图/字体/语音转换、协议解析、TTS 生成
├── data/           示例模型与日志
└── releasenotes.txt
```

### 2.1 `radio/src/` 的分层

自底向上（也是推荐的阅读顺序的反向）：

| 层 | 目录 / 文件 | 职责 |
|---|---|---|
| 硬件抽象 | `targets/` | 板级差异全部集中在这里 |
| 驱动 | `targets/common/arm/stm32/` | 跨板共用：ADC、音频 DAC、USB（CDC/HID/MSD）、Flash、定时器、摇杆 PWM、调度器、辅助串口 |
| 核心逻辑 | `opentx.cpp` / `main.cpp` / `mixer.cpp` / `mixer_scheduler.cpp` | 启动、主循环、混控运算与实时调度 |
| 模型数据 | `model_init.cpp`、`myeeprom.h`、`datastructs.h`、`gvars.cpp`、`curves.cpp` | 模型结构体、全局变量、曲线、定时器 |
| 功能模块 | `telemetry/` `pulses/` `storage/` `lua/` `trainer.cpp` `sdcard.cpp` `audio.cpp` `gps.cpp` | 遥测、发射脉冲、持久化、脚本、教练、SD 卡、音频、GPS |
| UI | `gui/` + `gui/navigation/` | 三个分辨率族 + 导航逻辑 |
| 其他 | `thirdparty/`、`translations/`、`bitmaps/`、`fonts/`、`tests/` | 第三方库、多语言、位图字体、单元测试 |

### 2.2 `radio/src/targets/` —— 板型抽象

```
targets/
├── common/arm/stm32/     共用 STM32 驱动（含 bootloader/ 与 f0/f2/f4 子目录）
├── flysky/               ★ i6X 专属（本 fork 的核心）
├── taranis/  horus/      其它官方板型
└── simu/                 模拟器后端
```

`targets/flysky/` 逐文件说明：

| 文件 | 作用 |
|---|---|
| `board.cpp/.h`、`hal.h` | 板级初始化与硬件抽象接口定义 |
| `A7105_SPI.cpp`、`AFHDS2A_a7105.cpp`、`iface_a7105.h` | A7105 射频芯片 SPI 驱动 + AFHDS2A 协议实现 |
| `intmodule_pulses_driver.cpp` | 内置高频头的脉冲生成 |
| `extmodule_driver.cpp` | 外置模块（CRSF / PPM） |
| `adc_driver.cpp`、`keys_driver.cpp`、`lcd_driver.cpp`、`backlight_driver.cpp` | 摇杆采样、按键、LCD、背光 |
| `buzzer_driver.cpp`、`dfplayer_driver.cpp`、`i2c_driver.cpp` | 蜂鸣器、DFPlayer 语音模块、I2C |
| `pwr_driver.cpp` | 电源管理与关机 |
| `telemetry_driver.cpp`、`trainer_driver.cpp`、`crc_driver.cpp` | 遥测串口、教练口、CRC 硬件加速 |
| `startup_stm32f072.s`、`STM32F072VB_FLASH.ld`、`stm32_ramboot.ld` | 启动汇编与链接脚本 |
| `bootloader/boot_menu.cpp` | 升级用 Bootloader 菜单 |
| `tools/` | 板内小工具：`elrs.cpp`（ExpressLRS 配置器）、`inav.cpp`（INAV Lite 遥测页）、`hello.cpp` |

### 2.3 构建配置：PCB 与 flavor

顶层 `radio/src/CMakeLists.txt` 定义可选板型：

```
PCB_TYPES = X7 XLITE X9D X9D+ X9E X10 X12S SKY9X 9XRPRO AR9X I6X
```

选中 `I6X` 时（`targets/flysky/CMakeLists.txt`）设置：

| 配置 | 值 |
|---|---|
| CPU | `STM32F0` / `STM32F072` |
| GUI 分辨率 | `128x64` |
| 导航类型 | `9x` |
| 链接脚本 | `STM32F072VB_FLASH.ld` |
| LUA | 关闭（Flash 太小） |
| 内存分配 | NANO（nano newlib + bin allocator） |
| 默认协议 | `AFHDS2A` |

`flysky/CMakeLists.txt` 顶部是一批**尺寸敏感的功能开关**，注释里直接标了占用的 kB：

`PCBI6X_ELRS`（~4.0 kB）、`PCBI6X_INAV`（~2 kB）、`PCBI6X_HELLO`、`USB_MSD`（3.2 kB）、
`DFPLAYER`（~1.7 kB）、`SBUS_TRAINER`（~0.6 kB）、`CRSF_EXTENDED_TYPES`（~0.2 kB）、
`FAKE_RSSI`、`AFHDS2A_LQI_CH`、`STICK_DEAD_ZONE`、`SHUTDOWN_CONFIRMATION`、`INVERT_GIMBAL_CH1..4`。

> 这是理解本项目构建的关键：**每个 feature 都是一个个开关，因为 128 kB Flash 要用到最后一个字节。**
> `tools/build-flysky.py` 里定义了发布组合：`I6X` / `I6X_DFPLAYER` / `I6X_HELI` / `I6X_HELI_DFPLAYER` × 10 种语言。

### 2.4 功能子系统明细

| 目录 | 内容 |
|---|---|
| `telemetry/` | `flysky_ibus.cpp`（i6X 走的就是这个）、`frsky.cpp`、`frsky_sport.cpp`、`crossfire.cpp`、`spektrum.cpp`、`multi.cpp`、`telemetry_sensors.cpp` |
| `pulses/` | `ppm.cpp`、`pxx1/pxx2.cpp`、`sbus.cpp`、`dsm2.cpp`、`crossfire.cpp`、`multi.cpp` |
| `storage/` | `eeprom_common/raw/rlc/conversions`、`sdcard_raw`、`rambackup`、`modelslist`、`rlc.cpp`（行程压缩） |
| `gui/` | `128x64/`（i6X 实际使用）、`212x64/`、`480x272/`（彩色屏）、`common/stdlcd`、`navigation/navigation_9x.cpp` |
| `lua/` | `api_lcd.cpp`、`api_model.cpp`、`api_general.cpp`、`widgets.cpp`（i6X 默认关闭） |

---

## 3. `companion/` —— PC 上位机

Qt5 桌面程序（`Qt5Core/Widgets/Xml/LinguistTools/PrintSupport/Multimedia/Svg` + SDL 1.x，非 Windows 还需 Fox 库）。

| 部分 | 内容 |
|---|---|
| 主界面 | `mainwindow.cpp`、`mdichild.cpp` |
| 模型编辑 | `modeledit/`、`generaledit/`、`radio.xsd`（模型 XML Schema） |
| 固件刷写 | `firmwares/`、`process_flash.cpp`、`downloaddialog.cpp`、`flashfirmwaredialog.cpp` |
| EEPROM 读写 | `flasheepromdialog.cpp`、`process_copy.cpp`、`process_sync.cpp` |
| 数据模型 | `datamodels/` |
| 模拟器 | `simulation/`（`libsimulator`，**直接复用 `radio/src` 源码**） |
| 平台打包 | `targets/{linux,mac,windows}` |
| 翻译 | `translations/`、`companion.qrc` |

关键点：**上位机与固件共享同一份业务源码**。`simulation/` 把 `radio/src` 编成库，用 Qt/SDL 窗口替代真实硬件，
所以模拟器里能跑的就是真机逻辑 —— 这是调试混控/界面的最高效途径。

---

## 4. `tools/` 与 `cmake/`

| 脚本 | 用途 |
|---|---|
| `build-flysky.py` / `build-opentx.py` / `build-firmware.py` | 批量构建固件（多板型 × 多语言） |
| `fwoptions.py` | 固件可选项解析 |
| `bin2lbm.py` / `img2lbm.py` / `font2png.py` / `taranisicons.py` | 位图与字体转换（生成 LCD 用的 LBM 格式） |
| `translate.py` / `addtr.py` / `tts*.py` | 翻译与语音合成（`tts_en.py` / `tts_cz.py` / …） |
| `parse.py` / `sport-parse.py` / `crossfire-parse.py` | **串口/协议报文解析辅助脚本** |
| `elf-size-report.sh` / `elf-size-compare.awk` | 固件体积报告与对比（Flash 紧张，必须盯） |
| `codeformat.sh` + `uncrustify.cfg` | 统一代码风格 |

`cmake/Macros.cmake` 里的 `git_id()` 会把当前 commit 注入固件版本字符串（启动画面可见）。

---

## 5. 第三方依赖（`radio/src/thirdparty/`，450 个文件）

| 库 | 用途 |
|---|---|
| `STM32F0xx / STM32F2xx / STM32F4xx_StdPeriph_Lib_*` + `CMSIS` | 官方标准外设库（i6X 用 F0） |
| `CoOS/` | 实时内核（协作式调度） |
| `Lua/src/` | Lua 5.2 源码（i6X 默认关闭） |
| `STM32_USB-Host-Device_Lib_V2.2.0`、`STM32F0x2_USB-FS-Device_Lib_V1.0.0` | USB 协议栈 |
| `GCS_MAVLink/include_v1.0` | **git 子模块**，MAVLink v1 头文件 |
| `mini_print/`、`FatFs`、`windows/dirent` | 精简 printf、文件系统、Windows 兼容层 |

> 注意：`--depth 1` 克隆不会拉子模块。若要完整构建含 MAVLink 的功能，需补：
> `git submodule update --init --recursive`

---

## 6. 构建与产物

```bash
# 依赖：CMake >= 3.13、Python3、arm-none-eabi-gcc、（上位机）Qt5 + SDL 1.2
python3 tools/build-flysky.py            # 批量出 i6X 固件
# 或手动
cmake -DPCB=I6X -DTRANSLATIONS=EN ..     # 交叉编译
```

- 顶层 `CMakeLists.txt` 恒编译 `radio/src`；`companion` 仅在找到 Qt5 且未开 `DISABLE_COMPANION` 时编译。
- 交叉编译时所有 `CMAKE_CXX_FLAGS` 被清空，额外 flag 需通过 `FIRMWARE_C_FLAGS` / `FIRMWARE_CXX_FLAGS` 传入。
- 产物：STM32F072VB 的 `.bin` / `.dfu`，通过 SD 卡刷写或 ST-Link / DFU 烧录。
- `Dockerfile` 提供完整构建环境；CI 见 `.circleci/config.yml`。

---

## 7. 代码规模

| 指标 | 数值 |
|---|---|
| 仓库文件数 / 体积 | 3453 / 85 MB |
| 核心源码（排除 thirdparty） | 695 个文件，≈ 18.4 万行 |
| 第三方源码 | 450 个文件 |

---

## 8. 与 GPS / 协议逆向工作的结合点

如果你的关注点在 GPS 数据与私有协议解析，这个仓库里有几个现成可参考的模块：

| 位置 | 价值 |
|---|---|
| `radio/src/gps.cpp` / `gps.h` | 机载 GPS（NMEA）解析与坐标处理的成熟实现 |
| `radio/src/telemetry/flysky_ibus.cpp` | FlySky iBUS 遥测帧的字节级解析范例 |
| `radio/src/telemetry/frsky_sport.cpp`、`crossfire.cpp` | Sport / CRSF 异步串口帧状态机 |
| `radio/util/parse.py`、`sport-parse.py`、`crossfire-parse.py` | 直接可用的**离线报文解析脚本**，思路与你逆向 `192.168.230.97:9999` 时"抓原始字节 → 验证假设"完全一致 |
| `radio/src/targets/common/arm/stm32/aux_serial_driver.cpp` | 辅助串口驱动（遥测镜像 / 调试输出）的环形缓冲与 DMA 实现 |

---

## 9. 建议的阅读顺序

1. `README.md` + GitHub Wiki（用户手册、刷新方法、硬件改装）
2. `CMakeLists.txt` → `radio/src/CMakeLists.txt` → `radio/src/targets/flysky/CMakeLists.txt`
   （先把**编译单元和开关**搞清楚，否则看代码会迷路）
3. `radio/src/main.cpp` / `opentx.cpp` —— 启动流程与主循环
4. `radio/src/mixer.cpp` + `mixer_scheduler.cpp` —— 实时混控
5. `radio/src/gui/128x64/` + `gui/navigation/navigation_9x.cpp` —— i6X 实际界面
6. `radio/src/targets/flysky/board.cpp` + `hal.h` —— 板级初始化
7. `radio/src/storage/` —— EEPROM / SD 卡模型存取
