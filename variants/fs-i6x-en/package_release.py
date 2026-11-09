#!/usr/bin/env python3
"""Package the validated English image and record its source identity."""
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from verify_release import VARIANT, verify_sources, verify_original, sha

validation = VARIANT / "validation"
firmware = VARIANT / "firmware"
firmware.mkdir(exist_ok=True)
toolchain = Path(os.environ.get("ARM_GNU_TOOLCHAIN_ROOT",
    str(Path.home() / ".local/opt/arm-gnu-toolchain-15.3.rel1-x86_64-arm-none-eabi"))) / "bin"
objcopy, size = toolchain / "arm-none-eabi-objcopy", toolchain / "arm-none-eabi-size"
gcc = toolchain / "arm-none-eabi-gcc"
assert subprocess.check_output([str(gcc), "-dumpfullversion"], text=True).strip() == "15.3.1"
changed, final_sources = verify_sources()
original = VARIANT.parents[1]
original_count = verify_original(original) if (original / "OpenI6X/FS-I6X/opentx").is_dir() else None

cache = (VARIANT / "build/en/CMakeCache.txt").read_text()
for option, value in {"PCB": "I6X", "TRANSLATIONS": "EN", "PCBI6X_MOWER_BLADE_OK": "ON",
                      "PCBI6X_MOWER_UI_SHELL": "YES", "PCBI6X_MOWER_UI_MOCK": "NO",
                      "PCBI6X_EXP3": "OFF", "PCBI6X_EXP3_PWM_HIL": "OFF",
                      "PCBI6X_TICKET03_HIL_MODEL": "NO"}.items():
    assert re.search(r"^" + option + r":[^=]+=" + value + r"$", cache, re.M), option

build_log = (validation / "en-radio-final.log").read_text()
baseline_log = (validation / "baseline-radio-build.log").read_text()
warnings = sorted(set(re.findall(r"warning:.*", build_log)))
old_warnings = sorted(set(re.findall(r"warning:.*", baseline_log)))
assert not set(warnings) - set(old_warnings), "New firmware warnings"
all_log = (validation / "verify-all.log").read_text()
assert "ALL HOST HARNESSES PASSED!" in all_log and "Chassis CMake build succeeded" in all_log
assert "PASS English production UI" in build_log
assert "PASS" in (validation / "font-check.log").read_text()
preflight = (validation / "svn-preflight.log").read_text()
assert "PREFLIGHT CHECK PASSED" in preflight

for suffix in ("bin", "elf"):
    shutil.copy2(VARIANT / ("build/en/firmware." + suffix), firmware / ("fs-i6x-en." + suffix))
elf = firmware / "fs-i6x-en.elf"
hex_path = firmware / "fs-i6x-en.hex"
subprocess.run([str(objcopy), "-O", "ihex", str(elf), str(hex_path)], check=True)
with tempfile.TemporaryDirectory(prefix="verify-images-") as work:
    for name, source in (("elf", elf), ("hex", hex_path)):
        converted = Path(work) / (name + ".bin")
        input_format = ["-I", "ihex"] if name == "hex" else []
        subprocess.run([str(objcopy), *input_format, "-O", "binary", str(source), str(converted)], check=True)
        assert converted.read_bytes() == (firmware / "fs-i6x-en.bin").read_bytes(), name


def capacity(image):
    line = subprocess.check_output([str(size), str(image)], text=True).splitlines()[1].split()
    text, data, bss = map(int, line[:3])
    return {"flash_used": text + data, "static_ram_used": data + bss}


usage = capacity(elf)
baseline_usage = capacity(VARIANT / "build/baseline/firmware.elf")
assert usage["flash_used"] <= 131072 and usage["static_ram_used"] <= 16384
assert usage["flash_used"] == (firmware / "fs-i6x-en.bin").stat().st_size
(validation / "english-source.json").write_text(json.dumps(final_sources, indent=2) + "\n")
if original_count:
    patch = []
    for relative in changed:
        patch.extend(difflib.unified_diff((original / relative).read_text().splitlines(True),
                     (VARIANT / "source" / relative).read_text().splitlines(True),
                     fromfile="a/" + relative, tofile="b/" + relative))
    (validation / "source-changes.patch").write_text("".join(patch))

preview_count = len(list((VARIANT / "preview").glob("*.pbm")))
assert preview_count >= 70
metadata = {
    "release": "fs-i6x-en", "protocol": "0x36", "ui_language": "English",
    "baseline": json.loads((validation / "baseline-identity.json").read_text()),
    "toolchain": "Arm GNU GCC 15.3.1",
    "build_entrypoint": "./build.sh", "capacity": {
        **usage, "flash_limit": 131072, "ram_limit": 16384,
        "flash_free": 131072 - usage["flash_used"],
        "static_ram_free": 16384 - usage["static_ram_used"],
        "baseline_flash_used": baseline_usage["flash_used"],
        "baseline_static_ram_used": baseline_usage["static_ram_used"],
        "flash_saved": baseline_usage["flash_used"] - usage["flash_used"]},
    "font": {"glyphs": 65, "bitmap_bytes": 585, "type_pixels": 11,
             "cell_width": 6, "row_height": 12, "face_index": 7,
             "source_sha256": "b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a"},
    "changed_sources": {relative: final_sources[relative] for relative in changed},
    "validation": {"host": "PASS", "chassis_arm": "PASS", "radio_arm": "PASS",
        "actual_ui_frames": preview_count, "glyphs_bounds_overlap": "PASS",
        "font_regeneration_check": "PASS", "elf_hex_bin_consistency": "PASS byte-for-byte",
        "new_radio_warnings": False, "radio_warnings": warnings,
        "original_files_unchanged": original_count,
        "protected_control_and_protocol": "PASS unchanged",
        "svn_preflight": "PASS with existing workspace warnings",
        "hardware": "PENDING; no device flashed"},
    "artifacts": {str(p.relative_to(VARIANT)): {"bytes": p.stat().st_size, "sha256": sha(p)}
                  for p in sorted(firmware.glob("fs-i6x-en.*"))}}
(VARIANT / "release.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
summary = f"""# 英文版验证结果

- 基于当前工作区（SVN r104 加本地改动），协议保持 0x36；独立复制 {len(final_sources)} 个文件。
- 原中文版 {original_count} 个文件哈希保持；副本只有 5 个显示、字库、文档和 UI 测试文件改变。
- 控制循环、通信、授权、微调键屏蔽、保存参数及页面事件处理均保持原样。
- 全套 host、底盘 ARM、FS-i6X 正式 ARM 构建：PASS。
- 真实绘制回放 {preview_count} 帧，含全部 28 项 DEBUG 和遥测最大值；缺字、越界、重叠及反显检查 PASS。
- 字库重生成核对 PASS：65 个 ASCII 字符，585 字节，原字号与行距。
- Flash {usage['flash_used']}/131072，余 {131072-usage['flash_used']} 字节；
  静态 RAM {usage['static_ram_used']}/16384，余 {16384-usage['static_ram_used']} 字节。
- 独立中文版基线 Flash {baseline_usage['flash_used']} 字节，英文版减少 {baseline_usage['flash_used']-usage['flash_used']} 字节。
- 无新增遥控器构建警告；保留原有 RWX LOAD 段链接警告。
- ELF、HEX 提取的 BIN 与交付 BIN 逐字节一致。
- SVN 预检通过；工作区既有编译垃圾、BSP/工程配置及 IMU 差异提示见日志，未改写这些原文件。
- 未执行 SVN add/commit 或设备刷写。实机验收仍待执行，步骤见上级 README。

源码基线与保留文件记录见 JSON 清单；实际改动见 source-changes.patch；
完整构建、回归与预检输出分别见本目录对应日志。
"""
(validation / "results.md").write_text(summary)
deliveries = [VARIANT / "README.md", VARIANT / "release.json"]
deliveries += sorted(VARIANT.glob("*.py")) + [VARIANT / "build.sh"]
deliveries += sorted(firmware.glob("*")) + sorted((VARIANT / "preview").glob("*.png"))
deliveries += [validation / name for name in ("results.md", "source-baseline.json", "english-source.json",
    "baseline-seams.json", "baseline-identity.json", "original-preservation.json", "ui-translations.json",
    "source-changes.patch", "baseline-radio-build.log", "en-radio-build.log", "en-radio-final.log",
    "verify-all.log", "ui-render.log", "font-check.log", "svn-preflight.log")]
(VARIANT / "SHA256SUMS").write_text("".join(f"{sha(p)}  {p.relative_to(VARIANT)}\n" for p in deliveries))
print(json.dumps({"capacity": metadata["capacity"], "validation": metadata["validation"]}, ensure_ascii=False))
