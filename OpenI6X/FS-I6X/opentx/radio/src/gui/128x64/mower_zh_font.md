# Mower UI Subset 11

固定字符子集，用于 FS-i6X 的中文主界面、维护入口、调试信息和割刀确认弹窗。
字体数据存放在 Flash；正常构建无需下载或生成字库。

- 原字体：Noto Sans Mono CJK SC Regular，来源为 [Noto CJK 官方仓库](https://github.com/notofonts/noto-cjk/tree/main/Sans/Mono)。
- 当前源文件：`/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc`，face index 7（Noto Sans Mono CJK SC Regular）。
- 当前源文件 SHA256：`b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a`。
- 旧版 12px 字库来源为 `NotoSansMonoCJKsc-Regular.otf`，SHA256 为 `ec04cc376b34887cedbdf84074e2e226ed2761eeabdcb9173fc1dd7bfd153ef7`。当前 TTC 的 SC Mono 字面在旧参数下与旧版全部 214 个字符逐像素相同。
- 许可证：SIL Open Font License 1.1，完整文本见同目录 `mower_zh_font.LICENSE`。
- 派生名称：Mower UI Subset 11；中文宽 11、ASCII 宽 6，字形字号 11px、字模行高 12px，1-bit 行优先位图。
- 生成参数：Pillow FreeType 11px、中文基线 y=10、ASCII 基线 y=9、灰度阈值 80。中文保留顶端笔画，ASCII 为 `g/j/p/q/y/|` 等保留下部空间。
- 当前含164个字符（34个界面所需ASCII、130个中文），位图共2386字节；单字ASCII 9字节、中文16字节。中文末行原为空白，仅省略存储，绘制行高仍为12px；原有可见字形和排版不变。
- “急停”所需两字已在子集中；快速急停首页复用现有字形，重新生成与完整字形校验通过，无需扩大字库。
- ASCII取自中文界面的生产字面量，另保留十进制/十六进制数字及数字标点；原生英文实验页使用系统字库。子集缩减与中文空白行省略共节省801字节Flash，33张实际界面回放逐像素一致。
- 生成时在更大的画布上再次栅格化，检查阈值后的完整边界与像素数，任何裁切或缺字都会失败；绘制器与宽度计算共用生成的字模尺寸常量。

界面增加中文字符后，使用带 Pillow 的 Python 运行：

```bash
python3 tools/generate_mower_zh_font.py \
  /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --font-index 7
# 验证生成结果和完整字形，不改写文件：
python3 tools/generate_mower_zh_font.py \
  /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --font-index 7 --check
```

生成器只更新 `mower_zh_font.h`；正常固件编译不依赖系统字体或 Pillow。使用单独 OTF 时可省略 `--font-index`，须记录对应源文件哈希。发布包记录源文件及 face；完整上游字体无需随固件分发。
