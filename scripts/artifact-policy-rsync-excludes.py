#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import sys


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from artifact_policy import load_policy


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="artifact-policy-rsync-excludes.py",
        description="Emit rsync --exclude patterns from artifact-policy.json",
    )
    parser.add_argument("--policy", type=pathlib.Path, default=None, help="override policy JSON path")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    policy = load_policy(args.policy)
    for pattern in policy.export_excludes:
        sys.stdout.write(pattern + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
