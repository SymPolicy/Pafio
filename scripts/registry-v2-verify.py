#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from spio_registry_v2 import verify_registry_root  # noqa: E402
from spio_registry_v2.common import RegistryV2Error  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify the trust metadata, append-only index, and artifacts inside a spio registry v2 root."
    )
    parser.add_argument(
        "--root",
        required=True,
        help="Path to the registry v2 root that should be verified.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        payload = verify_registry_root(args.root)
    except RegistryV2Error as err:
        print(str(err), file=sys.stderr)
        return 1
    print(json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
