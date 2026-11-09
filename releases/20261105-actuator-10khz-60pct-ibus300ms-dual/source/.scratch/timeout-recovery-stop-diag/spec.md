# 超时恢复修复与首次撤权诊断

Status: software-verified; direct-trigger-confirmed; electrical-root-cause-pending
Triage: ready-for-human
Context: control_chassis, OpenI6X/FS-I6X

## 证据与范围

用户读数：解锁时0x00C8，推杆动作锁定后0x04C8；仅新增系统锁定。断开推杆电机输出后同样调整高度不再锁定，带载触发原因仍待实测。

已用真实GUI、双端协议、SENS、iBUS、授权和割刀状态机复现：120ms SERVO断流后重新合法解锁，历史超时仍进入割刀停车条件，导致新割刀确认框不出现。新增scenario14在修复前报`prompt timeout`，修复后通过两轮超时恢复及再次启刀。

## 实施

- 历史timeout_latched及计数保留；共享只读判定负责当前链路超时或尚未恢复的超时授权锁存，割刀和E0 bit3使用该判定。
- 100ms保护、显式重新授权、割刀独立OK和所有现有安全输入保持。
- g_app内保存首次撤权记录，每次允许运行后重新布防；一次停机期间冻结。记录当前原因、输入、接收错误/队列计数、时钟、通道、高度及PWM；启动时保存RCC复位标志。
- ST-Link故障后读取：配套生产优化ELF带完整DWARF，核对实际Flash与BIN，再从类型定位记录，拒绝不完整或不一致读取；finally恢复CPU。不复位、不下载、不解锁。
- 遥控器固件原样复用，不新增无线字段或界面。

## 验收

- [x] 新闭环场景与原闭环回归通过，故障位正确清除，历史记录保留。
- [x] 首次冻结、重复不覆盖、重新布防、复位记录和接收诊断回归通过。
- [x] 读取脚本成功/失败/超时均尝试恢复CPU，并明确报告恢复失败；通道及目标内存使用模拟，实际生产ELF/DWARF和GDB代码验证通过，实机读取待执行。
- [x] 全量host、底盘构建、生产构建及SVN preflight通过，无新增生产警告。
- [x] 发布至releases/20261030-timeout-recovery-stop-diag，含固件、ELF、读取脚本、说明及带独立校验清单的诊断ZIP。
- [x] 推杆接入/断开现场记录完成：CH5不动的带载样本确认100ms真实失联，UART新增71次错误、最后NE；锁定后接收恢复。无载对照为CH5主动停车，UART新增63次错误。详见现场对照记录。
- [ ] 接收机供电、信号地、SERVO信号及可能的输出/恢复因素进一步区分，完成根因修复与整机验证。此项未完成前不宣称电气故障已修复。

## 验证与交付记录

`ASAN_OPTIONS=detect_leaks=0 ./tools/verify.sh all`通过；仅关闭桌面ptrace环境不支持的LeakSanitizer。生产配置保持-Os，新增-g3；BIN 90412字节，ELF类型记录232字节。隔离重建任务开始时源码与当前源码均有42种既有生产警告，新增0种。SVN预检通过，3类提示详见日志。

遥控器原样复用20261029草袋集满版，BIN SHA256为846ea5bcedce821335a3de241818b71ed64378f9d5efb5b25abd0c5153d54768；本轮未修改或重建遥控器。原始红/绿回归和读取模拟测试记录在validation目录。未执行刷写或实机捕获，发布状态为“软件修复通过，推杆带载故障待现场诊断”。

2026-11-01现场跟进：已连接ST-Link尝试读取；修正HLA全局poll日志不能代表目标状态的问题，改用目标arp_poll/curstate。CPU恢复已实机确认，但多种Flash读回存在不一致、整字错位或短块失败，尚未接受有效的首次撤权记录。固件未重新刷写、复位或解锁；当前先核对调试供电与接线。原始记录见[field/20261101/README.md](field/20261101/README.md)。

同日更换连接后，已成功核对实际Flash与配套BIN，按DWARF读取两次一致SRAM，并恢复CPU。当前记录`valid=0/event_count=0/authorized_ms=0`，运行时间54805ms，启动标志0x0E000000（BOR/PIN/POR），没有保存到本次启动的一次解锁后撤权事件。等待保持供电的新一轮推杆锁定复现，不据启动标志认定掉压。详见[field/20261101/after-rewire-01/README.md](field/20261101/after-rewire-01/README.md)。

后续已完成条件明确的带载复现和无载对照，并补读故障后的当前接收状态。直接触发链已确认，UART/DMA在锁定后已恢复接收；无载也存在错误，不能仅凭计数认定电机EMI是最终根因。当前生产配置还保留USART1_TX HIL输出，作为待单独对照的变量记录，本轮未修改或刷写。最终证据与边界见[field/20261101/conclusion.md](field/20261101/conclusion.md)。
