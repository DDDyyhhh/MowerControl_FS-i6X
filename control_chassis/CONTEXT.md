# control_chassis 领域导航与架构契约 (CONTEXT.md)

本文件为底盘控制板（`control_chassis`）核心子系统的渐进式导航入口（Progressive Disclosure Index）。为降低单次会话上下文压力，本文件仅保留关键模块边界、调用契约与实存代码路径指针。

---

## 1. Remote Backend (遥控后端抽象)

遥控后端隔离层实现了手持无线遥控（A28/E34 433MHz）与航模接收机（FlySky SERVO i-BUS）在编译期的互斥选择与统一状态发布。

- **配置契约**：`control_chassis/Core/Inc/remote_backend_config.h`
  - 必须严格二选一宏定义：`REMOTE_BACKEND_A28_E34` (0 或 1) 与 `REMOTE_BACKEND_FLYSKY_IBUS` (0 或 1)。
- **统一接口**：`control_chassis/Core/Inc/remote_backend.h`
  - 提供 `RemoteBackend_Init`、`RemoteBackend_OnUartRx`、`RemoteBackend_Advance` 与 `RemoteBackend_GetCommand`。
- **核心实现**：`control_chassis/Core/UserDev/remote_backend.c`
- **传统 433MHz 驱动**：
  - `control_chassis/Core/UserDev/A28.c`
  - `control_chassis/Core/UserDev/E34.C`
- **上层接入**：
  - `control_chassis/Core/UserApp/app_Comm.c`（UART 接收与解包触发）
  - `control_chassis/Core/UserApp/app_Motion.c`（周期推进与超时检查）

---

## 2. FlySky i-BUS (SERVO 接收流解析)

负责 FS-iA10B 等接收机 115200bps 串行总线（SERVO i-BUS 14 通道）的双缓冲 DMA 接收与校验解包。

- **模块接口**：`control_chassis/Core/Inc/flysky_ibus_servo.h`
- **模块实现**：`control_chassis/Core/UserDev/flysky_ibus_servo.c`
- **硬件外设**：
  - `control_chassis/Core/Src/usart.c` (USART1 配置为 115200 8N1，使能 DMA 接收)
  - `control_chassis/Core/Inc/usart.h`
- **关键机制**：
  - 帧长 32 字节，前缀 `0x20 0x40`，小端 16-bit 校验和 (`sum = 0xFFFF - sum(bytes[0..29])`)。
  - 序列号保护（Sequence Counter），确保读取端获取原子一致通道快照。

---

## 3. Remote Authorization (遥控授权状态机)

遥控运行权限授权状态机，防止遥控器开机误动或意外恢复。

- **模块接口**：`control_chassis/Core/Inc/remote_authorization.h`
- **模块实现**：`control_chassis/Core/UserDev/remote_authorization.c`
- **核心行为**：
  - FlySky 生产配置（`MOWER_BLADE_OK_ENABLED=1`）：CH8 专用于遥控/API 选择；独立 OK 底盘解锁由 `Core/Inc/mower_mode_control.h` / `Core/UserDev/mower_mode_control.c` 管理，运动任务单写。
  - 共享契约与恢复边界：[CH8 / 底盘 OK 协议](../docs/protocols/ch8-mode-and-chassis-ok.md)。
  - 既有 CH8 双边沿状态机仅供旧配置与独立台架使用。
  - 旧配置双边沿防误触机制：必须经历特定跳变，且摇杆持续回中（Deadband + Hold Time）后方可授予运行许可 (`MANUAL_REMOTE run permission`)。
  - 故障锁存恢复：链路断开或安全锁存后，必须重新走完整授权序列，拒绝任何旧命令直接重放。

---

## 4. SafetyOwnershipGate (安全与控制权仲裁门)

统一仲裁硬件急停、物理碰撞防撞杆、软件锁定以及多控制权源（遥控 vs 上位机 RK3588 API）。

- **模块接口**：`control_chassis/Core/Inc/safety_ownership_gate.h`
- **模块实现**：`control_chassis/Core/UserApp/safety_ownership_gate.c`
- **执行管线接入**：`control_chassis/Core/UserApp/app_Motion.c`
- **核心行为**：
  - 物理与软件双重锁存（`g_collision_latched`、急停、超时）。
  - 任何拒绝条件触发时毫秒级切断动力输出，强制输出清零（Zero Intent）。
  - 锁存恢复需专用解锁序列（上位机 `magic_key = 0xAA` 或遥控器重授权流程）。

---

## 5. Blade Driver Seam (割刀电机驱动层与确认状态机)

割刀动力控制支持传统 PWM 电子调速器与 Flipsky FTESC CAN 驱动，并引入物理确认安全状态机。

- **配置契约**：`control_chassis/Core/Inc/blade_motor_config.h`
  - `BLADE_MOTOR_DRIVER_USED` 宏配置（`BLADE_MOTOR_DRIVER_PWM` 或 `BLADE_MOTOR_DRIVER_FTESC_CAN`）。
- **手动启动确认状态机**：
  - 接口：`control_chassis/Core/Inc/blade_manual_control.h`
  - 实现：`control_chassis/Core/UserApp/blade_manual_control.c`
  - 必须由启动开关触发、中立位校验、确认事件匹配后才进入运行态，任何急停/安全联锁触发立刻停刀并清除意图。
- **逻辑协议契约**：`control_chassis/Core/Inc/blade_hmi_contract.h`（版本化 BladeStartConfirmRequest 与 BladeConfirmOK 协议及校验）
- **CH6 确认事件适配器**：
  - 接口：`control_chassis/Core/Inc/blade_confirm_ch6_adapter.h`
  - 实现：`control_chassis/Core/UserDev/blade_confirm_ch6_adapter.c`
  - 负责 CH6 连续有效帧消抖、首见基线抑制、单调事件序列号生成及单次消费。
- **PWM 驱动实现**：
  - `control_chassis/Core/Inc/BladeMotor.h`
  - `control_chassis/Core/UserDev/BladeMotor.c`
- **FTESC CAN 驱动实现**：
  - `control_chassis/Core/Inc/flipsky_can.h`
  - `control_chassis/Core/UserDev/flipsky_can.c`

---

## 6. Telemetry Snapshot (遥测快照与数据聚合)

将整车电池（BMS）、轮速里程计、姿态、割刀状态及 8-bit 故障字聚合成原子快照，供回传与诊断。

- **模块接口**：`control_chassis/Core/Inc/telemetry_snapshot.h`
- **模块实现**：`control_chassis/Core/UserApp/telemetry_snapshot.c`
- **周期上报任务**：`control_chassis/Core/UserApp/app_Report.c`
- **关键机制**：
  - 无锁序列计数器（Sequence Counter）发布模型，单写多读零死锁。
  - 单位统一（分伏、分安、毫米每秒、极对数 ERPM 换算）。

---

## 7. SENS Responder / USART2 (遥控回显传感总线)

实现 FlySky i-BUS SENS 传感器回显协议，使开源遥控器固件能够实时读取底盘电池电压、速度与状态。

- **协议响应层**：
  - `control_chassis/Core/Inc/sens_ibus_responder.h`
  - `control_chassis/Core/UserDev/sens_ibus_responder.c`
- **硬件传输层**：
  - `control_chassis/Core/Inc/sens_ibus_usart2.h`
  - `control_chassis/Core/UserDev/sens_ibus_usart2.c`
- **外设初始化**：`control_chassis/Core/Src/usart.c` (USART2 半双工/中断驱动)
- **核心行为**：
  - 地址 1..8 为基础遥测；生产确认配置还使用地址 9 确认传输、10 割刀状态、11 草袋连接、12 解锁代次、13 控制状态。共享接口见根 `docs/protocols/`。
  - 固件未完成硬件总线电气调优前，保持被动安全，不意外拉低总线。

---

## 8. CAN / FTESC (底盘动力与刀盘总线)

底盘电机驱动与割刀驱动的 CAN 通信管理。

- **CAN 底层管理**：
  - `control_chassis/Core/Inc/can.h`
  - `control_chassis/Core/Src/can.c`
- **割刀电调驱动**：`control_chassis/Core/UserDev/flipsky_can.c`（实时反馈 0/1/2 帧解包与 CRC16 校验）
- **行走电机驱动器**：
  - 选择配置：`control_chassis/Core/Inc/motor_driver_config.h`
  - 调度分发：`control_chassis/Core/UserDev/chassis_control.c`
  - MSSD 驱动：`control_chassis/Core/UserDev/mssd_can.c`
  - OID 驱动：`control_chassis/Core/UserDev/oid_can.c`
  - ODrive CAN 驱动：`control_chassis/Core/UserDev/odrive_can.c`
- **上位机通信**：`control_chassis/RK3588_CAN协议手册.md`（0x005 控制、0x507 状态回传、0x50A 四轮速度）

---

## 9. Host Harness (主机确定性测试脚手架)

脱离真实物理硬件运行的高覆盖度回归验证套件，支持微秒级确定性时间推进与边界异常注入。

- **统一执行入口**：`tools/verify.sh host`（或 `./tools/verify.sh all`）
- **生产 CH8 / OK 验证**：`host/mower_mode_control_harness.c`、`host/mower_backend_harness.c`、`tools/test_ch8_host_commands.py` 与 `experiments/ticket03/test_release_*.c`；由 `tools/verify.sh host` 执行。
- **Harness 源码清单**：
  - `control_chassis/host/blade_driver_selection_harness.c`（驱动编译期排他与宏安全守卫）
  - `control_chassis/host/blade_manual_control_harness.c`（手动刀盘安全确认状态机与时序测试）
  - `control_chassis/host/blade_confirm_ch6_adapter_harness.c`（CH6 确认事件适配器、基线建立、去重防重放与状态机集成测试）
  - `control_chassis/host/ftesc_realtime_feedback_harness.c`（FTESC CAN 实时回传解包与 CRC 容错）
  - `control_chassis/host/remote_authorization_harness.c`（遥控双边沿授权与回中联锁测试）
  - `control_chassis/host/remote_backend_harness.c`（A28 与 FlySky 串口流解析、粘包、断包与超时恢复）
  - `control_chassis/host/safety_ownership_gate_harness.c`（安全仲裁门全故障拦截与输出清零）
  - `control_chassis/host/sens_ibus_responder_harness.c`（SENS 1..8 发现与报文校验）
  - `control_chassis/host/telemetry_snapshot_harness.c`（快照聚合、溢出保护与原子读写）
- **仿真桩 (Fakes)**：`control_chassis/host/fakes/` (`BladeMotor.h`, `flipsky_can.h`, `tim.h`)

---

## 10. Retired / Reserved Resources (已清退与预留资源说明)

防止对已变更用途的外设进行误调用：

1. **USART1**：
   - 历史用途：旧 ODrive 本地 ASCII 串口。
   - 当前用途：专用于 FlySky SERVO i-BUS 航模接收机 DMA 接收。严禁在此端口恢复旧 ODrive 文本指令。
2. **USART2**：
   - 历史用途：调试打印输出 / ODrive。
   - 当前用途：分配给 FlySky SENS 遥测回显总线 (`sens_ibus_usart2.c`)。调试输出严禁随意侵占。
3. **USART3**：
   - 当前用途：仅在 `REMOTE_BACKEND_A28_E34=1` 时作为 433MHz 透传无线串口使用。
4. **ODrive 本地串口指令集合**：已彻底清退；ODrive 通信统一收敛到 CAN 2.0B 接口 (`odrive_can.c`)。
