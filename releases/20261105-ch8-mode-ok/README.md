# CH8 模式切换 / 独立底盘 OK 软件验证包

软件验证通过；未刷机、未提交 SVN，实机验收未执行。两端必须配套更新，确认协议为 **0x33**。

**本包沿用仓库原台架安全配置：`VEHICLE_ESTOP_ENABLED=0`，PD1 实体急停仍处于原有软件旁路状态。不能把软件测试结果当作整车安全验收结果；整车运行前需按实际硬件配置启用并实测安全输入。**

## 固件与配置

- [底盘 BIN](control-chassis-ch8-ok.bin) / [底盘 HEX](control-chassis-ch8-ok.hex)：STM32F407，FlySky i-BUS、OID 行走驱动、PWM 割刀，`mower-ok-release` 生产配置。
- [FS-i6X BIN](openi6x-ch8-ok.bin)：真实中文主界面、Mower OK 开启，mock、测试模型及独立台架选项关闭。
- [SHA256 校验](SHA256SUMS) / [文件大小与元数据](artifacts.json)。

CH8 的 SD 直通混控沿用原配置：1000 遥控，2000 API，1500 遥控。开机锁定。CH5 打开、行走摇杆持续回中、CH4 回中且安全条件允许时，出现“底盘解锁 / 按 OK 确认”。底盘 OK 不启动割刀；割刀仍需新的 CH4 启动操作和独立 OK。

正常切换保留已解锁状态，先停止所有旧控制源动作并清除缓存。选择 API 后必须重新发送 CAN `1→2`；遥控选择下 CAN 不能接管。摇杆中断 API、CH5 停机、失联及故障需重新 OK，CAN `0xAA` 不能替代 OK。API 主机仍须每 50ms 连续发送完整帧。

## 软件验证

- `tools/verify.sh all`：全部 host harness 与默认 ARM debug 构建通过，覆盖旧配置回归。
- 生产 CH8/OK：模式消抖、开机锁定、独立解锁、切换保留授权、新鲜 CAN 两步启动、0xAA 隔离、API 超时/摇杆中断及所有锁存路径通过。
- 实际 Motion 仲裁和停止调用点、实际升降 FSM、草袋互斥/权限、CHAS/BLAD Confirm/Cancel/Result、迟到与重复消息、会话轮换、SENS AA/AC 及时间/心跳回绕测试通过。
- 实际中文渲染、菜单 OK、长按释放抑制、确认互斥、全局动作序号、代次退休和像素边界测试通过。
- 底盘与遥控器生产 ARM 构建通过；修改的底盘生产翻译单元与 SVN BASE 对比无新增编译警告。保留仓库既有编译/链接告警，详见日志。
- 新生产源码已登记 CMake、EIDE、Keil；Keil XML 可解析。SVN preflight 通过，两项提示分别为生成字库重排、Keil 工程清单修改。

遥控器 Flash 为 130556/131072 字节，剩余 516 字节；RAM 静态占用 15792/16384 字节，剩余 592 字节。后续扩展需继续检查空间，动态栈余量仍需实机观察。[固件大小](validation/firmware-size.log)、[警告对比](validation/warning-comparison.log)、[完整验证日志](validation/host-and-debug-build.log)。

[界面预览](preview/control-preview.png) 来自真实渲染函数的 host 像素输出，属于测试夹具，不是实机照片。

## 实机验收步骤（待执行）

1. 先隔离动力输出，确认实际引脚、急停与碰撞等输入的启用条件；配置接收机失控保护，使 RF 失联时 CH5 关闭或 SERVO 停止输出。无失联标志的持续保持末值 SERVO 帧，不能单靠帧校验区分 RF 已失联。
2. 更新两端并核对校验值，确认地址 12/E4、13/E5 可发现并持续轮询，地址 1～11 的原用途正常。记录轮询周期和状态心跳；冻结回传超过 1s 后界面应显示未知并禁止 OK。
3. CH8=1000、CH5 关闭时开机，应为遥控选择且锁定；打开 CH5、摇杆和 CH4 回中，确认只出现底盘 OK。反复双拨 CH8 不得解锁。单独按 OK 后再验证新的遥控帧可取得运行许可。
4. 底盘 OK 不得启动割刀。新的 CH4 回中→启动后，另按一次 OK 才能启动；菜单 OK 和长按释放不得复用为任一授权。
5. 在受控条件下启动遥控动作，切 CH8=2000，测量停车、停刀、升降/草袋停止时延，确认底盘保持已解锁。只发送 CAN 2 不得运行；发送新的 1 后为待机，再连续发送新的 2 才能运行。
6. API 运行时切回 CH8=1000/1500，确认旧 API 速度、割刀、升降和草袋目标都不恢复；底盘保留已解锁，新遥控帧才能执行。此时 CAN 1/2/0xAA 不能取得控制权。
7. API 运行时拨动行走摇杆，或断开 CAN 达 500ms，确认停止并锁定；回中后必须新 OK，若继续 API 则重新 1→2。关闭再打开 CH5 不得自动解锁。
8. 测试 SERVO 失联、RF 失控保护、重绑定、两端重启和已启用的物理安全输入；故障仍有效时 OK 应拒绝，恢复后旧 Confirm/Cancel 不得使旧动作复活。记录 OK 往返延迟与故障恢复参数，再进行受控运动验收。

共享字段和恢复规则见 [协议说明](../../docs/protocols/ch8-mode-and-chassis-ok.md)，选择原因见根 ADR 0002。本包没有任何硬件验收通过的声明。

## 重现构建

```sh
./tools/verify.sh all
cmake --preset mower-ok-release -S control_chassis
cmake --build control_chassis/build/mower-ok-release
MOWER_UI_PYTHON=/path/to/python-with-Pillow ./tools/build_mower_user_ui.sh
./tools/svn_preflight.sh
```

构建使用仓库中的位图字库；需要重生成字库时，按 `mower_zh_font.md` 的固定源字体哈希和生成器执行。host 禁用构建检查使用 `nm -g --defined-only`，兼容 macOS 空对象中的本地 `ltmp0` 汇编标签。
