# 刀盘升降推杆 15 kHz 固件

基于当前 CH8 模式切换／独立底盘 OK 工作区生成，仅将刀盘升降推杆 PWM 从 10 kHz 改为 15 kHz。通信协议仍为 **0x33**，沿用配套 FS-i6X 遥控器固件，本包只需更新底盘。

**软件验证通过；尚未刷机，硬件验收未执行。现有配置 `VEHICLE_ESTOP_ENABLED=0` 继续保留，PD1 实体急停仍被软件旁路；现场操作不能依赖该输入切断输出。**

## 固件与配置

| 项目 | 本包配置 |
|---|---|
| 目标 | STM32F407 底盘控制板，工程型号 STM32F407VETx |
| 构建 | `mower-ok-release`，Release，`MOWER_BLADE_OK=ON`，两项独立 HIL 选项关闭 |
| 升降推杆 | TIM3_CH3/PB0、TIM3_CH4/PB1；84 MHz，PSC=0，ARR=5599，15 kHz |
| 升降占空比 | 20%，活动通道 CCR=1120，另一通道为 0 |
| 草袋与割刀 | TIM2 保持 PSC=0、ARR=8399、10 kHz |
| 通信与行为 | 0x33；沿用现有 CH8、独立 OK、升降权限与行程估算 |

- `control-chassis-lifter-15khz.elf`：首选 OpenOCD 刷写文件，自带地址与调试信息。
- `control-chassis-lifter-15khz.hex`：自带地址的 HEX 镜像。
- `control-chassis-lifter-15khz.bin`：原始镜像，写入起始地址 **0x08000000**。
- [刷写与复测说明](刷写与复测说明.md)：接线、烧录、校验、回退及待执行的实测记录。
- `rollback/`：11 月 5 日底盘 BIN/HEX 候选回退镜像，已按原发布元数据核验；不是从当前设备读取的备份。
- `SHA256SUMS`、`artifacts.json`：包内校验值、镜像大小、构建与验证状态。
- `MowerControl-Lifter-15kHz.zip`：完整交付包；压缩包自身校验值在同目录 `ZIP-SHA256SUMS`。

## 已完成的软件验证

- 仅改动 `tim.c` 的 TIM3 周期值与 `concrol.ioc` 的 `TIM3.Period`，原有 CRLF／混合换行保持不变。TIM2 初始化完整保留。
- 修改前重新构建的发布版 BIN 与 11 月 5 日底盘 BIN 完全一致；基线包含已有工作区改动，不等同于 SVN r104 原版。
- `./tools/verify.sh all` 通过：全部现有 host harness 与 ARM Debug 构建，含升降权限撤销、停止、模式切换和草袋回归。
- Debug、`mower-ok-release` 均重新完整构建成功。与修改前同配置完整构建对比，编译／链接警告分别为 56→56、57→57，**新增 0 项**。
- SVN preflight 通过并保留两项提示；详见 `validation/svn-preflight.log`，不是无提示通过。
- `validation/pwm-configuration.log`、`validation/firmware-images.log` 记录频率、占空比、最终 ELF 配置及镜像一致性检查。
- `validation/lifter-15khz.patch` 为本次两处修改；`validation/SOURCE-SHA256SUMS` 记录本次构建输入的校验值。

## 重现构建

在包含当前 CH8／独立底盘 OK 改动和本次 PWM 修改的仓库根目录执行：

```sh
./tools/verify.sh all
cmake --preset mower-ok-release -S control_chassis
cmake --build control_chassis/build/mower-ok-release
./tools/svn_preflight.sh
```

输出位于 `control_chassis/build/mower-ok-release/`。工具版本、完整构建记录和警告对比见 `validation/`。此次未执行实机刷写或 SVN 提交。
