# 刀盘升降 15 kHz／60%：r109 双端发布

在 `/Users/dyh/phld/MowerControl_FS-i6X` 的 SVN r109 基线上，仅将升降 TIM3 的 ARR 从 8399 改为 5599，同步 CubeMX。84 MHz、PSC=0，对应 15 kHz；原有 60% 占空比对应 CCR=3360。草袋 TIM2 保持 10 kHz、ARR=8399、60%／CCR=5040。

保留 r109 的方向、换向先停止、机械计时、300 ms i-BUS 超时、0x36 协议、FTESC CAN 割刀及启用的实体急停配置。遥控器功能源码未改，配套正式镜像重新构建。现有实际 RCC／TIM／EG2104 的 O0/O2 回归同步覆盖两种频率；验证脚本增加 Bash 3.2 空数组兼容，macOS 主机链接适配单独记录。

[双端交付包](../../releases/20261106-lifter-15khz/README.md)提供两端 BIN／HEX／ELF／MAP、SHA256、双端刷写与独立读回说明、修改前底盘及原配套遥控器候选回退镜像和验证日志。该包基于此目录的新版本构建，硬件刷写、波形和机械验收尚未执行。
