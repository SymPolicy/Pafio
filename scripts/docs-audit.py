#!/usr/bin/env python3
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
REQUIRED_COLLECTION_DIRS = [
    DOCS,
    DOCS / "adr",
    DOCS / "audit",
    DOCS / "assets",
    DOCS / "assets" / "workflow",
    DOCS / "governance",
    DOCS / "operations",
    DOCS / "plan",
    DOCS / "registry",
    DOCS / "security",
    DOCS / "specs",
    DOCS / "specs" / "audit",
    DOCS / "external",
    DOCS / "external" / "for-styio",
    DOCS / "teams",
]
PURPOSE_RE = re.compile(r"^\*\*Purpose:\*\*\s+.+$", re.M)
LAST_UPDATED_RE = re.compile(r"^\*\*Last updated:\*\*\s+[0-9]{4}-[0-9]{2}-[0-9]{2}\s*$", re.M)
DEFECTS = DOCS / "audit" / "defects"
STATUS_FIELD_RE = re.compile(r"^\*\*Status:\*\*\s*(\S[^\n]*)$", re.M)
CLOSED_STATUSES = {"closed", "resolved", "cleared"}
OPEN_STATUSES = {"open", "triaged", "accepted", "in progress", "in-progress", "reopened"}
CLOSURE_EVIDENCE_RE = re.compile(r"^\*\*Closure evidence:\*\*\s*([^\n]*)$", re.M)


def _queue_entries(defects: Path) -> list[os.DirEntry]:
    """Lstat-based walk: yields every entry without following links, so a
    symlinked directory cannot smuggle records past the gate."""
    entries: list[os.DirEntry] = []
    stack = [os.fspath(defects)]
    while stack:
        with os.scandir(stack.pop()) as it:
            for entry in it:
                entries.append(entry)
                if entry.is_dir(follow_symlinks=False) and not entry.is_symlink():
                    stack.append(entry.path)
    return sorted(entries, key=lambda e: e.path)


def check_defect_queue(defects: Path = DEFECTS, root: Path = ROOT) -> list[str]:
    """The ignored defect queue is scanned from the filesystem, not git:
    missing or empty passes; every markdown record must declare exactly one
    recognized status; an open, malformed, conflicting or non-markdown record
    blocks; a closed record must carry non-empty closure evidence; symlinks
    anywhere in the queue block."""
    errors: list[str] = []
    rel_root = defects.relative_to(root).as_posix()
    if defects.is_symlink():
        return [f"symlinked audit queue root blocks the gate: {rel_root}"]
    if not defects.exists():
        return errors
    if not defects.is_dir():
        return [f"audit queue root is not a directory: {rel_root}"]
    for entry in _queue_entries(defects):
        path = Path(entry.path)
        rel = path.relative_to(root).as_posix()
        if entry.is_symlink():
            errors.append(f"symlink blocks the audit queue: {rel}")
            continue
        if entry.is_dir(follow_symlinks=False):
            continue
        if not entry.is_file(follow_symlinks=False):
            errors.append(f"special file blocks the audit queue: {rel}")
            continue
        if path.suffix.lower() != ".md":
            errors.append(f"non-markdown defect record blocks the audit queue: {rel}")
            continue
        text = path.read_text(encoding="utf-8")
        statuses = STATUS_FIELD_RE.findall(text)
        if len(statuses) != 1:
            errors.append(f"missing or conflicting status field blocks the audit queue: {rel}")
            continue
        status = statuses[0].strip().lower()
        evidence = CLOSURE_EVIDENCE_RE.findall(text)
        non_empty_evidence = [e.strip() for e in evidence if e.strip()]
        if status in CLOSED_STATUSES:
            if len(evidence) != 1 or not non_empty_evidence:
                errors.append(f"closed defect record lacks closure evidence: {rel}")
        elif status in OPEN_STATUSES:
            errors.append(f"open defect record blocks the audit queue: {rel}")
        else:
            errors.append(f"malformed defect record blocks the audit queue: {rel}")
    return errors


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
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8")
        if not PURPOSE_RE.search(text):
            errors.append(f"missing Purpose line: {rel}")
        if not LAST_UPDATED_RE.search(text):
            errors.append(f"missing Last updated line: {rel}")
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
    errors.extend(check_defect_queue())
    errors.extend(run_check([sys.executable, "scripts/docs-index.py", "--check"]))
    errors.extend(run_check([sys.executable, "scripts/docs-lifecycle.py", "validate"]))
    if os.environ.get("STYIO_SKIP_TEAM_DOC_GATE") != "1":
        errors.extend(run_check([sys.executable, "scripts/team-docs-gate.py"]))

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
