# FS-i6X 双端固件：刀盘升降 15 kHz／60%

基于 `/Users/dyh/phld/MowerControl_FS-i6X` 的 SVN r109 重新构建底盘和 FS-i6X 遥控器。刀盘升降 TIM3 改为 15 kHz，沿用新项目现有 60% 占空比；草袋 TIM2 保持 10 kHz／60%。两端正式协议均为 **0x36**。

## 镜像与配置

| 设备 | 文件前缀 | BIN 起始地址 | BIN 字节数 |
|---|---|---|---:|
| STM32F407 底盘 | `control-chassis-lifter-15khz60pct` | `0x08000000` | 90396 |
| FS-i6X 遥控器 | `fs-i6x-lifter15khz-paired` | `0x08000000` | 130864 |

两端均提供 `.bin`、`.hex`、`.elf`、`.map`。HEX／ELF 自带地址，BIN 需要手动指定地址；ELF／MAP 与同名前缀的 BIN 配套。

- 升降：TIM3_CH3/PB0、TIM3_CH4/PB1，84 MHz、PSC=0、ARR=5599；15 kHz、60%，活动 CCR=3360。
- 草袋：TIM2_CH3/PA2、TIM2_CH4/PA3，PSC=0、ARR=8399；10 kHz、60%，活动 CCR=5040。
- 沿用 r109 的升降方向、换向先停止、机械时间表、300 ms i-BUS 超时和授权逻辑。
- 割刀继续使用 FTESC CAN；保留的 TIM2 PWM 配置为 10 kHz。实体急停配置保持 `VEHICLE_ESTOP_ENABLED=1`。
- 遥控器功能源码未修改，提供的是当前版本的重新构建镜像。已经使用匹配 0x36 遥控器时，可只更新底盘；本包支持完整双端刷写。

[双端刷写与复测说明](刷写与复测说明.md)包含接线、目标识别、下载、独立读回、回退与硬件验收。`artifacts.json` 记录大小、SHA256 和配置。

## 软件验证与边界

全套软件验证已通过；实机刷写和硬件测量尚未执行。

最终验证记录见 `validation/`：全套 host 回归、底盘 Debug／发布版、FS-i6X 正式构建、真实 ARM Thumb 遥测路径和双端镜像一致性检查。PWM 回归执行实际时钟、定时器初始化和 EG2104 驱动，在 O0/O2 下验证两种频率、正反转 60% 和启动／停止清零。

macOS 主机验证使用包内 `validation/macos-host-cc.py` 和 `validation/macos-bin/c++`，仅适配队列替身未使用函数、快照测试的 GNU `--wrap` 链接语义和按键替身的常量告警；原测试断言保留。`tools/verify.sh` 的两处空数组展开兼容系统 Bash 3.2，统一入口显式传播 host 失败；禁用功能检查使用 `nm -g --defined-only` 排除本地汇编标签。适配与首次失败记录均保留在 `validation/macos-host-compatibility.log` 及相邻日志中，ARM 生产编译参数不受影响。

底盘新旧同机完整构建警告对比见 `validation/warning-comparison.log`；遥控器完整构建告警保存在日志中。修改前 r109 由本机工具链重新生成的底盘镜像保存在 `rollback/`，它与历史发布包的构建产物不保证逐字节一致，来源见 `validation/baseline-image.json`。

本次没有实机刷写、波形测量或机械标定，也未执行 SVN 提交。`rollback/` 中的文件是候选回退镜像，不是从当前设备读取的备份。

## 重现构建

在新项目根目录、已安装相同版本临时依赖的环境中执行（工具链路径按本机实际安装位置设置）：

```sh
export ARM_GNU_TOOLCHAIN_ROOT=/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi
export PYTHONPATH=/private/tmp/mower-r109-dual-deps
export CC="$PWD/releases/20261106-lifter-15khz/validation/macos-host-cc.py"
export PATH="$PWD/releases/20261106-lifter-15khz/validation/macos-bin:$PATH"
ASAN_OPTIONS=detect_leaks=0 ./tools/build_blade_ok.sh
```

本轮临时依赖为 Unicorn 2.1.4、Pillow 11.3.0，可用 `python3 -m pip install --target /private/tmp/mower-r109-dual-deps unicorn==2.1.4 Pillow==11.3.0` 准备；工具版本与配置缓存见 `validation/`。上述命令只构建和验证，不刷写。Linux 可使用原生 GCC／GNU 链接器，省略 macOS 专用 `CC` 和 PATH 设置。
