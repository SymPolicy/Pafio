#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
REQUIRED_COLLECTION_DIRS = [
    DOCS,
    DOCS / "adr",
    DOCS / "archive",
    DOCS / "archive" / "history",
    DOCS / "governance",
    DOCS / "history",
    DOCS / "operations",
    DOCS / "planning",
    DOCS / "registry",
    DOCS / "rollups",
    DOCS / "security",
    DOCS / "styio",
    DOCS / "teams",
]
PURPOSE_RE = re.compile(r"^\*\*Purpose:\*\*\s+.+$", re.M)
LAST_UPDATED_RE = re.compile(r"^\*\*Last updated:\*\*\s+[0-9]{4}-[0-9]{2}-[0-9]{2}\s*$", re.M)
DATE_FILE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}\.md$")


def check_collections() -> list[str]:
    errors: list[str] = []
    for path in REQUIRED_COLLECTION_DIRS:
        if not path.exists():
            errors.append(f"missing docs collection: {path.relative_to(ROOT).as_posix()}")
            continue
        if not (path / "README.md").exists():
            errors.append(f"missing collection README: {(path / 'README.md').relative_to(ROOT).as_posix()}")
        if not (path / "INDEX.md").exists():
            errors.append(f"missing collection INDEX: {(path / 'INDEX.md').relative_to(ROOT).as_posix()}")
    return errors


def check_metadata() -> list[str]:
    errors: list[str] = []
    for path in sorted(DOCS.rglob("*.md")):
        text = path.read_text(encoding="utf-8")
        rel = path.relative_to(ROOT).as_posix()
        if not PURPOSE_RE.search(text):
            errors.append(f"missing Purpose line: {rel}")
        if not LAST_UPDATED_RE.search(text):
            errors.append(f"missing Last updated line: {rel}")
    return errors


def check_history_names() -> list[str]:
    errors: list[str] = []
    for base in (DOCS / "history", DOCS / "archive" / "history"):
        for path in sorted(base.glob("*.md")):
            if path.name in {"README.md", "INDEX.md"}:
                continue
            if not DATE_FILE_RE.match(path.name):
                errors.append(f"dated history file must use YYYY-MM-DD.md: {path.relative_to(ROOT).as_posix()}")
    return errors


def run_check(command: list[str]) -> list[str]:
    proc = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if proc.returncode == 0:
        return []
    detail = proc.stderr.strip() or proc.stdout.strip() or "subprocess failed"
    return [detail]


def main() -> int:
    errors: list[str] = []
    errors.extend(check_collections())
    errors.extend(check_metadata())
    errors.extend(check_history_names())
    errors.extend(run_check([sys.executable, "scripts/docs-index.py", "--check"]))
    errors.extend(run_check([sys.executable, "scripts/docs-lifecycle.py", "validate"]))

    if errors:
        sys.stderr.write("docs audit failed:\n")
        for error in errors:
            for line in error.splitlines():
                sys.stderr.write(f"- {line}\n")
        return 1

    sys.stdout.write("docs audit passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
