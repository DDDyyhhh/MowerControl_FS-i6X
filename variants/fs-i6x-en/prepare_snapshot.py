#!/usr/bin/env python3
"""Create the isolated source snapshot once; never replace an existing snapshot."""
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

variant = Path(__file__).resolve().parent
original = variant.parents[1]
source = variant / "source"
validation = variant / "validation"
roots = ["OpenI6X/FS-I6X/opentx", "control_chassis", "experiments/ticket03",
         "tools", "cmake", "docs"]
excluded = {".git", ".svn", ".agents", ".aws", ".codex", ".vscode",
            ".codebuddy", ".workbuddy", "downloads", "__pycache__",
            "releases", "Objects", "Listings"}


def files(relative):
    for base, dirs, names in os.walk(original / relative):
        dirs[:] = sorted(d for d in dirs if d not in excluded and not d.startswith("build"))
        for name in sorted(names):
            if name.endswith((".bak", ".stackdump", ".pyc")):
                continue
            path = Path(base) / name
            if path.is_symlink():
                raise RuntimeError(f"Review symbolic link before copying: {path}")
            yield path.relative_to(original)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


if source.exists():
    raise SystemExit("Source snapshot already exists; refusing to overwrite it")
validation.mkdir(parents=True, exist_ok=True)
selected = sorted({p for root in roots for p in files(root)} | {Path("AGENTS.md")})
manifest = {}
for relative in selected:
    src, dst = original / relative, source / relative
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    manifest[str(relative)] = digest(src)
    assert digest(dst) == manifest[str(relative)]
(validation / "source-baseline.json").write_text(json.dumps(manifest, indent=2) + "\n")
preserve = dict(manifest)
for relative in files("remote_lvgl"):
    preserve[str(relative)] = digest(original / relative)
for path in (original / "releases/20261103-main-trim-key-lock").rglob("*"):
    if path.is_file():
        preserve[str(path.relative_to(original))] = digest(path)
# Record existing primary radio binaries too, without copying build caches.
for directory in (original / "OpenI6X/FS-I6X/opentx").glob("build*"):
    for name in ("firmware.bin", "firmware.elf", "firmware.hex"):
        path = directory / name
        if path.is_file():
            preserve[str(path.relative_to(original))] = digest(path)
(validation / "original-preservation.json").write_text(json.dumps(preserve, indent=2) + "\n")
identity = {}
for key in ("url", "revision", "last-changed-revision"):
    result = subprocess.run(["svn", "info", "--show-item", key, str(original)],
                            capture_output=True, text=True, check=True)
    identity[key] = result.stdout.strip()
identity["baseline"] = "Current working files, including local modifications; not SVN HEAD"
identity["snapshot_files"] = len(manifest)
identity["preserved_original_files"] = len(preserve)
(validation / "baseline-identity.json").write_text(json.dumps(identity, indent=2) + "\n")
print(f"Copied {len(manifest)} independent files; recorded {len(preserve)} original file hashes")
