# Mower UI Subset 12

固定字符子集，用于 FS-i6X 的中文主界面、维护入口、调试信息和割刀确认弹窗。
字体数据存放在 Flash；正常构建无需下载或生成字库。

- 原字体：Noto Sans Mono CJK SC Regular，来源为 [Noto CJK 官方仓库](https://github.com/notofonts/noto-cjk/tree/main/Sans/Mono)。
- 原文件：`NotoSansMonoCJKsc-Regular.otf`。
- 原文件 SHA256：`ec04cc376b34887cedbdf84074e2e226ed2761eeabdcb9173fc1dd7bfd153ef7`。
- 许可证：SIL Open Font License 1.1，完整文本见同目录 `mower_zh_font.LICENSE`。
- 派生名称：Mower UI Subset 12；中文宽 12、ASCII 宽 6，高 12 像素，1-bit 行优先位图。
- 生成参数：Pillow FreeType 12px、基线 y=10、灰度阈值 80；含可打印 ASCII 及界面字面量所需中文。

界面增加中文字符后，使用带 Pillow 的 Python 运行：

```bash
python3 tools/generate_mower_zh_font.py /path/to/NotoSansMonoCJKsc-Regular.otf
```

生成器只更新 `mower_zh_font.h`。发布包记录源文件哈希；完整上游字体无需随固件分发。
