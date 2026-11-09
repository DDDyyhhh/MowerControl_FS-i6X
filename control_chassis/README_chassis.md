## 概述

这是一个为4轮差速底盘设计的控制程序，支持两种控制模式：

1. **遥控模式**：直接使用遥控器输入（ly, rx），内部处理死区和响应曲线
2. **API模式**：使用(vx, vy, w)格式进行运动控制

两种模式最终都通过统一的运动学计算转换为轮子速度命令。

## 配置参数

### 轮子参数
- 轮子直径：240mm (0.24m)
- 轴距：775mm (前后距离)
- 轮距：650mm (左右距离)
- 电机减速比：4.43

### 遥控模式参数
- 最大前进速度：2.0 m/s
- 最大角速度：2.0 rad/s
- 死区范围：10%
- 最小转向响应：45%

## API接口

### 初始化
```c
#include "chassis_control.h"

chassis_control_init();
```

### 遥控模式
```c
// 使用遥控器输入控制 (0-4095)
// ly: 左摇杆Y轴 (前进后退)
// rx: 右摇杆X轴 (转向)
set_chassis_rc_control(2048, 2048);  // 中位，停止
set_chassis_rc_control(3048, 2048);  // 前进
set_chassis_rc_control(2048, 3048);  // 右转
```

遥控模式特性：
- 支持死区处理（默认10%）
- 转向起步跃变补偿（克服静摩擦）
- 可配置最大速度和响应曲线

### API模式
```c
// vx: 前进速度 (m/s)
// vy: 侧向速度 (m/s) - 差速底盘不支持侧向移动，默认忽略
// omega: 角速度 (rad/s)
// 输出: 各轮子速度命令 (turn/s)
set_chassis_velocity(1.0f, 0.0f, 0.0f);  // 前进1m/s
set_chassis_velocity(0.0f, 0.0f, 1.0f);  // 原地旋转
set_chassis_velocity(0.5f, 0.0f, 0.5f);  // 前进并旋转
```

### 停止
```c
chassis_stop();
```

## 运动学原理

对于4轮差速底盘，各轮子速度计算公式（vy=0）：

```
v_fl = vx - ω * R  // 前左轮
v_fr = vx + ω * R  // 前右轮
v_rl = vx - ω * R  // 后左轮
v_rr = vx + ω * R  // 后右轮
```

其中 R = sqrt((L/2)² + (W/2)²)，L为轴距，W为轮距。

RPM = (v * 60) / (π * D)，D为轮子直径。

## 文件结构

- `chassis_control.h` - 头文件，包含函数声明和参数定义
- `chassis_control.c` - 实现文件，包含运动学计算和电机控制
- 控制命令输出单位：**turn/s** (转每秒)

## 验证示例

### 输入参数验证

R = sqrt((0.775/2)² + (0.65/2)²) ≈ 0.506m

#### 输入 (1, 0, 1) - 前进1m/s + 旋转1rad/s
```
v_fl = 1 - 1×0.506 = 0.494 m/s → 39.3 RPM → 0.655 turn/s
v_fr = 1 + 1×0.506 = 1.506 m/s → 119.8 RPM → 1.997 turn/s
v_rl = 1 - 1×0.506 = 0.494 m/s → 39.3 RPM → 0.655 turn/s
v_rr = 1 + 1×0.506 = 1.506 m/s → 119.8 RPM → 1.997 turn/s
```

#### 输入 (0, 0, 1) - 原地旋转1rad/s
```
v_fl = 0 - 1×0.506 = -0.506 m/s → -40.3 RPM → -0.672 turn/s
v_fr = 0 + 1×0.506 = 0.506 m/s → 40.3 RPM → 0.672 turn/s
v_rl = 0 - 1×0.506 = -0.506 m/s → -40.3 RPM → -0.672 turn/s
v_rr = 0 + 1×0.506 = 0.506 m/s → 40.3 RPM → 0.672 turn/s
```

RPM计算公式：RPM = (线速度 × 60) / (π × 轮子直径)  
turn/s计算公式：turn/s = RPM / 60

## 示例应用

### 遥控模式使用
```c
// 初始化
chassis_control_init();

// 在遥控器中断或任务中调用
void remote_control_task(uint16_t ly, uint16_t rx)
{
    set_chassis_rc_control(ly, rx);
}
```

### API模式使用
```c
// 初始化
chassis_control_init();

// 前进
set_chassis_velocity(1.0f, 0.0f, 0.0f);

// 左转
set_chassis_velocity(0.0f, 0.0f, -1.0f);

// 停止
chassis_stop();
```