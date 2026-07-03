#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[1]
TODAY = date.today().isoformat()
COLLECTION_DIRS = [
    Path("docs"),
    Path("docs/adr"),
    Path("docs/audit"),
    Path("docs/assets"),
    Path("docs/assets/workflow"),
    Path("docs/governance"),
    Path("docs/operations"),
    Path("docs/plan"),
    Path("docs/registry"),
    Path("docs/security"),
    Path("docs/specs"),
    Path("docs/specs/audit"),
    Path("docs/external"),
    Path("docs/external/for-styio"),
    Path("docs/teams"),
]
INDEX_META = {
    "docs": ("pafio Docs Index", "Provide the generated inventory for `docs/`; directory boundaries and maintenance rules live in [README.md](./README.md)."),
    "docs/adr": ("ADR Index", "Provide the generated inventory for `docs/adr/`; decision-record conventions live in [README.md](./README.md)."),
    "docs/audit": ("Audit Index", "Provide the generated inventory for `docs/audit/`; transient defect records live in ignored `docs/audit/defects/` and are enforced by external `styio-audit` runs."),
    "docs/assets": ("Assets Index", "Provide the generated inventory for `docs/assets/`; reusable workflow assets live in [README.md](./README.md)."),
    "docs/assets/workflow": ("Workflow Assets Index", "Provide the generated inventory for `docs/assets/workflow/`; gate and workflow guidance live in [README.md](./README.md)."),
    "docs/governance": ("Governance Index", "Provide the generated inventory for `docs/governance/`; normative rules and contracts live in [README.md](./README.md)."),
    "docs/operations": ("Operations Index", "Provide the generated inventory for `docs/operations/`; gates and operational runbooks live in [README.md](./README.md)."),
    "docs/plan": ("Planning Index", "Provide the generated inventory for `docs/plan/`; phased implementation plans live in [README.md](./README.md)."),
    "docs/registry": ("Registry Index", "Provide the generated inventory for `docs/registry/`; package registry contracts live in [README.md](./README.md)."),
    "docs/security": ("Security Index", "Provide the generated inventory for `docs/security/`; public/private security boundary docs live in [README.md](./README.md)."),
    "docs/specs": ("Specs Index", "Provide the generated inventory for `docs/specs/`; cross-cutting agent and audit rules live in [README.md](./README.md)."),
    "docs/specs/audit": ("Audit Specs Index", "Provide the generated inventory for `docs/specs/audit/`; audit checklist ownership lives in [README.md](./README.md)."),
    "docs/external": ("External Docs Index", "Provide the generated inventory for `docs/external/`; external handoff boundaries live in [README.md](./README.md)."),
    "docs/external/for-styio": ("Styio Handoff Index", "Provide the generated inventory for `docs/external/for-styio/`; external compiler knowledge and requirements live in [README.md](./README.md)."),
    "docs/teams": ("Teams Index", "Provide the generated inventory for `docs/teams/`; team ownership and runbook boundaries live in [README.md](./README.md)."),
}
TITLE_RE = re.compile(r"^#\s+(.+?)\s*$", re.M)
PURPOSE_RE = re.compile(r"^\*\*Purpose:\*\*\s+(.+?)\s*$", re.M)
LAST_UPDATED_RE = re.compile(r"^\*\*Last updated:\*\*\s+([0-9]{4}-[0-9]{2}-[0-9]{2})\s*$", re.M)
LINK_RE = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")


@dataclass
class Entry:
    rel_path: str
    link_target: str
    label: str
    summary: str
    is_dir: bool
    last_updated: str


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def compact_plain(text: str) -> str:
    text = LINK_RE.sub(lambda match: match.group(1), text)
    for token in ("**", "`", "__"):
        text = text.replace(token, "")
    return re.sub(r"\s+", " ", text.replace("|", "\\|")).strip()


def extract_title(path: Path) -> str:
    match = TITLE_RE.search(read_text(path))
    return compact_plain(match.group(1)) if match else path.stem


def extract_purpose(path: Path) -> str:
    match = PURPOSE_RE.search(read_text(path))
    return compact_plain(match.group(1)) if match else f"Inventory entry for `{path.name}`."


def extract_last_updated(path: Path) -> str:
    match = LAST_UPDATED_RE.search(read_text(path))
    return match.group(1) if match else TODAY


def rel_link(from_dir: Path, target: Path) -> str:
    rel = Path(os.path.relpath(target, from_dir)).as_posix()
    return rel if rel.startswith(".") else f"./{rel}"


def choose_dir_entry(path: Path) -> Path | None:
    for name in ("INDEX.md", "README.md"):
        candidate = path / name
        if candidate.exists():
            return candidate
    return None


def build_entries(base: Path) -> list[Entry]:
    entries: list[Entry] = []
    children = [child for child in base.iterdir() if child.name not in {"README.md", "INDEX.md"} and not child.name.startswith(".")]
    for child in sorted(children, key=lambda item: (0 if item.is_dir() else 1, item.name.lower())):
        if child.is_dir():
            entry_target = choose_dir_entry(child)
            if entry_target is None:
                continue
            summary_source = child / "README.md" if (child / "README.md").exists() else entry_target
            entries.append(
                Entry(
                    rel_path=f"{child.name}/",
                    link_target=rel_link(base, entry_target),
                    label=extract_title(entry_target),
                    summary=extract_purpose(summary_source),
                    is_dir=True,
                    last_updated=extract_last_updated(summary_source),
                )
            )
            continue
        if child.suffix != ".md":
            continue
        entries.append(
            Entry(
                rel_path=child.name,
                link_target=rel_link(base, child),
                label=extract_title(child),
                summary=extract_purpose(child),
                is_dir=False,
                last_updated=extract_last_updated(child),
            )
        )
    return entries


def render_table(entries: Iterable[Entry]) -> list[str]:
    rows = ["| Path | Entry | Summary |", "|------|-------|---------|"]
    for entry in entries:
        rows.append(f"| `{entry.rel_path}` | [{entry.label}]({entry.link_target}) | {entry.summary} |")
    return rows


def render_index(base: Path) -> str:
    rel = base.relative_to(ROOT).as_posix()
    title, purpose = INDEX_META[rel]
    entries = build_entries(base)
    updated = max((entry.last_updated for entry in entries), default=TODAY)
    dir_entries = [entry for entry in entries if entry.is_dir]
    file_entries = [entry for entry in entries if not entry.is_dir]

    lines = [
        f"# {title}",
        "",
        f"**Purpose:** {purpose}",
        "",
        f"**Last updated:** {updated}",
        "",
        "> Generated by `python3 scripts/docs-index.py --write`. Edit `README.md` for scope and rules, then re-run the generator after docs-tree changes.",
    ]
    if dir_entries:
        lines.extend(["", "## Directories", ""])
        lines.extend(render_table(dir_entries))
    if file_entries:
        lines.extend(["", "## Files", ""])
        lines.extend(render_table(file_entries))
    lines.append("")
    return "\n".join(lines)


def sync_indexes(check: bool) -> int:
    failures: list[str] = []
    for rel_dir in sorted(COLLECTION_DIRS, key=lambda path: len(path.parts), reverse=True):
        base = ROOT / rel_dir
        index_path = base / "INDEX.md"
        expected = render_index(base)
        current = index_path.read_text(encoding="utf-8") if index_path.exists() else None
        if check:
            if current != expected:
                failures.append(index_path.relative_to(ROOT).as_posix())
            continue
        index_path.write_text(expected, encoding="utf-8")
    if failures:
        print("Out-of-date generated indexes:", file=sys.stderr)
        for item in failures:
            print(f"  - {item}", file=sys.stderr)
        print("Run: python3 scripts/docs-index.py --write", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate or verify pafio docs INDEX.md files.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true")
    group.add_argument("--check", action="store_true")
    args = parser.parse_args()
    return sync_indexes(check=args.check)


if __name__ == "__main__":
    raise SystemExit(main())
