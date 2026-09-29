#!/usr/bin/env python3
"""Restore large Cowboy Link assets from repository-friendly base64 parts."""
from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / ".payload" / "manifest.json"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def restore(entry: dict) -> None:
    target = ROOT / entry["target"]
    expected_size = int(entry["size"])
    expected_sha = entry["sha256"].lower()

    if target.is_file():
        current = target.read_bytes()
        if len(current) == expected_size and digest(current) == expected_sha:
            print(f"ok: {entry['target']}")
            return

    parts_dir = ROOT / entry["parts"]
    parts = sorted(parts_dir.glob("*.b64"))
    if not parts:
        raise RuntimeError(f"No payload parts found for {entry['target']}")

    encoded = "".join(
        "".join(part.read_text(encoding="ascii").split())
        for part in parts
    )
    data = base64.b64decode(encoded, validate=True)

    if len(data) != expected_size:
        raise RuntimeError(
            f"Size mismatch for {entry['target']}: {len(data)} != {expected_size}"
        )
    actual_sha = digest(data)
    if actual_sha != expected_sha:
        raise RuntimeError(
            f"SHA-256 mismatch for {entry['target']}: {actual_sha} != {expected_sha}"
        )

    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    print(f"restored: {entry['target']} ({len(data)} bytes)")


def main() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    for entry in manifest["files"]:
        restore(entry)


if __name__ == "__main__":
    main()
