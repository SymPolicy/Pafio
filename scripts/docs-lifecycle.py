#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import date
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
HISTORY = DOCS / "history"
ROLLUPS = DOCS / "rollups"
ARCHIVE = DOCS / "archive"
ARCHIVE_HISTORY = ARCHIVE / "history"
MANIFEST_PATH = ARCHIVE / "ARCHIVE-MANIFEST.json"
LEDGER_PATH = ARCHIVE / "ARCHIVE-LEDGER.md"
TODAY = date.today().isoformat()
DATE_FILE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}\.md$")


def default_manifest() -> dict[str, object]:
    return {
        "version": 1,
        "last_updated": TODAY,
        "archive_root": "docs/archive",
        "rollup_root": "docs/rollups",
        "keep_window": {"history": 1},
        "entries": [],
    }


def ensure_dirs() -> None:
    for path in (HISTORY, ROLLUPS, ARCHIVE, ARCHIVE_HISTORY):
        path.mkdir(parents=True, exist_ok=True)


def load_manifest() -> dict[str, object]:
    if not MANIFEST_PATH.exists():
        return default_manifest()
    payload = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise RuntimeError("archive manifest must be a JSON object")
    return payload


def save_manifest(manifest: dict[str, object]) -> None:
    MANIFEST_PATH.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def render_ledger(manifest: dict[str, object]) -> str:
    entries = manifest.get("entries", [])
    if not isinstance(entries, list):
        raise RuntimeError("archive manifest entries must be a JSON array")
    lines = [
        "# Archive Ledger",
        "",
        "**Purpose:** Track archived `spio` documentation provenance and lifecycle status; the machine-readable source of truth lives in [ARCHIVE-MANIFEST.json](./ARCHIVE-MANIFEST.json).",
        "",
        f"**Last updated:** {manifest.get('last_updated', TODAY)}",
        "",
        "## Status",
        "",
        f"- Archive root: `{manifest.get('archive_root', 'docs/archive')}`",
        f"- Rollup root: `{manifest.get('rollup_root', 'docs/rollups')}`",
        f"- History keep window: `{manifest.get('keep_window', {}).get('history', 1)}` active daily file(s)",
        "",
        "## Entries",
        "",
    ]
    if not entries:
        lines.append("- No archived `spio` docs are recorded yet. When active history or other provenance files are archived, register them in `ARCHIVE-MANIFEST.json` and refresh this ledger.")
    else:
        lines.extend(["| Source | Status | Archive Path |", "|--------|--------|--------------|"])
        for entry in entries:
            if not isinstance(entry, dict):
                raise RuntimeError("archive manifest entry must be a JSON object")
            lines.append(
                f"| `{entry.get('source_path', '')}` | `{entry.get('status', '')}` | `{entry.get('archive_path', '')}` |"
            )
    lines.append("")
    return "\n".join(lines)


def refresh() -> int:
    ensure_dirs()
    manifest = load_manifest()
    manifest["version"] = 1
    manifest["last_updated"] = TODAY
    manifest.setdefault("archive_root", "docs/archive")
    manifest.setdefault("rollup_root", "docs/rollups")
    manifest.setdefault("keep_window", {"history": 1})
    manifest.setdefault("entries", [])
    save_manifest(manifest)
    LEDGER_PATH.write_text(render_ledger(manifest), encoding="utf-8")
    return 0


def validate() -> int:
    errors: list[str] = []
    for path in (HISTORY, ROLLUPS, ARCHIVE, ARCHIVE_HISTORY):
        if not path.exists():
            errors.append(f"missing lifecycle directory: {path.relative_to(ROOT).as_posix()}")
    for path in (ROLLUPS / "CURRENT-STATE.md", ROLLUPS / "NEXT-STAGE-GAP-LEDGER.md", MANIFEST_PATH, LEDGER_PATH):
        if not path.exists():
            errors.append(f"missing lifecycle file: {path.relative_to(ROOT).as_posix()}")

    for path in HISTORY.glob("*.md"):
        if path.name in {"README.md", "INDEX.md"}:
            continue
        if not DATE_FILE_RE.match(path.name):
            errors.append(f"history file must use YYYY-MM-DD.md: {path.relative_to(ROOT).as_posix()}")
    for path in ARCHIVE_HISTORY.glob("*.md"):
        if path.name in {"README.md", "INDEX.md"}:
            continue
        if not DATE_FILE_RE.match(path.name):
            errors.append(f"archived history file must use YYYY-MM-DD.md: {path.relative_to(ROOT).as_posix()}")

    if MANIFEST_PATH.exists():
        try:
            manifest = load_manifest()
            current = LEDGER_PATH.read_text(encoding="utf-8") if LEDGER_PATH.exists() else ""
            if current != render_ledger(manifest):
                errors.append("archive ledger is out of date; run `python3 scripts/docs-lifecycle.py refresh`")
            entries = manifest.get("entries", [])
            if not isinstance(entries, list):
                errors.append("archive manifest entries must be a JSON array")
            else:
                for index, entry in enumerate(entries):
                    if not isinstance(entry, dict):
                        errors.append(f"archive manifest entry #{index} must be a JSON object")
                        continue
                    for key in ("source_path", "status", "archive_path"):
                        if key not in entry:
                            errors.append(f"archive manifest entry #{index} is missing `{key}`")
                    archive_path = entry.get("archive_path")
                    if isinstance(archive_path, str) and archive_path:
                        candidate = ROOT / archive_path
                        if not candidate.exists():
                            errors.append(f"archive manifest target does not exist: {archive_path}")
        except Exception as exc:  # pragma: no cover
            errors.append(str(exc))

    if errors:
        sys.stderr.write("docs lifecycle validation failed:\n")
        for error in errors:
            sys.stderr.write(f"- {error}\n")
        return 1

    sys.stdout.write("docs lifecycle validation passed\n")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Refresh or validate spio docs lifecycle metadata.")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("refresh", help="refresh archive manifest/ledger metadata")
    sub.add_parser("validate", help="validate docs lifecycle directories and ledger freshness")
    args = parser.parse_args()
    if args.command == "refresh":
        return refresh()
    return validate()


if __name__ == "__main__":
    raise SystemExit(main())
