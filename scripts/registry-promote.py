#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from spio_registry_v2 import verify_registry_root  # noqa: E402
from spio_registry_v2.common import (  # noqa: E402
    RegistryV2Error,
    copy_if_missing_or_same,
    load_json_file,
    normalize_local_root,
    require_object,
    require_string,
)


def _scope_records(source_root: pathlib.Path, package: str | None, version: str | None) -> list[str]:
    packages: list[str] = []
    index_root = source_root / "index"
    if not index_root.exists():
        raise RegistryV2Error(f"registry v2 index root not found: {index_root}")

    if package is not None:
        namespace, short_name = package.split("/", 1)
        index_paths = [index_root / namespace / f"{short_name}.jsonl"]
    else:
        index_paths = sorted(path for path in index_root.rglob("*.jsonl") if path.is_file())

    for index_path in index_paths:
        if not index_path.exists():
            raise RegistryV2Error(f"registry v2 index file not found: {index_path}")
        lines = [line for line in index_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        for line in lines:
            record = require_object(json.loads(line), f"registry v2 index record in {index_path}")
            record_package = require_string(record.get("package"), f"registry v2 package in {index_path}")
            record_version = require_string(record.get("version"), f"registry v2 version in {index_path}")
            if package is not None and record_package != package:
                continue
            if version is not None and record_version != version:
                continue
            packages.append(f"{record_package}@{record_version}")

    if not packages:
        if package is not None and version is not None:
            raise RegistryV2Error(f"registry v2 source root does not contain {package}@{version}")
        if package is not None:
            raise RegistryV2Error(f"registry v2 source root does not contain package {package}")
        raise RegistryV2Error("registry v2 source root does not contain any releases")
    return packages


def _mirror_registry_root(source_root: pathlib.Path, dest_root: pathlib.Path) -> dict[str, Any]:
    summary = {
        "files_total": 0,
        "files_copied": 0,
        "files_reused": 0,
        "metadata_files": 0,
        "index_files": 0,
        "artifact_files": 0,
        "log_files": 0,
    }

    for source_path in sorted(path for path in source_root.rglob("*") if path.is_file()):
        relative = source_path.relative_to(source_root)
        destination = dest_root / relative
        action = copy_if_missing_or_same(source_path, destination)
        summary["files_total"] += 1
        summary[f"files_{action}"] += 1

        relative_text = relative.as_posix()
        if relative_text.startswith("trust/") or relative_text == "config.json":
            summary["metadata_files"] += 1
        elif relative_text.startswith("index/"):
            summary["index_files"] += 1
        elif relative_text.startswith("artifacts/"):
            summary["artifact_files"] += 1
        elif relative_text.startswith("log/"):
            summary["log_files"] += 1

    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="registry-promote.py")
    parser.add_argument("--source-root", required=True, help="Writable source registry v2 root as a local path or file:// URL")
    parser.add_argument("--dest-root", required=True, help="Read-side destination registry v2 root as a local path or file:// URL")
    parser.add_argument("--package", help="Optional namespace/name release scope asserted to exist before promotion")
    parser.add_argument("--version", help="Optional version asserted together with --package before promotion")
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    args = parser.parse_args(argv)

    if args.version and not args.package:
        parser.error("--version requires --package")

    try:
        source_root = normalize_local_root(args.source_root)
        dest_root = normalize_local_root(args.dest_root)
        verify_registry_root(str(source_root))
        packages = _scope_records(source_root, args.package, args.version)
        dest_root.mkdir(parents=True, exist_ok=True)
        mirror = _mirror_registry_root(source_root, dest_root)
        verified = verify_registry_root(str(dest_root))
        summary = {
            "ok": True,
            "source_root": str(source_root),
            "dest_root": str(dest_root),
            "scope": {
                "package": args.package,
                "version": args.version,
            },
            "packages": packages,
            **mirror,
            "verified": verified,
        }
    except RegistryV2Error as err:
        summary = {
            "ok": False,
            "error": str(err),
        }

    if args.json:
        sys.stdout.write(json.dumps(summary, sort_keys=True) + "\n")
    else:
        if summary["ok"]:
            sys.stdout.write(
                "registry v2 promotion succeeded: "
                f"{summary['files_total']} files mirrored for {len(summary['packages'])} release(s)\n"
            )
        else:
            sys.stderr.write(f"registry v2 promotion failed: {summary['error']}\n")

    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
