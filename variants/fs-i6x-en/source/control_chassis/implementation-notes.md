# Implementation Notes

## Deviations

- 原计划依赖 Git 差异确认改动，但项目根目录不是 Git 工作树；本次改为以仓库文件、源码和现有 PDF/文本资料为事实来源。
- 计划中的“直接从 OID 厂商协议生成 RK3588 手册”暂不执行；当前证据表明 RK3588 更可能通过 STM32 的上位机主控帧接入，需先确认是否允许绕过 STM32 直接访问 OID 节点。
- PDF 排版中间目录中的 Chrome 临时 profile 有后台进程占用，已清理 HTML/PNG 中间文件和可删除内容，残留 profile 目录暂不强制结束用户进程。

## Discovered edge cases

- 当前仓库同时包含 STM32 CAN 驱动源码、多个 CAN 相关模块和已有 CAN 手册 PDF，不能直接假设“代码即协议”或“旧手册即现行协议”。
- 用户只说明目标设备为 RK3588，尚未明确 RK3588 使用 SocketCAN、USB-CAN、专用 PCIe/CAN 适配器或其他链路；手册的接入示例会受此影响。
- 用户已确认当前下位机实际采用 OID 驱动；正式手册应以 OID 配置下实际启用的上位机接口为主，其他驱动代码暂不作为协议依据。
- 用户已确认 CAN 物理速率采用 500 kbit/s；手册以当前 `can.c` 的实际 HAL 配置为准，不采用 `.ioc` 中过期的 875 kbit/s 记录。
- `Core/Src/can.c` 的实际时序参数为 Prescaler=6、BS1=9TQ、BS2=4TQ；结合 APB1=42 MHz 计算为 500 kbit/s，但 `concrol.ioc` 仍记录 875 kbit/s，必须在手册中声明最终以固件实际参数或用户确认值为准。
- 当前主控接收帧为标准数据帧 ID `0x005`、DLC 至少 8；API 超时实际为 500 ms，头文件注释中的 300 ms 已过期。
- 当前运行任务每 40 ms 发送 ID `0x507`（DLC=8，大端 int16 缩放速度/角速度 + 模式/电量/温度/草袋状态），每 100 ms 发送 ID `0x508` 与 `0x509`；OID 配置下这两帧的位置浮点值目前保持为 0。
- 源码中 `0x506` 状态帧函数和另一套 `0x507` 浮点打包函数没有调用点，不能作为当前有效协议直接发布。
- 已发现并修复 `Byte6` 草袋控制缺口：原 `chassis_control_parse_host_frame()` 只更新状态变量，没有调用草袋执行接口；现改为调用幂等的目标状态接口。
- 已按用户确认补齐草袋目标状态接口：`Lifter_SetGrassBagTarget(1)` 请求打开，`Lifter_SetGrassBagTarget(0)` 请求关闭；重复 50 ms 指令在打开/关闭过程中不会反复翻转。
- PDF 联调示例假定 RK3588 的 SocketCAN 接口名为 `can0`；实际接口名由 RK3588 的 CAN 适配器和驱动决定。
- PE5 当前被配置为 GPIOE 推挽输出并默认置高；割刀 PWM 仍使用 TIM2_CH2，PE5 仅承担低有效总使能。
- 割刀非法 PWM 参数、零速、启动和停止路径都会把 PE5 置高，避免异常参数或停刀流程留下低有效开启信号。

## Questions for review

- 无新的阻塞性问题；现场仍需用实际 RK3588 CAN 适配器验证总线终端、接口名和草袋机构动作时间。

## Confirmed decisions

- RK3588 只与 STM32 主控接口通信；OID 报文属于 STM32 内部实现，不纳入 RK3588 必需接口。
- CAN 速率为 500 kbit/s；控制帧 ID 为 `0x005`，RK3588 固定每 50 ms 发送一次。
- API 启动采用两步流程：先发送 `Byte0=1` 进入 API 待机，再发送 `Byte0=2` 进入 API 运行。
- 本次正式控制范围包含运动、割刀、升降和草袋；暂不交付真实轮速/里程计，`0x508`、`0x509` 不纳入当前接口。
- 反馈只整理现有 `0x507`，不新增命令确认、锁定原因、故障码或升降状态字段。
- 交付物为 Markdown 源文档和 PDF 分发版。
- 已建立 `can_bag_control_contract.ps1` 作为本次缺陷的结构契约测试：旧代码红灯，修复后绿灯。
- 已建立 `pe5_blade_switch_contract.ps1`：验证 PE5 高默认、推挽输出、`.ioc` 配置、正 PWM 拉低以及启动/停止置高。

## Previous Handoff

- 当前进度：已完成 OID 配置下 RK3588 主控协议手册、草袋 `Byte6` 目标状态执行修复、Markdown/PDF 交付，并新增 PE5 低有效割刀总使能。
- 当前决策：RK3588 协议决策保持不变；PE5 常态高电平，割刀有效时低电平；PWM 继续负责割刀速度。
- 交付文件：`RK3588_CAN协议手册.md`；`output/pdf/rk3588_can_protocol_manual.pdf`；PE5 契约测试 `pe5_blade_switch_contract.ps1`。
- 验证结果：PE5 契约测试红灯后绿灯；`gpio.c` 与 `BladeMotor.c` ARM GCC 语法检查通过；此前协议手册和草袋修复验证保持通过。
- 下一步行动：实机确认上电默认 PE5=高、割刀开启 PE5=低、割刀关闭/故障停刀 PE5=高，并确认 PE5 外部电路的有效电平与 MCU 引脚定义一致。

### Completion Summary

- 偏差数量：3；包括 Git 工作树不可用、协议范围从 OID 直连收敛为 STM32 主控接口、Chrome 临时 profile 清理受进程占用影响。
- 最可能复审：`.ioc` 的 875 kbit/s 旧记录、BMS 故障占位 `0x01` 与 1% 电量的歧义。
- 发现并处理的边界：OID 里程计为 0、无应用层确认/故障码、500 ms 超时锁定、`0xAA` 解锁风险、Byte6 重复帧幂等。
- 本次验证：结构契约测试、两份 ARM GCC 语法检查、Markdown 关键事实扫描、PDF 完整性和页面预览检查均通过。
- 下一次优先阅读：本文件的 Active Handoff、`RK3588_CAN协议手册.md`，再进行真实总线和草袋机构联调。
- 当前追加任务：使用 PE5 作为割刀低有效开关；现状确认 PE5 未配置，现有割刀链路使用 TIM2_CH2 PWM 调速。
- 当前实施假设：保留 PWM 作为转速控制，PE5 作为总使能；速度为 0 或调用停止接口时 PE5 必须为高电平。
- 当前验证：PE5 高默认/低开启契约测试通过，`gpio.c` 和 `BladeMotor.c` ARM GCC 语法检查通过。
- 当前下一步：实机测量 PE5 上电、开刀、关刀和故障停刀电平，并检查外部驱动电路的低有效定义。

## Active Handoff

- 当前进度：PE5 已配置为 GPIOE 推挽输出并默认高电平；正 PWM 时拉低，零速、非法参数、启动、停止和故障停刀路径保持高电平。
- 当前决策：保留 TIM2_CH2 负责割刀转速，PE5 仅作为低有效总使能；RK3588 CAN 协议范围与已有手册保持不变。
- 本次验证：`pe5_blade_switch_contract.ps1` 与 `can_bag_control_contract.ps1` 均通过；`gpio.c` 和 `BladeMotor.c` 均通过 STM32F407 ARM GCC 语法检查。
- 已补充防回归检查：`BladeMotor_Start()` 不得直接把 PE5 拉低，避免启动时产生低有效使能脉冲。
- 下一步行动：实机确认上电 PE5=高、割刀开启 PE5=低、关闭/故障停刀 PE5=高，并确认外部驱动电路确实为低有效及满足 PWM/使能时序。
