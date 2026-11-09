# 软件验证

- PYTHONPATH=/tmp/mower-60pct-test-deps ASAN_OPTIONS=detect_leaks=0 ./tools/build_blade_ok.sh退出0：全套host、底盘Debug/生产、FS-i6X生产及实际Thumb机器码回归通过。
- O0/O2回归执行实际SystemClock_Config、MX_TIM2_Init、MX_TIM3_Init及EG2104驱动，确认84MHz、10kHz、两路正反转5040/8400、CubeMX一致及启动/停止0。
- 推杆既有归位10秒、草袋20秒、换向先停止及撤权停车场景保留；300ms默认及100/300ms通信矩阵和恢复授权回归通过。
- 底盘BIN与原已验证10kHz/60%镜像逐字节一致，SHA256为4469331b6dcff4514dfb095670c205833775336c2d30a5a38ddc62b00ff82fe5。
- 新底盘ELF/BIN实际ARM DWARF离线读取验证通过，目标内存/RPC模拟，未连接硬件。
- 新增构建警告0；svn_preflight通过，保留3类工作区提示，包含本轮tim.c及ioc恢复配置。
- 系统时钟、H桥驱动、300ms和割刀配置未改，遥控器921份源文件未改；遥控器BIN与上一版仅构建时间字符串不同。
- 历史10kHz/60%和15kHz/40%发布包保持不变。本轮不实际刷写，声音改善、实机波形和机械行程效果待现场短程复测。
