# 独立英文界面源码SVN归档

2026-11-05按用户要求，将现有独立英文版本登记到：

`http://192.168.9.5:8080/svn/MowerControl/branches/MowerControl_FS-i6X/variants/fs-i6x-en`

本目录包含2026-11-03冻结的7030份实际源码文件、构建及复验脚本、原英文固件、预览与校验资料。`source/`保持原字节，源文件身份由`validation/english-source.json`记录；相对当时中文基线仍只有5个显示、字库、文档及UI测试文件改变。依赖库、设备包资源和许可证随独立源码保留。

## 版本差异

- 遥控器为英文界面、0x36协议；控制、授权、页面事件及物理微调按键屏蔽沿用冻结版本。
- 内附底盘源码为10kHz、升降20%、草袋60%、遥控超时100ms，保留原机械计时。这是当时用于回归的源码快照。
- 当前主程序r108为两路10kHz／60%、遥控超时300ms；本次上传不合并这些参数，也不修改主程序或已有固件标签。
- 原英文固件已在r106归档于`tags/20261103-fs-i6x-en-0x36`，r107补充刷写说明。本目录保留的英文BIN/HEX/ELF与该归档相同；本次构建复验不替换原交付镜像。

## 独立检出与构建

```bash
svn checkout http://192.168.9.5:8080/svn/MowerControl/branches/MowerControl_FS-i6X/variants/fs-i6x-en fs-i6x-en
cd fs-i6x-en
python3 verify_release.py
sha256sum -c SHA256SUMS
./build.sh
ASAN_OPTIONS=detect_leaks=0 ./source/tools/verify.sh all > build/verify-all.log 2>&1
```

需要Arm GNU GCC 15.3.1、宿主C/C++编译器、CMake及Python 3。已生成字库随源码提供；常规固件构建无需生成PNG或重新生成字库。新构建的时间字符串可能不同于原交付BIN，归档固件的SHA256仍以原清单为准。

构建输出写入`build/`及`source/**/build/`，均不纳入本次提交；`.pyc`、`__pycache__`、个人配置、版本库数据库和`.scratch`也不提交。历史制作记录描述原始打包状态，实际源码上传修订以SVN日志为准。

本次仅补充源码归档和可独立复验的说明/外层脚本。未刷写设备，实机英文显示及按键行为仍按原README列为待验收。
