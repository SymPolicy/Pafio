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

from pafio_registry_v2 import generate_key_directory  # noqa: E402
from pafio_registry_v2.common import RegistryV2Error  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate the Ed25519 signing keys used by the pafio registry v2 metadata roles."
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Directory where keys.json, private/*.pem, and public/*.pem will be written.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite an existing key directory if role keys are already present.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        payload = generate_key_directory(Path(args.output_dir), force=args.force)
    except RegistryV2Error as err:
        print(str(err), file=sys.stderr)
        return 1
    print(json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
