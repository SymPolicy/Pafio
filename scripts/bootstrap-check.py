#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "contracts" / "compile-plan" / "v1" / "compile-plan.schema.json"
CLI = ROOT / "scripts" / "spio"


def main() -> int:
    json.loads(SCHEMA.read_text())

    proc = subprocess.run(
        [str(CLI), "machine-info", "--json"],
        check=True,
        capture_output=True,
        text=True,
    )
    json.loads(proc.stdout)

    suite = unittest.defaultTestLoader.discover(
        str(ROOT / "tests" / "unit"),
        pattern="test_*.py",
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
