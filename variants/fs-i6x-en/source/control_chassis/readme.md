
# Mower Control Board Firmware (RTOS Version)

## 1. 项目简介
本项目是割草机器人控制板（STM32F407VET6）的固件程序。
系统基于 **FreeRTOS** 实时操作系统开发，实现了多任务并发处理，涵盖了运动控制、姿态解算、多传感器融合（IMU/超声波/BMS）、无线通信及安全保护逻辑。

![stm32_simple_flow_v3](E:\STM32\HAL_STM32Project\phld\SVN_Work\control_rtos\stm32_simple_flow_v3.png)

## 2. 软件架构

系统采用 **生产者-消费者** 模型，通过 FreeRTOS 的 **Queue (队列)** 和 **Semaphore (信号量)** 进行任务间通信，彻底解决了裸机模式下的阻塞和时序冲突问题。

### 2.1 任务分配 (Task Configuration)

| 任务名称 | 优先级 | 堆栈大小 | 运行频率 | 功能描述 |
| :--- | :--- | :--- | :--- | :--- |
| **StartTask_Motion** | **High** | 1024 Words | **10ms** | **核心控制任务**。负责 IMU 姿态解算(100Hz)、碰撞/急停检测、按键逻辑、速度平滑(Ramp)及电机控制指令生成。 |
| **StartTask_Comm** | **AboveNormal** | 512 Words | 事件驱动 | **通信解析任务**。处理来自遥控器的无线数据包，解析摇杆/按键状态，更新心跳时间戳。 |
| **StartTask_BMS_Sensor** | **Normal** | 512 Words | **50ms** | **慢速传感器任务**。负责 RS485 Modbus (BMS) 轮询、超声波数据滤波解析、推杆电机状态机更新。 |
| **StartTask_SerialTx** | **Normal** | 256 Words | 事件驱动 | **发送守护任务**。从 `Queue_TxData` 取出数据并通过 DMA 发送，实现非阻塞 `printf`。 |
| **StartTask_Report** | **Low** | 512 Words | **100ms** | **状态上报任务**。控制 RGB LED 状态灯，向遥控器回传整车状态（电量、故障码等）。 |

### 2.2 关键机制

#### A. 通信接收 (UART RX)
采用 **DMA + IDLE Interrupt** 模式。
*   **机制**：硬件 DMA 自动搬运数据 -> 空闲中断触发 -> 回调函数将数据放入队列 -> 任务唤醒处理。
*   **优势**：CPU 零干预接收，支持不定长数据，永不丢包。
*   **容错**：包含 `HAL_UART_ErrorCallback` 及超时重启机制，防止 ORE/NE 错误导致接收死锁。

#### B. 通信发送 (UART TX)
*   **通用串口 (ODrive/Debug/Remote)**：使用 `Queue_TxData`。调用 `Serial_Printf` 时数据瞬间入队，由后台任务 DMA 发送，**绝不阻塞控制循环**。
*   **RS485 (BMS)**：使用 **DMA + 信号量**。发送时挂起当前任务 (`osSemaphoreAcquire`)，等待发送完成中断。实现了“任务级阻塞，CPU 非阻塞”，保证 Modbus 时序严格准确。

#### C. 运动控制
*   **IMU**：使用 HXY ICM-42688P 专用驱动，100Hz 采样积分，提供 Pitch/Roll 倾角数据。
*   **避障**：超声波数据经过“非对称滤波”（靠近快、远离慢）和“危险优先”逻辑。
*   **平滑**：输出速度经过 Ramp 算法，避免电机突变。

## 3. 硬件模块说明

| 模块 | 接口 | 驱动策略 | 备注 |
| :--- | :--- | :--- | :--- |
| **无线模块 (E34)** | USART3 | DMA RX / Queue TX | 全双工，支持心跳检测 |
| **BMS (AXE)** | UART4 | RS485 (DMA+Sem) | 半双工，需控制 DE 引脚，Modbus RTU 协议 |
| **超声波 (SR04)** | USART6 | DMA RX / Queue | 单线/TTL 协议，带滤波算法 |
| **IMU (ICM-42688)** | SPI1 | 阻塞式 SPI | 华选阳版，寄存器与 TDK 原厂不同 |
| **推杆电机** | PWM (TIM) | 状态机 (FSM) | 带 Flash 断电记忆功能 |
| **轮毂电机** | UART1/2 | ASCII 协议 | ODrive 驱动 |

## 4. 安全特性

1.  **心跳保护**：若 1000ms 未收到遥控器数据，自动触发停车并上锁。
2.  **碰撞/急停锁存**：触发物理碰撞或急停按钮后，系统进入 `Latched` 状态，必须执行“双键+双摇杆”解锁序列方可恢复。
3.  **Flash 写入保护**：在写入 Flash（推杆高度记忆）期间，自动“续命”心跳时间戳，防止因 CPU 阻塞导致的误报断联。
4.  **RS485 抗干扰**：在 ISR 中切换收发引脚，消除 RTOS 调度延迟带来的时序风险。

## 5. 调试指南

*   **日志输出**：连接 **USART2** (115200) 可查看系统启动日志及运行状态。
*   **调试宏**：使用 `Serial_Debug(PORT, fmt, ...)` 可自动打印 `[File:Line]` 信息。
*   **常见故障**：
    *   `[MB] Timeout`：检查 BMS 接线 (A/B) 及波特率。
    *   `[SR04] Len:0`：检查 FreeRTOS 队列 ItemSize 是否设置为 68 字节。