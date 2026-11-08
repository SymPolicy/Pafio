#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from artifact_policy import load_policy

POLICY = load_policy()
POLICY_PATH = "scripts/artifact-policy.json"
DEFAULT_MAX_FILE_BYTES = 20 * 1024 * 1024
TEXT_EXTENSIONS = {".cmake", ".cpp", ".h", ".hpp", ".json", ".md", ".py", ".sh", ".toml", ".txt", ".xml", ".yaml", ".yml"}
FORBIDDEN_DIRS = set(POLICY.forbidden_dirs)
FORBIDDEN_DIR_GLOBS = list(POLICY.forbidden_dir_globs)
FORBIDDEN_FILE_NAMES = set(POLICY.forbidden_file_names)
FORBIDDEN_FILE_GLOBS = list(POLICY.forbidden_file_globs)
FORBIDDEN_CONTRACT_TERMS = list(POLICY.forbidden_contract_terms)
REQUIRED_GITIGNORE_PATTERNS = list(POLICY.required_gitignore_patterns)
REQUIRED_DOC_REFERENCES = {
    Path("docs/governance/Docs-Maintenance-Model.md"): [
        "scripts/docs-index.py",
        "scripts/docs-lifecycle.py",
        "scripts/docs-audit.py",
    ],
    Path("docs/BUILD-AND-DEV-ENV.md"): [
        "cmake -S .",
        "ctest --test-dir",
        "./scripts/pafio",
    ],
    Path("docs/governance/Pafio-CLI-Contract.md"): [
        "pafio metadata --json",
        "PAFIO_STYIO_BIN",
        "pafio doctor",
    ],
    Path("docs/assets/workflow/REPO-HYGIENE.md"): [
        "scripts/repo-hygiene-gate.py",
        "scripts/delivery-gate.sh",
    ],
}


def run_git(repo_root: Path, *args: str, check: bool = True) -> str:
    result = subprocess.run(["git", "-C", str(repo_root), *args], check=check, text=True, capture_output=True)
    return result.stdout


def staged_files(repo_root: Path) -> list[str]:
    return [line for line in run_git(repo_root, "diff", "--cached", "--name-only", "--diff-filter=ACMR").splitlines() if line]


def worktree_files(repo_root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(repo_root), "status", "--porcelain=v1", "--untracked-files=all"],
        check=True,
        text=True,
        capture_output=True,
    )
    paths: list[str] = []
    for line in result.stdout.splitlines():
        if not line:
            continue
        raw = line[3:]
        if " -> " in raw:
            raw = raw.split(" -> ", 1)[1]
        if raw:
            paths.append(raw.strip().strip('"'))
    return sorted(set(paths))


def tracked_files(repo_root: Path) -> list[str]:
    return [line for line in run_git(repo_root, "ls-files").splitlines() if line]


def export_files(repo_root: Path) -> list[str]:
    paths: list[str] = []
    for path in repo_root.rglob("*"):
        relative = path.relative_to(repo_root)
        if ".git" in relative.parts:
            continue
        paths.append(relative.as_posix())
    return sorted(paths)


def match_forbidden(path: str) -> str | None:
    if path.startswith("docs/audit/defects/") and not path.endswith(".md"):
        return "docs/audit/defects/<non-md>"
    parts = Path(path).parts
    for part in parts:
        if part in FORBIDDEN_DIRS:
            return part
        for pattern in FORBIDDEN_DIR_GLOBS:
            if fnmatch.fnmatch(part, pattern):
                return pattern
    name = parts[-1] if parts else path
    if name in FORBIDDEN_FILE_NAMES:
        return name
    for pattern in FORBIDDEN_FILE_GLOBS:
        if fnmatch.fnmatch(name, pattern):
            return pattern
    return None


def is_binary_file(path: Path) -> bool:
    data = path.read_bytes()[:8192]
    if not data:
        return False
    if b"\x00" in data:
        return True
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return True
    return False


def path_violations(repo_root: Path, files: list[str]) -> list[str]:
    problems: list[str] = []
    for rel in files:
        if not (repo_root / rel).exists():
            continue
        pattern = match_forbidden(rel)
        if pattern is not None:
            problems.append(f"{rel}: matches forbidden pattern {pattern}")
        lowered = rel.lower()
        for term in FORBIDDEN_CONTRACT_TERMS:
            if term in lowered:
                problems.append(f"{rel}: contains forbidden non-native contract term in path")
    return problems


def decode_text(data: bytes) -> str | None:
    if b"\x00" in data:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def staged_file_text(repo_root: Path, rel: str) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(repo_root), "show", f":{rel}"],
        text=False,
        capture_output=True,
    )
    if result.returncode != 0:
        return None
    return decode_text(result.stdout)


def working_file_text(repo_root: Path, rel: str) -> str | None:
    path = repo_root / rel
    if not path.is_file():
        return None
    return decode_text(path.read_bytes())


def contract_reference_violations(repo_root: Path, files: list[str], source: str) -> list[str]:
    problems: list[str] = []
    for rel in files:
        if rel == POLICY_PATH:
            continue
        text = staged_file_text(repo_root, rel) if source == "staged" else working_file_text(repo_root, rel)
        if text is None:
            continue
        lowered = text.lower()
        for term in FORBIDDEN_CONTRACT_TERMS:
            if term in lowered:
                problems.append(f"{rel}: contains forbidden non-native contract text reference")
    return problems


def size_violations(repo_root: Path, files: list[str], max_bytes: int) -> list[str]:
    problems: list[str] = []
    for rel in files:
        path = repo_root / rel
        if not path.is_file():
            continue
        size = path.stat().st_size
        if size > max_bytes:
            problems.append(f"{rel}: file size {size} bytes exceeds soft limit {max_bytes} bytes")
    return problems


def binary_violations(repo_root: Path, files: list[str]) -> list[str]:
    problems: list[str] = []
    for rel in files:
        path = repo_root / rel
        if not path.is_file():
            continue
        if path.suffix.lower() in TEXT_EXTENSIONS:
            continue
        if is_binary_file(path):
            problems.append(f"{rel}: binary-looking file detected")
    return problems


def history_violations(repo_root: Path, rev_range: str, max_bytes: int) -> list[str]:
    rev_list = subprocess.run(
        ["git", "-C", str(repo_root), "rev-list", "--objects", rev_range],
        check=True,
        text=True,
        capture_output=True,
    )
    if not rev_list.stdout.strip():
        return []
    batch = subprocess.run(
        [
            "git",
            "-C",
            str(repo_root),
            "cat-file",
            "--batch-check=%(objecttype) %(objectname) %(objectsize) %(rest)",
        ],
        input=rev_list.stdout,
        check=True,
        text=True,
        capture_output=True,
    )
    problems: list[str] = []
    object_ids_by_path: dict[str, str] = {}
    for line in batch.stdout.splitlines():
        parts = line.split(" ", 3)
        if len(parts) != 4:
            continue
        object_type, oid, object_size, path = parts
        if object_type != "blob" or not path:
            continue
        object_ids_by_path[path] = oid
        pattern = match_forbidden(path)
        if pattern is not None:
            problems.append(f"{path}: appears in pushed history range {rev_range} and matches forbidden pattern {pattern}")
        lowered = path.lower()
        for term in FORBIDDEN_CONTRACT_TERMS:
            if term in lowered:
                problems.append(f"{path}: appears in pushed history range {rev_range} with forbidden non-native contract term in path")
        try:
            size = int(object_size)
        except ValueError:
            continue
        if size > max_bytes:
            problems.append(f"{path}: blob size {size} bytes in pushed history range {rev_range} exceeds soft limit {max_bytes} bytes")
    for path, oid in object_ids_by_path.items():
        if path == POLICY_PATH:
            continue
        blob = subprocess.run(
            ["git", "-C", str(repo_root), "cat-file", "blob", oid],
            text=False,
            capture_output=True,
        )
        if blob.returncode != 0:
            continue
        text = decode_text(blob.stdout)
        if text is None:
            continue
        lowered = text.lower()
        for term in FORBIDDEN_CONTRACT_TERMS:
            if term in lowered:
                problems.append(f"{path}: appears in pushed history range {rev_range} with forbidden non-native contract text reference")
    return sorted(set(problems))


def gitignore_violations(repo_root: Path) -> list[str]:
    gitignore = repo_root / ".gitignore"
    if not gitignore.exists():
        return [".gitignore is missing"]
    patterns = {
        line.strip()
        for line in gitignore.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    return [f".gitignore must include: {required}" for required in REQUIRED_GITIGNORE_PATTERNS if required not in patterns]


def doc_reference_violations(repo_root: Path) -> list[str]:
    problems: list[str] = []
    for relative_path, needles in REQUIRED_DOC_REFERENCES.items():
        path = repo_root / relative_path
        if not path.exists():
            problems.append(f"required documentation file is missing: {relative_path.as_posix()}")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                problems.append(f"{relative_path.as_posix()} must document {needle}")
    return problems


def check_export(repo_root: Path, *, check_docs: bool = True) -> list[str]:
    files = export_files(repo_root)
    problems = path_violations(repo_root, files)
    problems.extend(gitignore_violations(repo_root))
    if check_docs:
        problems.extend(doc_reference_violations(repo_root))
    return problems


def upstream_range(repo_root: Path) -> str:
    probe = subprocess.run(
        ["git", "-C", str(repo_root), "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"],
        text=True,
        capture_output=True,
    )
    return "@{u}..HEAD" if probe.returncode == 0 else "HEAD"


def print_report(header: str, problems: list[str]) -> int:
    if not problems:
        print(f"[repo-hygiene] {header}: OK")
        return 0
    print(f"[repo-hygiene] {header}: FAILED", file=sys.stderr)
    for problem in problems:
        print(f"  - {problem}", file=sys.stderr)
    return 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="pafio repository hygiene gate")
    parser.add_argument("--mode", choices=("worktree", "staged", "tracked", "push", "export"), default="staged")
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT, help="repository root or exported delivery tree to scan")
    parser.add_argument("--range", dest="rev_range")
    parser.add_argument("--max-file-bytes", type=int, default=DEFAULT_MAX_FILE_BYTES)
    parser.add_argument("--skip-doc-check", action="store_true", help="skip checks that gate commands are documented in operations/governance docs")
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()

    if args.mode == "export":
        problems = check_export(repo_root, check_docs=not args.skip_doc_check)
        return print_report(f"export {repo_root}", sorted(set(problems)))

    if args.mode == "push":
        rev_range = args.rev_range or upstream_range(repo_root)
        problems = history_violations(repo_root, rev_range, args.max_file_bytes)
        problems.extend(gitignore_violations(repo_root))
        if not args.skip_doc_check:
            problems.extend(doc_reference_violations(repo_root))
        return print_report(f"push range {rev_range}", sorted(set(problems)))

    if args.mode == "staged":
        files = staged_files(repo_root)
    elif args.mode == "worktree":
        files = worktree_files(repo_root)
    else:
        files = tracked_files(repo_root)
    if not files:
        print(f"[repo-hygiene] {args.mode}: nothing to check")
        return 0
    problems = []
    problems.extend(path_violations(repo_root, files))
    problems.extend(size_violations(repo_root, files, args.max_file_bytes))
    problems.extend(binary_violations(repo_root, files))
    problems.extend(contract_reference_violations(repo_root, files, args.mode))
    problems.extend(gitignore_violations(repo_root))
    if not args.skip_doc_check:
        problems.extend(doc_reference_violations(repo_root))
    return print_report(args.mode, sorted(set(problems)))


if __name__ == "__main__":
    raise SystemExit(main())
