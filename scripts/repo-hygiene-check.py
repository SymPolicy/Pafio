#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from artifact_policy import load_policy


POLICY = load_policy()
FORBIDDEN_DIRS = set(POLICY.forbidden_dirs)
FORBIDDEN_FILE_NAMES = set(POLICY.forbidden_file_names)
FORBIDDEN_FILE_GLOBS = list(POLICY.forbidden_file_globs)
REQUIRED_GITIGNORE_PATTERNS = list(POLICY.required_gitignore_patterns)
REQUIRED_DOC_REFERENCES = {
    pathlib.Path("docs/operations/Pafio-Verification-Matrix.md"): [
        "scripts/docs-audit.py",
        "scripts/submit-gate.py",
        "scripts/perf-gate.py",
        "scripts/repo-hygiene-check.py",
        "scripts/delivery-gate.py",
    ],
    pathlib.Path("docs/governance/Pafio-Entry-Argument-Index.md"): [
        "scripts/docs-index.py",
        "scripts/docs-lifecycle.py",
        "scripts/docs-audit.py",
        "scripts/submit-gate.py",
        "scripts/perf-gate.py",
        "scripts/repo-hygiene-check.py",
        "scripts/delivery-gate.py",
    ],
    pathlib.Path("docs/governance/Docs-Maintenance-Model.md"): [
        "scripts/docs-index.py",
        "scripts/docs-lifecycle.py",
        "scripts/docs-audit.py",
    ],
}


def is_git_repo(repo_root: pathlib.Path) -> bool:
    return (repo_root / ".git").exists()


def git_tracked_paths(repo_root: pathlib.Path) -> list[pathlib.Path]:
    proc = subprocess.run(
        ["git", "ls-files", "-z", "--cached"],
        cwd=repo_root,
        check=True,
        capture_output=True,
    )
    entries = [entry for entry in proc.stdout.split(b"\0") if entry]
    return [pathlib.Path(entry.decode("utf-8", errors="surrogateescape")) for entry in entries]


def filesystem_paths(repo_root: pathlib.Path) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    for path in repo_root.rglob("*"):
        relative = path.relative_to(repo_root)
        if ".git" in relative.parts:
            continue
        paths.append(relative)
    return paths


def candidate_paths(repo_root: pathlib.Path, mode: str) -> list[pathlib.Path]:
    if mode == "tracked":
        return git_tracked_paths(repo_root)
    if mode == "all":
        return filesystem_paths(repo_root)
    if is_git_repo(repo_root):
        return git_tracked_paths(repo_root)
    return filesystem_paths(repo_root)


def is_forbidden_path(relative_path: pathlib.Path) -> bool:
    if FORBIDDEN_DIRS.intersection(relative_path.parts):
        return True
    if relative_path.name in FORBIDDEN_FILE_NAMES:
        return True
    return any(fnmatch.fnmatch(relative_path.name, pattern) for pattern in FORBIDDEN_FILE_GLOBS)


def check_forbidden_paths(repo_root: pathlib.Path, mode: str) -> list[str]:
    errors: list[str] = []
    for relative_path in sorted(candidate_paths(repo_root, mode)):
        if is_forbidden_path(relative_path):
            errors.append(f"forbidden repository path: {relative_path.as_posix()}")
    return errors


def check_gitignore(repo_root: pathlib.Path) -> list[str]:
    gitignore = repo_root / ".gitignore"
    if not gitignore.exists():
        return [".gitignore is missing"]

    patterns = {
        line.strip()
        for line in gitignore.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    errors = []
    for required in REQUIRED_GITIGNORE_PATTERNS:
        if required not in patterns:
            errors.append(f".gitignore must include: {required}")
    return errors


def check_doc_references(repo_root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    for relative_path, needles in REQUIRED_DOC_REFERENCES.items():
        path = repo_root / relative_path
        if not path.exists():
            errors.append(f"required documentation file is missing: {relative_path.as_posix()}")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                errors.append(f"{relative_path.as_posix()} must document {needle}")
    return errors


def check_repo(repo_root: pathlib.Path, mode: str = "auto", *, check_docs: bool = True) -> list[str]:
    errors: list[str] = []
    errors.extend(check_forbidden_paths(repo_root, mode))
    errors.extend(check_gitignore(repo_root))
    if check_docs:
        errors.extend(check_doc_references(repo_root))
    return errors


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="repo-hygiene-check.py",
        description="Validate repository hygiene for tracked sources and delivery exports.",
    )
    parser.add_argument(
        "--repo-root",
        type=pathlib.Path,
        default=ROOT,
        help="repository root or exported delivery tree to scan",
    )
    parser.add_argument(
        "--mode",
        choices=["auto", "tracked", "all"],
        default="auto",
        help="scan tracked files in git repos, all files in exports, or force one mode",
    )
    parser.add_argument(
        "--skip-doc-check",
        action="store_true",
        help="skip checks that gate commands are documented in operations/governance docs",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    errors = check_repo(repo_root, args.mode, check_docs=not args.skip_doc_check)

    if errors:
        sys.stderr.write("repository hygiene check failed:\n")
        for error in errors:
            sys.stderr.write(f"- {error}\n")
        return 1

    sys.stdout.write(f"repository hygiene check passed for {repo_root}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
