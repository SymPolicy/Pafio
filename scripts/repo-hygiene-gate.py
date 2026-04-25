#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MAX_FILE_BYTES = 20 * 1024 * 1024
TEXT_EXTENSIONS = {".cmake", ".cpp", ".h", ".hpp", ".json", ".md", ".py", ".sh", ".toml", ".txt", ".xml", ".yaml", ".yml"}
FORBIDDEN_GLOBS = [
    ".DS_Store",
    ".cursor/*",
    ".idea/*",
    ".vscode/*",
    "__pycache__/*",
    "*.a",
    "*.dll",
    "*.dylib",
    "*.exe",
    "*.log",
    "*.obj",
    "*.o",
    "*.so",
    "*.tmp",
    "CMakeCache.txt",
    "CMakeFiles/*",
    "Makefile",
    "Testing/*",
    "build",
    "build/*",
    "build-*",
    "build-*/*",
    "dist",
    "dist/*",
    ".spio",
    ".spio/*",
    "docs/audit/defects/*",
]
REQUIRED_GITIGNORE_PATTERNS = [
    ".DS_Store",
    ".cursor/",
    ".idea/",
    ".vscode/",
    ".cache/",
    "__pycache__/",
    ".pytest_cache/",
    ".mypy_cache/",
    ".ruff_cache/",
    ".venv/",
    "venv/",
    "node_modules/",
    "build/",
    "build-*/",
    "tmp/",
    "*.tmp",
    "*.log",
    "docs/audit/defects/",
    "!docs/**/build/",
    "!docs/**/build/**",
    "!docs/**/build-*/",
    "!docs/**/build-*/**",
    "!docs/**/tmp/",
    "!docs/**/tmp/**",
    "!docs/**/*.tmp",
    "!docs/**/*.log",
    "!tests/**/build/",
    "!tests/**/build/**",
    "!tests/**/build-*/",
    "!tests/**/build-*/**",
    "!tests/**/tmp/",
    "!tests/**/tmp/**",
    "!tests/**/*.tmp",
    "!tests/**/*.log",
]
REQUIRED_DOC_REFERENCES = {
    Path("docs/README.md"): [
        "scripts/docs-index.py",
        "scripts/docs-lifecycle.py",
        "scripts/docs-audit.py",
    ],
    Path("docs/assets/workflow/REPO-HYGIENE.md"): [
        "scripts/repo-hygiene-gate.py",
        "scripts/delivery-gate.sh",
    ],
}
FORBIDDEN_CONTRACT_TERMS = [
    "open" + "api",
    "ara" + "zzo",
    "redo" + "cly",
]


def run_git(*args: str, check: bool = True) -> str:
    result = subprocess.run(["git", "-C", str(REPO_ROOT), *args], check=check, text=True, capture_output=True)
    return result.stdout


def staged_files() -> list[str]:
    return [line for line in run_git("diff", "--cached", "--name-only", "--diff-filter=ACMR").splitlines() if line]


def tracked_files() -> list[str]:
    return [line for line in run_git("ls-files").splitlines() if line]


def match_forbidden(path: str) -> str | None:
    for pattern in FORBIDDEN_GLOBS:
        if fnmatch.fnmatch(path, pattern):
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


def path_violations(files: list[str]) -> list[str]:
    problems: list[str] = []
    for rel in files:
        if not (REPO_ROOT / rel).exists():
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


def staged_file_text(rel: str) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "show", f":{rel}"],
        text=False,
        capture_output=True,
    )
    if result.returncode != 0:
        return None
    return decode_text(result.stdout)


def working_file_text(rel: str) -> str | None:
    path = REPO_ROOT / rel
    if not path.is_file():
        return None
    return decode_text(path.read_bytes())


def contract_reference_violations(files: list[str], source: str) -> list[str]:
    problems: list[str] = []
    for rel in files:
        text = staged_file_text(rel) if source == "staged" else working_file_text(rel)
        if text is None:
            continue
        lowered = text.lower()
        for term in FORBIDDEN_CONTRACT_TERMS:
            if term in lowered:
                problems.append(f"{rel}: contains forbidden non-native contract text reference")
    return problems


def size_violations(files: list[str], max_bytes: int) -> list[str]:
    problems: list[str] = []
    for rel in files:
        path = REPO_ROOT / rel
        if not path.is_file():
            continue
        size = path.stat().st_size
        if size > max_bytes:
            problems.append(f"{rel}: file size {size} bytes exceeds soft limit {max_bytes} bytes")
    return problems


def binary_violations(files: list[str]) -> list[str]:
    problems: list[str] = []
    for rel in files:
        path = REPO_ROOT / rel
        if not path.is_file():
            continue
        if path.suffix.lower() in TEXT_EXTENSIONS:
            continue
        if is_binary_file(path):
            problems.append(f"{rel}: binary-looking file detected")
    return problems


def history_violations(rev_range: str, max_bytes: int) -> list[str]:
    rev_list = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-list", "--objects", rev_range],
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
            str(REPO_ROOT),
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
        blob = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "cat-file", "blob", oid],
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


def gitignore_pattern_violations() -> list[str]:
    gitignore = REPO_ROOT / ".gitignore"
    if not gitignore.exists():
        return [".gitignore is missing"]
    patterns = {
        line.strip()
        for line in gitignore.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    return [f".gitignore must include: {required}" for required in REQUIRED_GITIGNORE_PATTERNS if required not in patterns]


def doc_reference_violations() -> list[str]:
    problems: list[str] = []
    for relative_path, needles in REQUIRED_DOC_REFERENCES.items():
        path = REPO_ROOT / relative_path
        if not path.exists():
            problems.append(f"required documentation file is missing: {relative_path.as_posix()}")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                problems.append(f"{relative_path.as_posix()} must document {needle}")
    return problems


def upstream_range() -> str:
    probe = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"],
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


def main() -> int:
    parser = argparse.ArgumentParser(description="spio repository hygiene gate")
    parser.add_argument("--mode", choices=("staged", "tracked", "push"), default="staged")
    parser.add_argument("--range", dest="rev_range")
    parser.add_argument("--max-file-bytes", type=int, default=DEFAULT_MAX_FILE_BYTES)
    args = parser.parse_args()

    if args.mode == "push":
        rev_range = args.rev_range or upstream_range()
        problems = history_violations(rev_range, args.max_file_bytes)
        problems.extend(gitignore_pattern_violations())
        problems.extend(doc_reference_violations())
        return print_report(f"push range {rev_range}", sorted(set(problems)))

    files = staged_files() if args.mode == "staged" else tracked_files()
    if not files:
        print(f"[repo-hygiene] {args.mode}: nothing to check")
        return 0
    problems = []
    problems.extend(path_violations(files))
    problems.extend(size_violations(files, args.max_file_bytes))
    problems.extend(binary_violations(files))
    problems.extend(contract_reference_violations(files, args.mode))
    problems.extend(gitignore_pattern_violations())
    problems.extend(doc_reference_violations())
    return print_report(args.mode, sorted(set(problems)))


if __name__ == "__main__":
    raise SystemExit(main())
