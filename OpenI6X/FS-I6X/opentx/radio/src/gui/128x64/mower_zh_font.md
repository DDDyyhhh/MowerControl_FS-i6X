# Mower UI Subset 11

固定字符子集，用于 FS-i6X 的中文主界面、维护入口、调试信息和割刀确认弹窗。
字体数据存放在 Flash；正常构建无需下载或生成字库。

- 原字体：Noto Sans Mono CJK SC Regular，来源为 [Noto CJK 官方仓库](https://github.com/notofonts/noto-cjk/tree/main/Sans/Mono)。
- 当前源文件：`NotoSansMonoCJKsc-Regular.otf`，face index 0（Noto Sans Mono CJK SC Regular），来自上游 `Sans/Mono`。
- 当前源文件 SHA256：`ec04cc376b34887cedbdf84074e2e226ed2761eeabdcb9173fc1dd7bfd153ef7`。
- 旧TTC源为 `NotoSansCJK-Regular.ttc` face 7，SHA256 `b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a`；本次已有字形位图保持一致。
- 许可证：SIL Open Font License 1.1，完整文本见同目录 `mower_zh_font.LICENSE`。
- 派生名称：Mower UI Subset 11；中文宽 11、ASCII 宽 6，字形字号 11px、字模行高 12px，1-bit 行优先位图。
- 生成参数：Pillow FreeType 11px、中文基线 y=10、ASCII 基线 y=9、灰度阈值 80。中文保留顶端笔画，ASCII 为 `g/j/p/q/y/|` 等保留下部空间。
- 当前含179个字符（34个界面所需ASCII、145个中文），位图共2500字节；ASCII每字72bit、中文每字121bit，跨字形连续存储。中文末行原为空白，仅省略存储，绘制行高仍为12px；原有可见字形和排版不变。
- v39障碍提醒新增“前、方、碍、倒”，首页“避障”标签增加“避”。去掉每个中文字形原有的7个填充bit，已有174个字形逐像素保持一致；完整字形边界和生成一致性检查通过。
- ASCII取自中文界面的生产字面量，另保留十进制/十六进制数字及数字标点；原生英文实验页使用系统字库。本次新增模式字符之前已有的170个字形逐字节保持一致，界面回放由 `tools/test_blade_ok_ui.py` 验证。
- 生成时在更大的画布上再次栅格化，检查阈值后的完整边界与像素数，任何裁切或缺字都会失败；绘制器与宽度计算共用生成的字模尺寸常量。

界面增加中文字符后，使用带 Pillow 的 Python 运行：

```bash
python3 tools/generate_mower_zh_font.py \
  NotoSansMonoCJKsc-Regular.otf
# 验证生成结果和完整字形，不改写文件：
python3 tools/generate_mower_zh_font.py \
  NotoSansMonoCJKsc-Regular.otf --check
```

生成器只更新 `mower_zh_font.h`；正常固件编译不依赖系统字体或 Pillow。使用单独 OTF 时可省略 `--font-index`，须记录对应源文件哈希。发布包记录源文件及 face；完整上游字体无需随固件分发。
