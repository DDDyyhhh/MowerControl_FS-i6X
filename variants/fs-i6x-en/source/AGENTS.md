# Repository Guidelines

## Project Overview
本项目为**商用割草机嵌入式双节点控制系统**，基于 STM32F407（ARM Cortex-M4）平台，采用 SVN 版本控制。整个工作区划分为两个独立固件子工程：
- **`control_chassis`（底盘主控板）**：负责整车安全监控、状态机调度、差速底盘动力控制（支持 MSSD / OID / ODrive 电机驱动器）、割刀驱动控制（Flipsky FTESC CAN / PWM）、推杆升降控制、超声波避障、IMU 姿态解算及 RK3588 上位机 CAN API 通信。
- **`remote_lvgl`（手持无线触摸遥控器）**：基于 LVGL 图形库、ADC 摇杆、按键旋钮输入，通过无线串口模块（E34 / A28 433MHz）与底盘进行双向遥控与状态监控回显。

---

## Architecture & Data Flow

### 1. 系统分层架构
工程遵循嵌入式分层设计：
```
[硬件外设 / BSP]
    ├── STM32 HAL / CMSIS (Drivers/)
    └── 传感器与执行器驱动 (Core/UserDev/ 及 Drivers/)
[中间件层 (Middlewares/)]
    ├── FreeRTOS (CMSIS-RTOS v2)
    └── LVGL v8 / UI (remote_lvgl)
[应用层 (Core/UserApp/)]
    ├── 全局运行上下文与配置 (app_context / app_vars)
    ├── 核心状态机与控制循环 (app_Motion / app_control)
    └── 通信管理 (app_Comm / app_SerialTX / can)
```

### 2. 数据流与通信拓扑
```
[手持遥控器 remote_lvgl]
   │ 摇杆 ADC / 按键 / 旋钮输入 (app_control.c, 10ms 扫描, 50ms 错峰发送)
   │ 433MHz 无线透传 (E34/A28 UART DMA: 0xAB..0xBA 摇杆, 0xCC..0xDD 按键)
   ▼
[底盘控制板 control_chassis]
   ├── Task_Comm (20ms/异步队列): UART DMA 接收帧，更新全局上下文 g_app
   ├── Task_Motion (100Hz/10ms 周期): 硬件安全检测 -> IMU解算 -> 50Hz运动与避障限速 -> 驱动控制
   └── Task_Report / Task_WirelessTx: 打包状态回传至遥控器与上位机
   ▲
   │ CAN 2.0B (500 kbps, 50ms 周期: 0x005 控制, 0x507 状态, 0x50A 四轮线速度)
[RK3588 上位机 / ROS2 自主导航系统]
```

### 3. 底盘核心模块与抽象
- **底盘驱动抽象**：`motor_driver_config.h` 宏配置 `MOTOR_DRIVER_USED`，统一由 `chassis_control.c` 调度 `mssd_can.c`（美思固）、`oid_can.c`（欧依达）或 `odrive_can.c`（ODrive）。
- **割刀驱动抽象**：`blade_motor_config.h` 配置 `BLADE_MOTOR_DRIVER_USED`（Flipsky CAN `flipsky_can.c` 或 PWM `BladeMotor.c`）。
- **避障与姿态**：`sr04_uart.c` 处理 4 路超声波雷达测距；`icm42688.c` + `bsp_imu_solver.c` 实时解算坡度与翻滚角。

---

## Key Directories

```
MowerControl_Workspace/
├── tools/                           # 统一验证与安全检查工具 (verify.sh, svn_preflight.sh)
├── control_chassis/                 # 底盘主控固件工程
│   ├── CONTEXT.md                   # 底盘架构领域导航与子系统索引 (Progressive Disclosure)
│   ├── Core/
│   │   ├── Inc/ & Src/              # HAL 外设初始化、中断、FreeRTOS 配置 (freertos.c)
│   │   ├── UserDev/                 # 底层硬件与外设驱动 (IMU、BMS、CAN驱动、超声波、遥控后端等)
│   │   └── UserApp/                 # 业务逻辑 (app_context.c, app_Motion.c, safety_ownership_gate.c 等)
│   ├── host/                        # 确定性主机回归测试脚手架 (8 组 C harnesses)
│   ├── MDK-ARM/                     # Keil MDK-ARM uVision5 工程 (concrol.uvprojx)
│   ├── CMakeLists.txt               # ARM GCC CMake 固件构建清单
│   ├── CMakePresets.json            # Debug/Release/烧录预设
│   ├── cmake/                       # Arm GNU Toolchain 配置
│   └── Eide/                        # 历史 EIDE 工程配置（兼容保留）
│   └── RK3588_CAN协议手册.md         # 上位机接口协议规范
├── remote_lvgl/                     # 手持遥控器固件工程
│   ├── Core/
│   │   ├── Inc/ & Src/              # 外设初始化、中断及 FreeRTOS 入口
│   │   ├── UserDev/                 # LCD ST7789、FT6336U 触摸屏、ADC、按键驱动
│   │   └── UserAPP/                 # 遥控业务任务 (app_control.c, app_comm.c, app_gui.c)
│   ├── Middlewares/UI_APP/          # LVGL 生成的界面代码及资源
│   ├── MDK-ARM/                     # Keil MDK-ARM uVision5 工程 (yaokon.uvprojx)
│   └── Eide/                        # VSCode EIDE 工程配置
├── docs/                            # 团队规范、协作与架构文档
│   ├── agents/                      # Agent 协作规范 (issue-tracker, triage-labels, domain)
│   └── retrospectives/              # 工程化复盘与改进记录
└── .scratch/                        # 需求与缺陷追踪 Markdown 工作区 (按特性划分目录)
```

---

## Development Commands

本项目为 STM32 纯 C 语言裸机/FreeRTOS 固件，无 Node/Bun 依赖。底盘工程的规范构建使用 CMake + Arm GNU Toolchain，并保留 Keil MDK 工程：

### 1. 规范验证与构建命令 (Canonical Entrypoint)
- **仓库唯一验证入口 (Linux / macOS / WSL)**:
  ```bash
  ./tools/verify.sh host      # 编译并运行全套 8 组主机确定性回归测试 (秒级完成)
  ./tools/verify.sh chassis   # 使用 CMake/ARM GCC 完整构建底盘固件 (0 Error)
  ./tools/verify.sh all       # 全量验证 (合并执行 host + chassis 检查)
  ```
- **SVN 提交前流水线检查**:
  ```bash
  ./tools/svn_preflight.sh    # 检查冲突、构建垃圾、CRLF换行畸变、高危文件及生产清单
  ```
- **Windows / Keil MDK-ARM (可选命令行构建)**:
  ```cmd
  UV4 -b control_chassis\MDK-ARM\concrol.uvprojx -j0 -o build.log
  UV4 -b remote_lvgl\MDK-ARM\yaokon.uvprojx -j0 -o build.log
  ```

### 2. 新增生产代码集成规则
- **必须同步登记到工程清单**：在 `Core/UserApp/`、`Core/UserDev/` 或 `Core/Src/` 新增任何生产 `.c` 文件后，必须**同时**加入：
  1. `control_chassis/CMakeLists.txt` (CMake manifest)
  2. `control_chassis/MDK-ARM/concrol.uvprojx` (Keil manifest)
- **提交前自检**：运行 `./tools/svn_preflight.sh`，确保第 5 项检查 `[PASS]`。
### 3. 清理临时编译文件
- 双击运行根目录或子工程下的 `keilkill.bat`，清理 Keil 生成的 `.bak`, `.d`, `.o`, `.crf`, `.axf` 等中间文件。

### 4. 代码格式化与 Lint
- 采用 C99 规范，可使用 `clang-format -i` 对 `Core/UserApp/` 和 `Core/UserDev/` 代码进行格式化（缩进 4 空格，保持现有风格）。

---

## Code Conventions & Common Patterns

### 1. 命名规范
- **文件命名**：外设驱动一般采用 `小写下划线.c`（如 `mssd_can.c`, `flipsky_can.c`）或历史遗留的驼峰（如 `BladeMotor.c`）；任务应用统一使用 `app_模块名.c`。
- **结构体与类型**：`typedef struct { ... } TypeName_t;`，枚举使用 `_e` 或 `_t` 结尾。
- **全局实例**：核心全局变量以 `g_` 开头（如 `g_app`, `g_sys_config`）。
- **函数命名**：驱动层前缀大写驼峰加下划线（如 `MSSD_CAN_SendSpeed(...)`），系统任务入口形如 `StartTask_Motion(...)`。

### 2. 状态管理与并发控制
- **全局上下文中心模式**：底盘运行状态集中在 `AppContext_t g_app`（定义于 `app_context.c`），禁止零散定义未经管理的跨文件全局变量。遥控器状态集中在 `app_vars.h`。
- **中断与线程分离**：
  - 中断服务函数（ISR）仅负责 DMA 搬运、标志清除以及通过 `osMessageQueuePut` / 信号量投递，严禁在 ISR 中做长时间阻塞或协议深度解析。
  - 任务间通信优先使用 FreeRTOS 队列（`osMessageQueue`）或二值信号量，共享硬件总线（如 RS485、UART Tx）使用互斥锁（`osMutexId_t`）。

### 3. 实时性与安全约束
- **确定性周期延时**：硬实时控制任务（如 `Task_Motion`）**必须**使用 `vTaskDelayUntil` 保证 10ms (100Hz) 绝对周期，禁止使用可能产生时间漂移的 `osDelay`。
- **双重/多重安全锁存 (Safety Latch)**：
  - 碰撞杆、急停按钮触发后必须硬件锁存（`g_collision_latched = 1`），哪怕物理回弹，软件仍保持停车红灯状态，必须由特定解锁序列或上位机明确清除指令（`magic_key = 0xAA`）解锁。
  - 状态切换必须遵循状态机规范（如 RK3588 API 必须先进入待机 `ctrl_mode=1`，确认无拦截后才能切入运行 `ctrl_mode=2`）。

---

## Important Files

| 相对路径 | 说明 |
| --- | --- |
| `control_chassis/Core/Inc/app_context.h` | 底盘系统全局数据结构体 `AppContext_t`、按键映射、速度档位、安全状态定义 |
| `control_chassis/Core/UserApp/app_context.c` | 底盘全局实例 `g_app` 的初始化（默认进入安全上锁状态） |
| `control_chassis/Core/UserApp/app_Motion.c` | 底盘 100Hz 核心控制循环、安全联锁、状态机、差速驱动计算与输出 |
| `control_chassis/Core/UserApp/app_Comm.c` | 底盘无线串口数据解析任务（0xAB/0xCC 帧解包） |
| `control_chassis/Core/Inc/motor_driver_config.h` | 底盘行走电机驱动器选择宏定义（ODrive / MSSD / OID） |
| `control_chassis/Core/Inc/blade_motor_config.h` | 割刀电机驱动选择（PWM / Flipsky FTESC CAN） |
| `control_chassis/RK3588_CAN协议手册.md` | RK3588 上位机与底盘的 CAN 通信协议文档（0x005, 0x507, 0x50A） |
| `remote_lvgl/Core/UserAPP/app_control.c` | 遥控器摇杆 ADC 读取、按键状态机扫描与错峰发送任务 |
| `remote_lvgl/Core/Inc/app_vars.h` | 遥控器全局变量与配置声明 |

---

## Runtime/Tooling Preferences

- **版本控制**：仓库使用 **Apache Subversion (SVN)** 进行源码管理，无 GitHub/GitLab 远端。请勿假设 git 命令可用。
- **开发与编译环境**：
  - Windows: Keil MDK-ARM v5.30+ (Compiler 5 / ARMCC, 兼容 Compiler 6)，STM32CubeMX `.ioc` 辅助引脚配置。
  - VSCode: 推荐配合 CMake Tools、Cortex-Debug 与 clangd 插件。
- **硬件平台**：STM32F407ZET6 / STM32F407VET6 (168MHz, 512KB/1MB Flash, 192KB RAM)。

---

## Testing & QA

- **分层测试体系**：
  1. **主机确定性测试脚手架 (`./tools/verify.sh host`)**：
     - 覆盖遥控后端抽象、SERVO i-BUS 解析、双边沿授权状态机、刀盘启动物理确认状态机、安全仲裁门、遥测快照与 FTESC CRC 解包。
     - 具备确定性时间注入与毫秒级边界故障注入能力，确保安全协议与状态机不依赖实机硬件即可完成穷尽回归。
  2. **静态与交叉编译检查 (`./tools/verify.sh chassis`)**：
     - 使用 Arm GNU Toolchain (`arm-none-eabi-gcc`) 验证 Cortex-M4 目标代码完整编译，确保零 Error 且不引入新 Warnings。
  3. **硬件实机/HIL 联调测试**：
     - **通信超时保护**：断开遥控器/上位机连接，验证底盘是否在 100ms 内停车并切入安全锁定。
     - **安全硬件中断**：触发碰撞开关或急停按键，验证电机输出是否毫秒级清零。
     - **电机与割刀状态回环**：监测 CAN 回显帧（`0x507`, `0x50A`）与遥控器回显状态是否同步。
     - **堆栈与内存监控**：遥控器已集成 `SystemMonitor_t g_SysMonitor`，在 Watch 窗口关注 `GUI_FreeStack` 和 `FreeHeapSize`。
---

## Issue Tracker & Workflow

- **任务与缺陷跟踪**：
  - 所有 Issue 与 Spec 均以 Markdown 格式沉淀于根目录 `.scratch/<feature-slug>/` 下。
  - 分类标签遵循标准五标签体系：`needs-triage`、`needs-info`、`ready-for-agent`、`ready-for-human`、`wontfix`（详见 `docs/agents/triage-labels.md`）。
- **领域文档与导航结构**：
  - 底盘主控板架构导航：`control_chassis/CONTEXT.md`（按需渐进式阅读核心子系统与路径）。
  - 跨板级共享决策记录于根 `docs/adr/`。
  - 验收与完成标准参见 `docs/agents/issue-tracker.md` 中的 `任务完成标准 (Definition of Done)`。
