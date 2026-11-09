# control_chassis 领域导航与架构契约 (CONTEXT.md)

本文件为底盘控制板（`control_chassis`）核心子系统的渐进式导航入口（Progressive Disclosure Index）。为降低单次会话上下文压力，本文件仅保留关键模块边界、调用契约与实存代码路径指针。

---

## 1. Remote Backend (遥控后端抽象)

遥控后端隔离层实现了手持无线遥控（A28/E34 433MHz）与航模接收机（FlySky SERVO i-BUS）在编译期的互斥选择与统一状态发布。

- **配置契约**：`control_chassis/Core/Inc/remote_backend_config.h`
  - 必须严格二选一宏定义：`REMOTE_BACKEND_A28_E34` (0 或 1) 与 `REMOTE_BACKEND_FLYSKY_IBUS` (0 或 1)。
  - 主程序有效i-BUS帧超时默认300ms，Comm、Motion独立看门狗及接收过期判断共用该配置，并使用实际接收时刻。
  - CMake参数`MOWER_IBUS_TIMEOUT_MS`只接受100或300；`mower-ok-release`默认300，`mower-ok-ibus-100ms`提供显式回退。非CMake构建由头文件默认值300U生效，仍可编译期覆盖。
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
  - **解析状态**：由 Comm 独占的接收流、稳定性及累计事件状态。
  - **发布快照**：供 Motion 与诊断读取的最后一份完整解析结果，通过短临界区传递，读取不等待发布者。
  - **当前链路超时**：Motion 根据快照年龄独立判断的运行许可失效条件，即使 Comm 停顿仍生效；与历史超时锁存、Comm 累计超时事件计数分别表达。

---

## 3. Remote Authorization (遥控授权状态机)

遥控运行权限授权状态机，防止遥控器开机误动或意外恢复。

- **模块接口**：`control_chassis/Core/Inc/remote_authorization.h`
- **模块实现**：`control_chassis/Core/UserDev/remote_authorization.c`
- **核心行为**：
  - 正式 FS-i6X（`MOWER_BLADE_OK_ENABLED=1`）：锁定后 CH4 回中 1400..1600，再左拨至 900..1100（约1000），保持并按 OK；右拨约2000和CH8不再解锁。解锁后启动割刀需重新回中、左拨并再次按OK。
  - OK 必须属于当前会话与底盘为实际请求分配的单次令牌，CH5许可、实际安全输入通过且 CH1/CH2 连续回中200ms；拒绝立即消耗请求。清锁后仍需新控制帧才允许输出。
  - 正式协议0x35，E9/profile3以原子32位快照回传实际授权阶段和底盘令牌，遥控器不累计物理代次；BLAD与UNLK共用单调动作序号，确认授权独立。解锁事件在Motion 100Hz锁定分支外分发；旧Cancel只排空旧事务。
  - 开机过早操作和失败重试见[授权契约](../docs/protocols/startup-authorization-v35.md)：区分行走/CH4回中、等待同步和安全阻断；10秒令牌/事务期限，超时自动新会话，不重放旧OK。
  - 非正式/历史测试配置保留CH8双边沿流程，与正式配置分别回归。
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

将整车电池（BMS）、轮速里程计、姿态、割刀状态及 16-bit 故障字聚合成原子快照，供回传与诊断。

- **模块接口**：`control_chassis/Core/Inc/telemetry_snapshot.h`
- **模块实现**：`control_chassis/Core/UserApp/telemetry_snapshot.c`
- **周期上报任务**：`control_chassis/Core/UserApp/app_Report.c`
- **关键机制**：
  - 无锁序列计数器（Sequence Counter）发布模型，单写多读零死锁。
  - 单位统一（分伏、分安、毫米每秒、极对数 ERPM 换算）。

---

**系统锁定**：禁止动力输出、等待合法授权的状态；正常开机也属于锁定，不表示已经发生碰撞或急停。

**急停原因锁存 / 碰撞原因锁存**：记录各自物理触发原因，按钮释放、碰撞杆回弹及其它报告状态覆盖均不清除；安全条件满足后一次合法解锁同时清除。首页依次显示未连接/未知、碰撞、急停、锁定、电池提示/其它故障、已连接。详细协议见[快速状态契约](../docs/protocols/ch4-fast-wheel-feedback.md)。

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
  - 处理地址 1..8 传感器发现（Discovery）、类型探测（Type）与数值测量（Measurement）。
  - 固件未完成硬件总线电气调优前，保持被动安全，不意外拉低总线。

---

## 8. CAN / FTESC (底盘动力与刀盘总线)

底盘电机驱动与割刀驱动的 CAN 通信管理。

- **割刀发送接纳**：发送调度器承担该请求的后续发送责任，不表示电调已执行。
- **停刀待完成**：零输出请求尚未获得对应 CAN 发送成功结果的状态；期间拒绝新的非零割刀请求。
- **CAN 停刀发送成功**：匹配停刀请求获得 TXOK；与刀盘实际停止是两个分别验收的结果。


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

## 11. 推杆占空比与发布

`Core/UserDev/lifter.c`中刀盘升降和草袋开合运行占空比均为50%，正反转及升降归位共用该值，停止0%。草袋使用TIM2 CH3/CH4，10kHz（84MHz输入、PSC=0、ARR=8399，50%比较值4200）；升降使用TIM3 CH3/CH4，10kHz（84MHz输入、PSC=0、ARR=8399，50%比较值4200）。两路推杆启动时由`lifter.c`软启动：400ms内从10%线性升到目标占空比，升速期间高度积分按实际占空比折算，任何停止立即清零。CubeMX `concrol.ioc`保持一致。48V母线下理想平均输出约24V；机械计时、高度时间表及上升补偿沿用原值，实际速度和行程需现场确认。有效i-BUS帧超时仍默认300ms。当前升降15kHz交付见[升降15kHz／60%发布说明](../docs/releases/lifter-15khz-r109.md)；原双端10kHz版本见[10kHz/60%推杆发布说明](../docs/releases/actuator-10khz-60pct-ibus300ms-dual.md)。
