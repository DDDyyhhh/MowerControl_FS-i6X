# 验证记录

硬件未连接验收；预览数据、GPIO 与 RF 输入均为主机注入。

- `verify-all.log`：完整主机回归与底盘 Debug 构建；`host-tests.log` 是最终测试集回归。
- `chassis-release-build.log` / `remote-build.log`：两端生产构建；对应 CMakeCache 随包保留。
- `regression-before.log`：仅在临时主机测试中使用本次修改前绘制器；新断言 `has("草袋未连接")` 按预期失败。源工作区未回退。
- `ui-tests.log`：修改后实际绘制器通过全部用例并输出 PBM；PNG 仅将这些像素放大，未使用生成图片代替真实字库绘制。
- `source-tests.log`：从实际 `RefreshTelemetrySnapshot` 提取函数并执行，链接真实快照聚合，GPIO/周边设备使用替身。
- `baseline-warnings.log`：修改前底盘编译单元的原有诊断，用于核对没有新增警告；不是清理历史警告的记录。
- `implementation.patch`：相对本次操作前文件副本的增量，不包含此前已有的未提交修改；`SOURCE-SHA256SUMS` 记录最终相关源文件。
- `firmware-manifest.json`：固件实际大小与 SHA256；包根 `SHA256SUMS` 覆盖交付文件。
- `svn-preflight.log`：冲突、行尾、生产源登记检查，无 SVN 提交。

macOS 的既有禁用模块测试要求空符号表，Clang/Mach-O 会生成 `ltmp` 标签。验证时只在临时 PATH 中使用本包 `macos-nm-shim.py` 过滤这些标签，生产代码、系统 nm 和原测试没有因此更改。

从项目根目录重现此兼容环境：

```bash
validation_bin=$(mktemp -d /tmp/mower-bag-nm.XXXXXX)
cp releases/20261021-bag-connection/validation/macos-nm-shim.py "$validation_bin/nm"
chmod +x "$validation_bin/nm"
PATH="$validation_bin:$PATH" ./tools/verify.sh all
rm "$validation_bin/nm"
rmdir "$validation_bin"
```

工具链和固件路径应以随包 CMakeCache 与构建日志为准。开启草袋回传的配置为底盘 `mower-ok-release`；遥控器 mock、自动测试模型与台架开关关闭。
