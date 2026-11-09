# 软件验证记录

- `remote-build.log`：遥控器单端发布构建、资源占用及 BIN 哈希。
- `host-tests.log`：全部确定性 host harness，包括生产割刀流程及中文界面回归。
- `ui-tests.log`：最终源码的界面回归；AddressSanitizer 和 UndefinedBehaviorSanitizer 开启。
- `blade-ok-tests.log`：生产协议、授权及取消流程的专项验证。
- `CMakeCache.txt`：交付固件的构建开关及工具路径。
- `SOURCE-SHA256SUMS`：本次界面相关源码和测试指纹，不是全仓库提交清单。

验证环境为 macOS ARM64、Apple Clang、ARM GNU 15.3.1、CMake 4.4。界面测试读取并执行实际 production 主界面路由、live provider、中文字库、绘制函数及确认状态机。绘制替身检查 128×64 边界并输出 PBM；`preview/` 的 PNG 来自这些输出。

旧实验回归的空翻译单元检查要求 `nm --defined-only` 完全无输出。Apple Clang 在空 Mach-O 文件中保留 `ltmp0` 本地标签，导致该原检查退出；本次用临时包装器只过滤编译器 `ltmp数字` 标签，保留其他所有符号及 nm 返回码。未修改生产代码或旧实验测试入口来规避检查。macOS 下从项目根目录复现：

```bash
test_bin=$(mktemp -d /tmp/mower-test-bin.XXXXXX)
cp releases/20261013-remote-simple-ui/validation/macos-nm-shim.py "$test_bin/nm"
chmod +x "$test_bin/nm"
PATH="$test_bin:$PATH" ./tools/verify.sh host
```

界面单独回归不需要该包装器：`python3 tools/test_blade_ok_ui.py`。
设置 `MOWER_UI_PREVIEW_DIR` 为已存在目录可生成实际绘制输出的 PBM。

软件验证不能替代实机刷写、无线链路、LCD 可读性、刷新流畅性及机构动作验收；这些项目按包内复测表填写。
