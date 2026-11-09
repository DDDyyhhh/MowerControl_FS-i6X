# 策略回归记录

测试先修改为用户选择的期望，再运行未修改的v2生产实现。以下三个命令均确定性失败，随后修改生产逻辑，使用同一组测试转为通过：

| 命令（仓库根目录） | 修复前结果 | 修复后结果 |
| --- | --- | --- |
| `ASAN_OPTIONS=detect_leaks=0 python3 tools/test_unlock_dispatch.py` | 无BMS但合法新OK未被接受，`accepted && !command.run_permitted && !g_motion_blade_event_ready` 断言失败 | PASS；真实iBUS/backend/Motion派发、独立启刀、旧OK拒绝及CH5首帧停车 |
| `ASAN_OPTIONS=detect_leaks=0 python3 tools/test_battery_protection.py` | 无反馈初始化仍停机禁刀，`!m.stop_required && !m.stop_latched && !m.blade_inhibited` 断言失败 | PASS；无反馈/过期/无效/恢复、LOW锁存、真实BMS解码/Motion钩子及三种轮驱动 |
| `ASAN_OPTIONS=detect_leaks=0 python3 tools/test_blade_ok_ui.py` | 单独反馈异常时首页未显示“已连接”，连接状态断言失败 | PASS；反馈异常仅调试、LOW持续禁刀、确认/取消流程和像素边界 |

真实快速状态生产者/接收者回归同步检查：E0 bit6保留、E9不产生通用故障，其他急停/碰撞/倾斜及状态时效仍有效。完整回归和构建见 paired-build.log，逐项日志见 host_*.log。

源码源包来自已校验的v2快照，5220个原文件哈希一致；提交前打包排除原历史scratch文档3项及11项个人工具记忆、IDE本地状态和临时缓存，5206个构建输入逐项记录。固件与生产源码未因归档清理改变。相对v2的13文件差异见 changed-files.json 与 changes.patch。历史v34文件备份位置记录于 rollback.json。

界面比较：36张原快照中35张逐像素一致；battery-invalid仅按本次需求改为“已连接”和未知数值。新增battery-invalid-debug展示调试项；battery-policy-preview.png为行渲染与首页效果合成预览，不是硬件截图。

尚未进行硬件刷写或机械响应验收。
