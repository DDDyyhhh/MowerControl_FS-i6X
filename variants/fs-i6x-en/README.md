# FS-i6X 独立英文界面

本目录是2026-11-03冻结的**独立英文版本**，使用 **0x36 配套协议**，包含冻结时的
主程序改动和物理微调按键屏蔽；已保存微调值继续生效。不是旧 0x33 底盘的候选 v6。
2026-11-05按用户要求登记完整独立源码至SVN，详见[SVN源码说明](SVN源码说明.md)。
原发布记录中的“未提交”描述2026-11-03打包时状态；本次不刷写设备。

内附底盘源码仍为冻结时的10kHz、升降20%／草袋60%、100ms遥控超时。
当前主程序r108的10kHz／两路60%／300ms修改没有合入这个历史快照。

## 固件与源码

- `firmware/fs-i6x-en.bin`：供原有 BIN 刷写流程使用的英文固件。
- 同目录 `.elf` 为带符号镜像，`.hex` 为 Intel HEX；三种镜像内容已核对。
- `source/`：实际复制的独立源码，保留原项目相对路径与测试依赖，不依赖指向
  中文版的源码链接。底盘源码仅用于配套回归，`remote_lvgl` 未包含在英文改造中。
- `preview/overview.png`：界面总览；其余 PNG 为真实绘制函数输出的 4 倍整数放大图，
  同名 PBM 保留原始 128×64 帧缓冲。
- `release.json`、`SHA256SUMS`、`validation/`：基线、容量、校验和及验证记录。

首页、维护入口、全部 28 项 DEBUG、解锁与割刀确认、故障及操作提示均为英文。
系统和模型菜单仍使用原生 OpenTX 英文。保留原页面导航、按键、单位与安全授权规则，
无需修改模型、绑定或校准数据，没有运行时语言切换。

首页 `CAR` 为整车电量，`TX` 为遥控器电压，`RF` 为链路质量；`Bag opn/cls` 分别
表示本地草袋开/关**指令**，不代表执行位置回执。`Bag full/OK` 为底盘确认的满载状态；
`Full ?` 表示满载状态未知，`Bag ?` 表示连接状态未知，`No bag` 表示确认未连接。
未知数据继续显示 `--`，不会被当成有效零值或运行许可。

英文版只对显示层作调整：给英文列留出间距、使用 ASCII 字库，底部提示先清除完整底行
再绘制，避免覆盖高度和草袋值。状态机、通信协议、控制输出及授权判断未改动。

## 构建和复验

在本目录执行（Arm GNU GCC 15.3.1，宿主 C/C++ 编译器、CMake、Python 3）：

```sh
python3 verify_release.py
sha256sum -c SHA256SUMS
./build.sh
ASAN_OPTIONS=detect_leaks=0 ./source/tools/verify.sh all > build/verify-all.log 2>&1
```

`build.sh` 只构建遥控器，输出在 `build/en/`，新日志为`build/en/build.log`，新回放
位于`build/preview/`，保留已归档固件、预览和历史日志。可通过`MOWER_RADIO_BUILD_DIR`
及`MOWER_UI_PREVIEW_DIR`选择其它独立输出目录。完整回归中的底盘构建位于独立
`source/control_chassis/build/`。不复用中文版CMake缓存，不执行设备刷写。
`ASAN_OPTIONS=detect_leaks=0` 用于桌面 ptrace 环境；ASan/UBSan 保持开启。
普通构建使用已生成字库，无需 Pillow；生成 PNG 预览和重新生成字库需要 Pillow。

当前验证和容量以 `release.json` 及 `validation/results.md` 为准。
`validation/source-baseline.json` 记录复制前源码；`original-preservation.json` 记录
原文件哈希。`verify_release.py`默认检查独立源码、保护函数、发布文件和容量。
`--original-root`仅用于核对当时冻结的中文版工作区；持续开发后的r108主程序不再是
该历史基线。`prepare_snapshot.py`、`localize_snapshot.py`、`render_previews.py`和
`package_release.py`保留制作过程；后者重新打包还需原冻结中文版及原基线构建目录，
正常检出构建无需运行。

## 实机验收（待执行）

使用与当前主程序匹配的 0x36 底盘，先隔离动力，再按既有刷写流程加载英文固件：

1. 检查首页、维护页、系统/模型菜单和全部 DEBUG 行，确认文字与单位完整。
2. 检查断链、锁定、碰撞、急停、低电量、草袋及割刀档位显示与底盘反馈一致。
3. 检查解锁和割刀启动仍只接受明确 OK，返回和导航键不产生授权。
4. 检查四组八方向微调键仍被屏蔽，原保存值、普通摇杆与菜单按键行为保持。

软件回归和预览不代替以上实机验收。恢复中文版时使用原先配套固件。
