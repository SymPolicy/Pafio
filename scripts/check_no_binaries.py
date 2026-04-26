#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from artifact_policy import DEFAULT_POLICY_PATH, load_policy


def is_git_repo(repo_root: pathlib.Path) -> bool:
    return (repo_root / ".git").exists()


def tracked_files(repo_root: pathlib.Path) -> list[pathlib.Path]:
    proc = subprocess.run(
        ["git", "ls-files", "-z", "--cached"],
        cwd=repo_root,
        check=True,
        capture_output=True,
    )
    entries = [entry for entry in proc.stdout.split(b"\0") if entry]
    return [repo_root / pathlib.Path(entry.decode("utf-8", errors="surrogateescape")) for entry in entries]


def filesystem_files(repo_root: pathlib.Path) -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for path in repo_root.rglob("*"):
        if ".git" in path.relative_to(repo_root).parts:
            continue
        if path.is_file():
            files.append(path)
    return files


def candidate_files(repo_root: pathlib.Path, mode: str) -> list[pathlib.Path]:
    if mode == "tracked":
        return tracked_files(repo_root)
    if mode == "all":
        return filesystem_files(repo_root)
    if is_git_repo(repo_root):
        return tracked_files(repo_root)
    return filesystem_files(repo_root)


def is_binary_content(data: bytes) -> bool:
    if not data:
        return False
    if b"\x00" in data:
        return True
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return True
    return False


def path_is_allowed(path: pathlib.Path, repo_root: pathlib.Path, allow_globs: list[str]) -> bool:
    relative = path.relative_to(repo_root).as_posix()
    return any(fnmatch.fnmatch(relative, pattern) for pattern in allow_globs)


def scan_repo(repo_root: pathlib.Path, allow_globs: list[str], mode: str = "auto") -> list[pathlib.Path]:
    offenders: list[pathlib.Path] = []
    for file_path in candidate_files(repo_root, mode):
        if path_is_allowed(file_path, repo_root, allow_globs):
            continue
        if not file_path.is_file():
            continue
        if is_binary_content(file_path.read_bytes()):
            offenders.append(file_path.relative_to(repo_root))
    return offenders


def effective_allow_globs(
    *,
    explicit_allow_globs: list[str],
    use_policy_allowlist: bool,
    policy_path: pathlib.Path | None,
) -> list[str]:
    allow_globs = list(explicit_allow_globs)
    if use_policy_allowlist:
        policy = load_policy(policy_path)
        allow_globs.extend(policy.tracked_binary_allow_globs)
    return allow_globs


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="check_no_binaries.py",
        description="Fail when tracked repository files contain binary content.",
    )
    parser.add_argument(
        "--repo-root",
        type=pathlib.Path,
        default=ROOT,
        help="repository root to scan (defaults to this repo)",
    )
    parser.add_argument(
        "--allow-glob",
        action="append",
        default=[],
        help="repo-relative glob for intentionally tracked binary assets",
    )
    parser.add_argument(
        "--policy",
        type=pathlib.Path,
        default=None,
        help=f"override artifact policy JSON path (default: {DEFAULT_POLICY_PATH})",
    )
    parser.add_argument(
        "--no-policy-allowlist",
        action="store_true",
        help="disable tracked_binary_allow_globs from artifact policy",
    )
    parser.add_argument(
        "--mode",
        choices=["auto", "tracked", "all"],
        default="auto",
        help="scan tracked files in git repos, all files in exports, or force one mode",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    try:
        allow_globs = effective_allow_globs(
            explicit_allow_globs=args.allow_glob,
            use_policy_allowlist=not args.no_policy_allowlist,
            policy_path=args.policy,
        )
    except (FileNotFoundError, ValueError, OSError, json.JSONDecodeError) as exc:
        sys.stderr.write(f"failed to load binary allowlist policy: {exc}\n")
        return 2

    offenders = scan_repo(repo_root, allow_globs, args.mode)
    if offenders:
        sys.stderr.write("binary files are not allowed in this repository:\n")
        for offender in offenders:
            sys.stderr.write(f"- {offender.as_posix()}\n")
        if allow_globs:
            sys.stderr.write(
                "If a binary asset is intentional, rerun with --allow-glob for that path pattern.\n"
            )
        return 1

    sys.stdout.write(f"binary file check passed for {repo_root}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
