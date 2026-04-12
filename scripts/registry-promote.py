#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import sys
from urllib.parse import unquote, urlsplit


MARKER_NAME = "spio-registry.json"


def normalize_local_root(value: str) -> pathlib.Path:
    parsed = urlsplit(value)
    if parsed.scheme and parsed.scheme != "file":
        raise RuntimeError("registry promotion supports only local paths or file:// roots")
    if parsed.scheme == "file":
        path = pathlib.Path(unquote(parsed.path))
    else:
        path = pathlib.Path(value)
    return path.resolve()


def load_json_file(path: pathlib.Path, context: str) -> dict:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as err:
        raise RuntimeError(f"{context} is not valid JSON: {path}") from err
    if not isinstance(payload, dict):
        raise RuntimeError(f"{context} must be a JSON object: {path}")
    return payload


def validate_marker(path: pathlib.Path, context: str) -> dict:
    payload = load_json_file(path, context)
    if payload.get("kind") != "filesystem-local" or payload.get("schema_version") != 1:
        raise RuntimeError(f"{context} does not match the current registry marker contract: {path}")
    return payload


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def ensure_same_or_copy(source: pathlib.Path, destination: pathlib.Path) -> str:
    if destination.exists():
        if not destination.is_file():
            raise RuntimeError(f"destination path is not a regular file: {destination}")
        if file_sha256(source) != file_sha256(destination):
            raise RuntimeError(f"immutable destination object differs from source: {destination}")
        return "reused"

    destination.parent.mkdir(parents=True, exist_ok=True)
    temp_path = destination.parent / (destination.name + ".tmp")
    shutil.copyfile(source, temp_path)
    temp_path.replace(destination)
    return "copied"


def split_package_name(package_name: str) -> tuple[str, str]:
    parts = package_name.split("/", 1)
    if len(parts) != 2 or not parts[0] or not parts[1]:
        raise RuntimeError(f"package name must match namespace/name: {package_name}")
    return parts[0], parts[1]


def iter_entry_paths(source_root: pathlib.Path, package: str | None, version: str | None) -> list[pathlib.Path]:
    index_root = source_root / "index"
    if package is None:
        return sorted(path for path in index_root.rglob("*.json") if path.is_file())

    namespace_name, short_name = split_package_name(package)
    package_root = index_root / namespace_name / short_name
    if version is not None:
        entry_path = package_root / f"{version}.json"
        return [entry_path]
    if not package_root.exists():
        return []
    return sorted(path for path in package_root.glob("*.json") if path.is_file())


def validate_blob_path(source_root: pathlib.Path, blob_path: str) -> pathlib.Path:
    if not blob_path or pathlib.PurePosixPath(blob_path).is_absolute():
        raise RuntimeError(f"registry entry blob_path must be a non-empty relative path: {blob_path}")
    resolved = (source_root / pathlib.PurePosixPath(blob_path)).resolve()
    try:
        resolved.relative_to(source_root)
    except ValueError as err:
        raise RuntimeError(f"registry entry blob_path escapes source root: {blob_path}") from err
    return resolved


def promote_entries(
    source_root: pathlib.Path,
    destination_root: pathlib.Path,
    entry_paths: list[pathlib.Path],
) -> dict:
    result = {
        "entries_total": 0,
        "entries_copied": 0,
        "entries_reused": 0,
        "blobs_total": 0,
        "blobs_copied": 0,
        "blobs_reused": 0,
        "packages": [],
    }

    for entry_path in entry_paths:
        if not entry_path.exists():
            raise RuntimeError(f"registry version entry not found: {entry_path}")
        entry = load_json_file(entry_path, "registry version entry")
        package_name = entry.get("package")
        version = entry.get("version")
        blob_path = entry.get("blob_path")
        sha256 = entry.get("sha256")

        if not isinstance(package_name, str) or not package_name:
            raise RuntimeError(f"registry version entry is missing package: {entry_path}")
        if not isinstance(version, str) or not version:
            raise RuntimeError(f"registry version entry is missing version: {entry_path}")
        if not isinstance(blob_path, str):
            raise RuntimeError(f"registry version entry is missing blob_path: {entry_path}")
        if not isinstance(sha256, str) or len(sha256) != 64:
            raise RuntimeError(f"registry version entry is missing a valid sha256: {entry_path}")

        source_blob = validate_blob_path(source_root, blob_path)
        if not source_blob.exists():
            raise RuntimeError(f"registry blob not found for version entry: {source_blob}")

        destination_blob = destination_root / pathlib.PurePosixPath(blob_path)
        blob_action = ensure_same_or_copy(source_blob, destination_blob)

        relative_entry_path = entry_path.relative_to(source_root)
        destination_entry = destination_root / relative_entry_path
        entry_action = ensure_same_or_copy(entry_path, destination_entry)

        result["entries_total"] += 1
        result["blobs_total"] += 1
        result[f"entries_{entry_action}"] += 1
        result[f"blobs_{blob_action}"] += 1
        result["packages"].append(f"{package_name}@{version}")

    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="registry-promote.py")
    parser.add_argument("--source-root", required=True, help="writable source registry root as a local path or file:// URL")
    parser.add_argument("--dest-root", required=True, help="read-side destination registry root as a local path or file:// URL")
    parser.add_argument("--package", help="optional package name namespace/name to promote")
    parser.add_argument("--version", help="optional single version to promote; requires --package")
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    args = parser.parse_args(argv)

    if args.version and not args.package:
        parser.error("--version requires --package")

    try:
        source_root = normalize_local_root(args.source_root)
        destination_root = normalize_local_root(args.dest_root)
        source_marker = source_root / MARKER_NAME
        if not source_marker.exists():
            raise RuntimeError(f"source registry marker not found: {source_marker}")
        validate_marker(source_marker, "source registry marker")

        entry_paths = iter_entry_paths(source_root, args.package, args.version)
        if not entry_paths:
            raise RuntimeError("no registry version entries matched the requested promotion scope")

        destination_root.mkdir(parents=True, exist_ok=True)
        marker_action = ensure_same_or_copy(source_marker, destination_root / MARKER_NAME)
        validate_marker(destination_root / MARKER_NAME, "destination registry marker")
        result = promote_entries(source_root, destination_root, entry_paths)

        summary = {
            "ok": True,
            "source_root": str(source_root),
            "dest_root": str(destination_root),
            "scope": {
                "package": args.package,
                "version": args.version,
            },
            "marker_action": marker_action,
            **result,
        }
    except RuntimeError as err:
        summary = {
            "ok": False,
            "error": str(err),
        }

    if args.json:
        sys.stdout.write(json.dumps(summary, sort_keys=True) + "\n")
    else:
        if summary["ok"]:
            sys.stdout.write(
                "registry promotion succeeded: "
                f"{summary['entries_total']} entries, {summary['blobs_total']} blobs, marker={summary['marker_action']}\n"
            )
        else:
            sys.stderr.write(f"registry promotion failed: {summary['error']}\n")

    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
