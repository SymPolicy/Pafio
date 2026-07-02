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

from pafio_registry_v2 import publish_to_registry_v2  # noqa: E402
from pafio_registry_v2.common import RegistryV2Error  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Publish a source package into the pafio registry v2 static read plane by updating append-only "
            "indexes, trust metadata, and the transparency log."
        )
    )
    parser.add_argument("--root", required=True, help="Destination local directory or file:// root for the registry v2 static read plane.")
    parser.add_argument("--key-dir", required=True, help="Directory containing keys.json and role signing keys.")
    parser.add_argument("--archive-path", help="Prebuilt source package archive to publish.")
    parser.add_argument("--manifest-path", help="Manifest path used to prepare a dry-run publish candidate through pafio.")
    parser.add_argument("--pafio-bin", help="pafio executable used when --manifest-path is provided. Defaults to scripts/pafio.")
    parser.add_argument("--package", help="Optional package name forwarded to pafio publish --dry-run for workspace selection.")
    parser.add_argument("--output", help="Optional output archive path forwarded to pafio publish --dry-run.")
    parser.add_argument("--registry-name", default="pafio-registry-v2", help="Registry name written during first-root initialization.")
    parser.add_argument("--publisher-id", default="local-publisher", help="Publisher identifier recorded in the release record.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        payload = publish_to_registry_v2(
            args.root,
            args.key_dir,
            archive_path_value=args.archive_path,
            manifest_path_value=args.manifest_path,
            pafio_bin_value=args.pafio_bin,
            package_name=args.package,
            output_path_value=args.output,
            registry_name=args.registry_name,
            publisher_id=args.publisher_id,
        )
    except RegistryV2Error as err:
        print(str(err), file=sys.stderr)
        return 1
    print(json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
