# FS-i6X 遥控器上下文

当前割草机生产遥控器为本目录中的 OpenTX 固件；`remote_lvgl` 是独立旧遥控器上下文。

- 生产界面和确认事件路由：`opentx/radio/src/gui/128x64/view_main.cpp` 与 `mower_user_ui.h`。
- CH8（SD 直通）只选择遥控/API；CH5 运行使能；CH4/OK 启动割刀；CH7 刀速；CH6 草袋；CH9 高度；CH10 行走速度。
- CH11/CH12 专用于串行确认协议，不能作为普通解锁开关。生产配置启用 `PCBI6X_MOWER_BLADE_OK`，关闭 mock、测试模型及独立台架配置。
- 底盘 OK 与割刀 OK 共用动作序号和队列，分别路由。共享字段、时序、代次与回放规则以 [CH8 / 底盘 OK 协议](../../docs/protocols/ch8-mode-and-chassis-ok.md) 为准。
- 两端协议适配器位于根 `experiments/ticket03/`，这些文件同时包含生产编译分支，修改时须保留独立台架分支。
- 界面字形源、许可证与生成方式见 `opentx/radio/src/gui/128x64/mower_zh_font.md`；界面验证执行根 `tools/test_blade_ok_ui.py`。
