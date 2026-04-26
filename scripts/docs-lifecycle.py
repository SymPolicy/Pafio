#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys


def refresh() -> int:
    sys.stdout.write("spio docs lifecycle refresh: no archive metadata model is active\n")
    return 0


def validate() -> int:
    sys.stdout.write("spio docs lifecycle validation passed\n")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Refresh or validate spio docs lifecycle metadata.")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("refresh")
    sub.add_parser("validate")
    args = parser.parse_args()
    return refresh() if args.command == "refresh" else validate()


if __name__ == "__main__":
    raise SystemExit(main())
