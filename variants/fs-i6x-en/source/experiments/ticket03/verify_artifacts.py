#!/usr/bin/env python3
"""Check the built Experiment3 images against the recorded release manifest."""

import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).with_name("artifacts.json")


def main() -> int:
    records = json.loads(MANIFEST.read_text(encoding="utf-8"))
    failed = False
    for name, expected in records.items():
        path = ROOT / name
        try:
            data = path.read_bytes()
        except OSError as exc:
            print(f"FAIL {name}: {exc}", file=sys.stderr)
            failed = True
            continue
        actual = {
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        if actual != expected:
            print(f"FAIL {name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
        else:
            print(f"PASS {name}: {actual['bytes']} bytes {actual['sha256']}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
