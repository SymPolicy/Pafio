#!/usr/bin/env python3
"""Repository-local exact-match secret scan.

Detects committed credential material by token shape only. Reports the rule
label and file:line of each hit; never echoes the matched text. Broader
privacy review is owned by the local GitVault reviewer, not this gate.

Modes:
  (default)      scan every tracked file in the worktree
  --staged       scan added lines of the staged diff (pre-commit)
  --range A..B   scan added lines of a revision range (delivery gate)
"""

import re
import subprocess
import sys

EDGE = r"(?:^|[^A-Za-z0-9_-])"
RULES = [
    ("private-key-block", r"-----BEGIN ([A-Z0-9]+ )?PRIVATE KEY-----"),
    ("aws-access-key", r"(AKIA|ASIA)[A-Z0-9]{16}"),
    ("github-token", r"(gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{22,})"),
    ("anthropic-key", r"sk-ant-[A-Za-z0-9_-]{20,}"),
    ("google-api-key", r"AIza[A-Za-z0-9_-]{35}"),
    ("openai-key", r"sk-[A-Za-z0-9_-]{20,}"),
    ("slack-token", r"xox[baprs]-[A-Za-z0-9-]{10,}"),
    ("stripe-key", r"(sk|rk)_(live|test)_[A-Za-z0-9]{20,}"),
    ("json-web-token", r"eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}"),
]
PATTERNS = [(name, re.compile(EDGE + pattern)) for name, pattern in RULES]


def git(*args: str) -> bytes:
    return subprocess.run(
        ["git", *args], check=True, stdout=subprocess.PIPE
    ).stdout


def scan_lines(lines, path_of, hits):
    for lineno, raw in enumerate(lines, 1):
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            continue
        for name, pattern in PATTERNS:
            if pattern.search(text):
                hits.append(f"{path_of}:{lineno} {name}")
                break


def worktree_hits():
    hits = []
    for path in git("ls-files", "-z").split(b"\0"):
        if not path:
            continue
        rel = path.decode("utf-8", "surrogateescape")
        try:
            with open(rel, "rb") as fh:
                blob = fh.read()
        except OSError:
            continue
        scan_lines(blob.splitlines(), rel, hits)
    return hits


def diff_hits(*diff_args):
    hits = []
    path = "<unknown>"
    lineno = 0
    for raw in git("diff", "--unified=0", *diff_args).splitlines():
        if raw.startswith(b"+++"):
            path = raw[6:].decode("utf-8", "surrogateescape")
        elif raw.startswith(b"@@"):
            m = re.search(rb"\+(\d+)", raw)
            lineno = int(m.group(1)) - 1 if m else 0
        elif raw.startswith(b"+") and not raw.startswith(b"+++"):
            lineno += 1
            scan_lines([raw[1:]], path, hits)
        elif raw.startswith(b"-") or raw.startswith(b" "):
            lineno += 1
    return hits


def main():
    args = sys.argv[1:]
    if args[:1] == ["--staged"]:
        hits = diff_hits("--cached")
    elif args[:1] == ["--range"] and len(args) == 2:
        hits = diff_hits(args[1])
    elif not args:
        hits = worktree_hits()
    else:
        sys.exit(__doc__)
    for hit in hits:
        print(hit)
    if hits:
        print(f"secret-scan: {len(hits)} exact-match credential hit(s)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
