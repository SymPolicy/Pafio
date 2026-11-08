#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import pathlib
import subprocess
import sys
import tempfile
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[1]
COPY_SCRIPT = ROOT / "scripts" / "copy-to-external-repo.sh"
CHECK_NO_BINARIES = ROOT / "scripts" / "check_no_binaries.py"
REPO_HYGIENE = ROOT / "scripts" / "repo-hygiene-gate.py"
DOCS_AUDIT = ROOT / "scripts" / "docs-audit.py"
NATIVE_CHECK = ROOT / "scripts" / "native-check.sh"
SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from artifact_policy import load_policy


POLICY = load_policy()
REQUIRED_PATHS = [pathlib.Path(path) for path in POLICY.required_delivery_paths]
FORBIDDEN_DIR_NAMES = set(POLICY.forbidden_dirs) | {".git"}
FORBIDDEN_FILE_NAMES = set(POLICY.forbidden_file_names)
FORBIDDEN_FILE_GLOBS = list(POLICY.forbidden_file_globs)


@dataclass(frozen=True)
class Step:
    name: str
    command: list[str]
    cwd: pathlib.Path


def run_step(step: Step) -> dict:
    log_root = os.environ.get("PAFIO_GATE_LOG_DIR")
    log_dir = pathlib.Path(log_root) / step.name if log_root else None
    env = None
    if log_dir:
        log_dir.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, PAFIO_GATE_LOG_DIR=str(log_dir))
    proc = subprocess.run(step.command, cwd=step.cwd, env=env, capture_output=True, text=True)
    if log_dir:
        (log_dir / "stdout.log").write_text(proc.stdout)
        (log_dir / "stderr.log").write_text(proc.stderr)
        (log_dir / "process.json").write_text(json.dumps({
            "command": step.command, "cwd": str(step.cwd), "returncode": proc.returncode,
        }) + "\n")
    return {
        "name": step.name,
        "command": step.command,
        "cwd": str(step.cwd),
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "ok": proc.returncode == 0,
    }


def validate_export_tree(export_root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    for required in REQUIRED_PATHS:
        if not (export_root / required).exists():
            errors.append(f"missing required delivery path: {required.as_posix()}")

    for path in export_root.rglob("*"):
        relative = path.relative_to(export_root)
        if FORBIDDEN_DIR_NAMES.intersection(relative.parts):
            errors.append(f"forbidden delivery path: {relative.as_posix()}")
            continue
        if path.is_file():
            if path.name in FORBIDDEN_FILE_NAMES:
                errors.append(f"forbidden delivery file: {relative.as_posix()}")
                continue
            if any(fnmatch.fnmatch(path.name, pattern) for pattern in FORBIDDEN_FILE_GLOBS):
                errors.append(f"forbidden delivery file: {relative.as_posix()}")
    return sorted(set(errors))


def delivery_steps(export_root: pathlib.Path) -> list[Step]:
    return [
        Step(
            "delivery_no_binaries",
            [sys.executable, str(CHECK_NO_BINARIES), "--repo-root", str(export_root), "--mode", "all"],
            export_root,
        ),
        Step(
            "delivery_repo_hygiene",
            [
                sys.executable,
                str(REPO_HYGIENE),
                "--repo-root",
                str(export_root),
                "--mode",
                "export",
                "--skip-doc-check",
            ],
            export_root,
        ),
        Step(
            "delivery_docs_governance",
            [sys.executable, str(export_root / "scripts" / "docs-audit.py")],
            export_root,
        ),
        Step(
            "delivery_native_check",
            [str(export_root / "scripts" / "native-check.sh")],
            export_root,
        ),
    ]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="delivery-gate.py")
    parser.add_argument("--json", action="store_true", help="emit machine-readable gate summary")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    results: list[dict] = []
    structural_errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="pafio-delivery-gate-") as temp_dir:
        export_root = pathlib.Path(temp_dir) / "delivery"
        copy_command = [str(COPY_SCRIPT), str(export_root)]
        copy_result = subprocess.run(copy_command, cwd=ROOT, capture_output=True, text=True)
        if log_root := os.environ.get("PAFIO_GATE_LOG_DIR"):
            log_dir = pathlib.Path(log_root) / "delivery_copy"
            log_dir.mkdir(parents=True, exist_ok=True)
            (log_dir / "stdout.log").write_text(copy_result.stdout)
            (log_dir / "stderr.log").write_text(copy_result.stderr)
            (log_dir / "process.json").write_text(json.dumps({
                "command": copy_command, "cwd": str(ROOT), "returncode": copy_result.returncode,
            }) + "\n")
        copy_result.check_returncode()
        structural_errors = validate_export_tree(export_root)
        if not structural_errors:
            for step in delivery_steps(export_root):
                result = run_step(step)
                results.append(result)
                if not result["ok"]:
                    break

    ok = not structural_errors and all(result["ok"] for result in results)
    payload = {"ok": ok, "structural_errors": structural_errors, "steps": results}
    if log_root := os.environ.get("PAFIO_GATE_LOG_DIR"):
        (pathlib.Path(log_root) / "report.json").write_text(json.dumps(payload) + "\n")

    if args.json:
        sys.stdout.write(json.dumps(payload, sort_keys=True) + "\n")
    else:
        for error in structural_errors:
            sys.stderr.write(f"{error}\n")
        for result in results:
            status = "OK" if result["ok"] else "FAIL"
            sys.stdout.write(f"[{status}] {result['name']}\n")
            if result["stdout"].strip():
                sys.stdout.write(result["stdout"])
                if not result["stdout"].endswith("\n"):
                    sys.stdout.write("\n")
            if result["stderr"].strip():
                sys.stderr.write(result["stderr"])
                if not result["stderr"].endswith("\n"):
                    sys.stderr.write("\n")
        sys.stdout.write(f"delivery gate {'passed' if ok else 'failed'}\n")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
