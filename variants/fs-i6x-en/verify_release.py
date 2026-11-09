#!/usr/bin/env python3
"""Verify delivered files, source isolation, protected control code and original files."""
import argparse
import hashlib
import json
import re
from pathlib import Path

VARIANT = Path(__file__).resolve().parent
GUI = "OpenI6X/FS-I6X/opentx/radio/src/gui/128x64/"
ALLOWED = {GUI + name for name in ("mower_user_ui.h", "view_main.cpp", "mower_zh_font.h", "mower_zh_font.md")}
ALLOWED.add("tools/test_blade_ok_ui.py")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def function_sha(text, signature, normalize_literals=False):
    body = function(text, signature)
    if normalize_literals:
        body = re.sub(r'"(?:\\.|[^"\\])*"', '""', body)
    return hashlib.sha256(body.encode()).hexdigest()


def verify_sources():
    baseline = json.loads((VARIANT / "validation/source-baseline.json").read_text())
    changed, final = [], {}
    for relative, expected in baseline.items():
        path = VARIANT / "source" / relative
        assert path.is_file() and not path.is_symlink(), relative
        final[relative] = sha(path)
        if final[relative] != expected:
            assert relative in ALLOWED, f"Unexpected source change: {relative}"
            changed.append(relative)
    assert set(changed) == ALLOWED, changed
    seams = json.loads((VARIANT / "validation/baseline-seams.json").read_text())
    view = (VARIANT / "source" / (GUI + "view_main.cpp")).read_bytes()
    start, end = view.index(b"static void mowerExp3Overlay("), view.index(b"static void mowerPage(")
    assert hashlib.sha256(view[:start]).hexdigest() == seams["view_prefix"]
    assert hashlib.sha256(view[end:]).hexdigest() == seams["view_suffix"]
    user = (VARIANT / "source" / (GUI + "mower_user_ui.h")).read_text()
    for item in seams["functions"]:
        assert function_sha(user, item["signature"], item["normalize_literals"]) == item["sha256"], item
    overlay = view[start:end].decode().split("\n#else\n", 1)[0]
    assert not re.search(r"[\u4e00-\u9fff]", user + overlay), "Chinese text remains in English production UI"
    print(f"PASS independent source: {len(final)} files; exactly 5 presentation/document/test changes; protected functions unchanged")
    return changed, final


def verify_original(original):
    manifest = json.loads((VARIANT / "validation/original-preservation.json").read_text())
    failures = [relative for relative, expected in manifest.items()
                if not (original / relative).is_file() or sha(original / relative) != expected]
    assert not failures, f"Original files changed: {failures}"
    print(f"PASS original Chinese workspace: {len(manifest)} file hashes unchanged")
    return len(manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original-root", type=Path)
    args = parser.parse_args()
    changed, final = verify_sources()
    recorded = json.loads((VARIANT / "validation/english-source.json").read_text())
    assert final == recorded, "Snapshot differs from the packaged English source"
    if args.original_root:
        if not (args.original_root / "OpenI6X/FS-I6X/opentx").is_dir():
            raise SystemExit(f"Original workspace not found: {args.original_root}")
        verify_original(args.original_root)
    else:
        print("Original preservation is a 20261103 baseline record; pass --original-root to check that frozen workspace")
    for line in (VARIANT / "SHA256SUMS").read_text().splitlines():
        expected, relative = line.split("  ", 1)
        assert sha(VARIANT / relative) == expected, relative
    metadata = json.loads((VARIANT / "release.json").read_text())
    assert set(changed) == set(metadata["changed_sources"])
    assert metadata["protocol"] == "0x36"
    assert metadata["capacity"]["flash_used"] <= 131072
    assert metadata["capacity"]["static_ram_used"] <= 16384
    assert not metadata["validation"]["new_radio_warnings"]
    print("PASS delivered SHA256 checksums, capacity and release metadata")


if __name__ == "__main__":
    main()
