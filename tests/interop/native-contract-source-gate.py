#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SKIP_DIRS = {
    ".git",
    ".mypy_cache",
    ".pytest_cache",
    ".ruff_cache",
    ".spio",
    ".venv",
    "build",
    "build-codex",
    "dist",
    "node_modules",
    "tmp",
    "__pycache__",
}
FORBIDDEN_NAME_PARTS = [
    ("open" + "api"),
    ("ara" + "zzo"),
    ("redo" + "cly"),
]
FORBIDDEN_TEXT_PARTS = [
    ("Open" + "API"),
    ("open" + "api"),
    ("Ara" + "zzo"),
    ("ara" + "zzo"),
    ("Redo" + "cly"),
    ("redo" + "cly"),
]


def tracked_files() -> list[Path]:
    result = subprocess.run(["git", "-C", str(ROOT), "ls-files"], text=True, capture_output=True)
    if result.returncode == 0:
        return [ROOT / line for line in result.stdout.splitlines() if line]
    files: list[Path] = []
    for path in ROOT.rglob("*"):
        if any(part in SKIP_DIRS or part.startswith("build-") for part in path.relative_to(ROOT).parts):
            continue
        if path.is_file():
            files.append(path)
    return sorted(files, key=lambda item: item.relative_to(ROOT).as_posix())


def main() -> int:
    problems: list[str] = []
    for path in tracked_files():
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        lowered = rel.lower()
        for needle in FORBIDDEN_NAME_PARTS:
            if needle in lowered:
                problems.append(f"{rel}: forbidden non-native contract filename")
                break
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for needle in FORBIDDEN_TEXT_PARTS:
            if needle in text:
                problems.append(f"{rel}: forbidden non-native contract reference")
                break
    if problems:
        print("[native-contract-source-gate] FAILED", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    print("[native-contract-source-gate] ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
