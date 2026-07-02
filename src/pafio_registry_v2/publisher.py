from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tarfile
import tomllib
from collections import defaultdict
from typing import Any

from .common import (
    REGISTRY_SUBPROCESS_TIMEOUT_SECONDS,
    RegistryV2Error,
    artifact_bucket_path,
    canonical_json_bytes,
    copy_if_missing_or_same,
    ensure_parent,
    expires_in,
    index_path_for_package,
    load_json_file,
    load_role_keys,
    normalize_local_root,
    normalize_registry_relative_path,
    parse_semver_key,
    require_object,
    require_sha256_digest,
    require_string,
    sha256_bytes,
    sha256_file,
    sign_payload,
    signed_file_meta,
    split_package_name,
    utc_now,
    validate_package_name,
    write_json_file,
)


MAX_ARCHIVE_MANIFEST_BYTES = 1024 * 1024


def _prescan_source_archive(archive_path: pathlib.Path) -> None:
    with tarfile.open(archive_path, mode="r:*") as archive:
        for member in archive.getmembers():
            normalized = member.name.replace("\\", "/")
            if member.isdir():
                normalized = normalized.rstrip("/")
            normalize_registry_relative_path(normalized, "source artifact archive member path")
            if not (member.isfile() or member.isdir()):
                raise RegistryV2Error(
                    f"source artifact archive may contain only regular files and directories: {member.name!r}"
                )


def _find_manifest_bytes(archive_path: pathlib.Path) -> bytes:
    with tarfile.open(archive_path, mode="r:*") as archive:
        candidates: list[tuple[str, bytes]] = []
        for member in archive.getmembers():
            if not member.isfile():
                continue
            normalized = normalize_registry_relative_path(
                member.name.replace("\\", "/"),
                "source artifact archive member path",
            )
            if normalized == "pafio.toml" or normalized.endswith("/pafio.toml"):
                if member.size > MAX_ARCHIVE_MANIFEST_BYTES:
                    raise RegistryV2Error(
                        f"source artifact manifest candidate exceeds {MAX_ARCHIVE_MANIFEST_BYTES} bytes: {member.name!r}"
                    )
                handle = archive.extractfile(member)
                if handle is None:
                    continue
                candidates.append((normalized, handle.read()))
        if not candidates:
            raise RegistryV2Error(f"source artifact does not contain pafio.toml: {archive_path}")
        if len(candidates) != 1:
            raise RegistryV2Error(f"source artifact contains multiple pafio.toml manifests: {archive_path}")
    return candidates[0][1]


def _log_root_hash(leaf_hashes: list[str]) -> str:
    state = bytes(32)
    for leaf_hash in leaf_hashes:
        state = hashlib.sha256(state + bytes.fromhex(leaf_hash)).digest()
    return state.hex()


def _role_keys_dict(role_keys: dict[str, Any]) -> dict[str, Any]:
    return {
        key.keyid: {
            "keytype": "ed25519",
            "scheme": "ed25519",
            "keyval": {
                "public": key.public_key_pem,
            },
        }
        for key in role_keys.values()
    }


def _roles_policy(role_keys: dict[str, Any]) -> dict[str, Any]:
    return {
        "root": {"keyids": [role_keys["root"].keyid], "threshold": 1},
        "timestamp": {"keyids": [role_keys["timestamp"].keyid], "threshold": 1},
        "snapshot": {"keyids": [role_keys["snapshot"].keyid], "threshold": 1},
        "targets": {"keyids": [role_keys["targets"].keyid], "threshold": 1},
        "log": {"keyids": [role_keys["log"].keyid], "threshold": 1},
    }


def _registry_config(registry_name: str, generated_at: str) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "protocol": "pafio-static-registry",
        "protocol_version": 2,
        "registry_name": registry_name,
        "generated_at": generated_at,
        "capabilities": {
            "append_only_index": True,
            "source_artifacts": True,
            "binary_artifacts": True,
            "transparency_log": True,
        },
        "paths": {
            "root": "trust/root.json",
            "timestamp": "trust/timestamp.json",
            "snapshot": "trust/snapshot.json",
            "targets_prefix": "trust/targets/",
            "index_prefix": "index/",
            "source_artifact_prefix": "artifacts/source/",
            "binary_artifact_prefix": "artifacts/binary/",
            "transparency_checkpoint": "log/checkpoint.json",
            "transparency_leaves_prefix": "log/leaves/",
        },
    }


def _canonical_archive_path(value: str) -> pathlib.Path:
    path = pathlib.Path(value).expanduser()
    if not path.is_absolute():
        path = pathlib.Path.cwd() / path
    return path.resolve()


def _prepare_publish_candidate(
    pafio_bin: pathlib.Path,
    manifest_path: pathlib.Path,
    package_name: str | None,
    output_path: pathlib.Path | None,
) -> dict[str, Any]:
    command = [
        str(pafio_bin),
        "--json",
        "publish",
        "--dry-run",
        "--manifest-path",
        str(manifest_path),
    ]
    if package_name is not None:
        command.extend(["--package", package_name])
    if output_path is not None:
        command.extend(["--output", str(output_path)])
    try:
        proc = subprocess.run(
            command,
            capture_output=True,
            text=True,
            check=False,
            timeout=REGISTRY_SUBPROCESS_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as err:
        raise RegistryV2Error(
            f"pafio publish --dry-run timed out after {REGISTRY_SUBPROCESS_TIMEOUT_SECONDS:g}s"
        ) from err
    if proc.returncode != 0:
        stderr = proc.stderr.strip() or proc.stdout.strip()
        raise RegistryV2Error(f"pafio publish --dry-run failed: {stderr}")
    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError as err:
        raise RegistryV2Error("pafio publish --dry-run did not emit valid JSON") from err
    if not isinstance(payload, dict):
        raise RegistryV2Error("pafio publish --dry-run must emit a top-level JSON object")
    if payload.get("command") != "publish" or payload.get("mode") != "dry-run":
        raise RegistryV2Error("pafio publish --dry-run emitted an unexpected payload shape")
    return payload


def _normalize_dependency(alias: str, spec: Any, section_name: str) -> dict[str, Any]:
    if not isinstance(spec, dict):
        raise RegistryV2Error(f"dependency '{alias}' in [{section_name}] must be an inline table")
    package = require_string(spec.get("package"), f"dependency '{alias}' package in [{section_name}]")
    validate_package_name(package, f"dependency '{alias}' package in [{section_name}]")
    version = require_string(spec.get("version"), f"dependency '{alias}' version in [{section_name}]")
    registry = require_string(spec.get("registry"), f"dependency '{alias}' registry in [{section_name}]")
    return {
        "alias": alias,
        "package": package,
        "version_req": version,
        "registry": registry,
        "kind": "runtime" if section_name == "dependencies" else "development",
        "optional": False,
        "target_condition": "",
        "features": [],
    }


def _record_dependencies(manifest_doc: dict[str, Any], section_name: str) -> list[dict[str, Any]]:
    section = manifest_doc.get(section_name, {})
    if section is None:
        return []
    if not isinstance(section, dict):
        raise RegistryV2Error(f"[{section_name}] must be a table inside the source package manifest")
    return [_normalize_dependency(alias, spec, section_name) for alias, spec in sorted(section.items())]


def _extract_record_from_archive(
    archive_path: pathlib.Path,
    *,
    publisher_id: str,
    published_at: str,
) -> tuple[dict[str, Any], bytes]:
    _prescan_source_archive(archive_path)
    manifest_bytes = _find_manifest_bytes(archive_path)
    try:
        manifest_doc = tomllib.loads(manifest_bytes.decode("utf-8"))
    except UnicodeDecodeError as err:
        raise RegistryV2Error(f"source package manifest is not UTF-8: {archive_path}") from err
    except tomllib.TOMLDecodeError as err:
        raise RegistryV2Error(f"source package manifest is not valid TOML: {archive_path}") from err

    package_table = require_object(manifest_doc.get("package"), "source package manifest [package]")
    package_name = require_string(package_table.get("name"), "source package manifest package.name")
    validate_package_name(package_name, "source package manifest package.name")
    package_version = require_string(package_table.get("version"), "source package manifest package.version")
    dependencies = _record_dependencies(manifest_doc, "dependencies")
    dev_dependencies = _record_dependencies(manifest_doc, "dev-dependencies")
    artifact_sha256 = sha256_file(archive_path)
    metadata_source = {
        "package": package_name,
        "version": package_version,
        "publisher_id": publisher_id,
        "published_at": published_at,
        "archive_sha256": artifact_sha256,
        "dependencies": dependencies,
        "dev_dependencies": dev_dependencies,
    }
    record = {
        "schema_version": 1,
        "package": package_name,
        "version": package_version,
        "release_revision": 1,
        "published_at": published_at,
        "publisher_id": publisher_id,
        "yanked": False,
        "deprecated_message": "",
        "source_artifact": {
            "sha256": artifact_sha256,
            "size_bytes": archive_path.stat().st_size,
            "path": artifact_bucket_path("artifacts/source/sha256", artifact_sha256, ".pafio.src.tar").as_posix(),
            "archive_format": "tar",
            "compression": "none",
        },
        "binary_artifacts": [],
        "dependencies": dependencies,
        "dev_dependencies": dev_dependencies,
        "features": {
            "default": [],
            "optional": [],
        },
        "manifest_digest": sha256_bytes(manifest_bytes),
        "metadata_digest": sha256_bytes(canonical_json_bytes(metadata_source)),
    }
    return record, manifest_bytes


def _read_signed_version(path: pathlib.Path) -> int:
    if not path.exists():
        return 0
    payload = load_json_file(path, f"signed metadata {path.name}")
    signed = require_object(payload.get("signed"), f"signed metadata {path.name}.signed")
    version = signed.get("version")
    if not isinstance(version, int):
        raise RegistryV2Error(f"signed metadata version must be an integer: {path}")
    return version


def _leaf_sequence_paths(dest_root: pathlib.Path) -> list[pathlib.Path]:
    leaves_root = dest_root / "log" / "leaves"
    if not leaves_root.exists():
        return []
    paths = sorted(path for path in leaves_root.glob("*.json") if path.is_file())
    for index, path in enumerate(paths, start=1):
        expected = f"{index:012d}.json"
        if path.name != expected:
            raise RegistryV2Error(f"registry v2 leaves must remain contiguous; expected {expected} but found {path.name}")
    return paths


def _ensure_root_matches_keys(dest_root: pathlib.Path, role_keys: dict[str, Any]) -> None:
    root_path = dest_root / "trust" / "root.json"
    if not root_path.exists():
        return
    root_payload = load_json_file(root_path, "registry v2 root metadata")
    signed = require_object(root_payload.get("signed"), "registry v2 root metadata signed payload")
    roles = require_object(signed.get("roles"), "registry v2 root metadata roles")
    for role_name, role_key in role_keys.items():
        if role_name not in roles:
            raise RegistryV2Error(f"registry v2 root metadata is missing role '{role_name}'")
        role_policy = require_object(roles.get(role_name), f"registry v2 root metadata role '{role_name}'")
        keyids = role_policy.get("keyids")
        if keyids != [role_key.keyid]:
            raise RegistryV2Error(
                f"registry v2 root metadata key id for role '{role_name}' does not match the provided key directory"
            )


def _initialize_registry_root(
    dest_root: pathlib.Path,
    role_keys: dict[str, Any],
    *,
    registry_name: str,
    registry_time: str,
) -> bool:
    config_path = dest_root / "config.json"
    root_path = dest_root / "trust" / "root.json"
    if config_path.exists() != root_path.exists():
        raise RegistryV2Error("registry v2 root is only partially initialized; config.json and trust/root.json must either both exist or both be absent")
    if config_path.exists() or root_path.exists():
        _ensure_root_matches_keys(dest_root, role_keys)
        config = load_json_file(config_path, "registry v2 config")
        if config.get("protocol") != "pafio-static-registry" or config.get("protocol_version") != 2:
            raise RegistryV2Error("existing registry root does not match the registry v2 protocol")
        existing_name = require_string(config.get("registry_name"), "registry v2 config registry_name")
        if existing_name != registry_name:
            raise RegistryV2Error(
                f"existing registry v2 root is named '{existing_name}', but the publish request expected '{registry_name}'"
            )
        return False

    dest_root.mkdir(parents=True, exist_ok=True)
    write_json_file(config_path, _registry_config(registry_name, registry_time))

    checkpoint_signed = {
        "type": "checkpoint",
        "spec_version": "1",
        "version": 1,
        "generated_at": registry_time,
        "tree_size": 0,
        "root_hash": _log_root_hash([]),
    }
    checkpoint_envelope = sign_payload(checkpoint_signed, role_keys["log"])
    checkpoint_path = dest_root / "log" / "checkpoint.json"
    write_json_file(checkpoint_path, checkpoint_envelope)

    snapshot_signed = {
        "type": "snapshot",
        "spec_version": "1",
        "version": 1,
        "expires": expires_in(7),
        "meta": {},
        "log_meta": {
            "log/checkpoint.json": signed_file_meta(checkpoint_path, version=1),
        },
    }
    snapshot_envelope = sign_payload(snapshot_signed, role_keys["snapshot"])
    snapshot_path = dest_root / "trust" / "snapshot.json"
    write_json_file(snapshot_path, snapshot_envelope)

    timestamp_signed = {
        "type": "timestamp",
        "spec_version": "1",
        "version": 1,
        "expires": expires_in(1),
        "meta": {
            "trust/snapshot.json": signed_file_meta(snapshot_path, version=1),
        },
    }
    timestamp_envelope = sign_payload(timestamp_signed, role_keys["timestamp"])
    timestamp_path = dest_root / "trust" / "timestamp.json"
    write_json_file(timestamp_path, timestamp_envelope)

    root_signed = {
        "type": "root",
        "spec_version": "1",
        "version": 1,
        "expires": expires_in(365),
        "keys": _role_keys_dict(role_keys),
        "roles": _roles_policy(role_keys),
    }
    root_envelope = sign_payload(root_signed, role_keys["root"])
    write_json_file(root_path, root_envelope)
    return True


def _collect_package_maps(dest_root: pathlib.Path) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    index_root = dest_root / "index"
    namespace_packages: dict[str, dict[str, Any]] = defaultdict(dict)
    snapshot_meta: dict[str, Any] = {}
    if not index_root.exists():
        return namespace_packages, snapshot_meta

    for index_path in sorted(path for path in index_root.rglob("*.jsonl") if path.is_file()):
        lines = [line for line in index_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        if not lines:
            raise RegistryV2Error(f"registry v2 index file is empty: {index_path}")
        records: list[dict[str, Any]] = []
        for line in lines:
            payload = json.loads(line)
            records.append(require_object(payload, f"registry v2 index record in {index_path}"))
        package_name = require_string(records[0].get("package"), f"registry v2 first package name in {index_path}")
        namespace, _ = split_package_name(package_name)
        versions = []
        releases: dict[str, Any] = {}
        for record in records:
            if require_string(record.get("package"), f"registry v2 package name in {index_path}") != package_name:
                raise RegistryV2Error(f"registry v2 index file contains mixed package names: {index_path}")
            version = require_string(record.get("version"), f"registry v2 version in {index_path}")
            source_artifact = require_object(record.get("source_artifact"), f"registry v2 source artifact in {index_path}")
            versions.append(version)
            releases[version] = {
                "release_revision": record.get("release_revision", 1),
                "index_record_sha256": sha256_bytes(canonical_json_bytes(record)),
                "source_artifact_sha256": require_sha256_digest(
                    source_artifact.get("sha256"),
                    f"registry v2 source artifact sha256 in {index_path}",
                ),
                "source_artifact_path": normalize_registry_relative_path(
                    require_string(
                        source_artifact.get("path"),
                        f"registry v2 source artifact path in {index_path}",
                    ),
                    f"registry v2 source artifact path in {index_path}",
                ),
                "binary_artifact_count": len(record.get("binary_artifacts", [])),
            }
        namespace_packages[namespace][package_name] = {
            "index_path": index_path.relative_to(dest_root).as_posix(),
            "latest_version": sorted(versions, key=parse_semver_key)[-1],
            "releases": releases,
        }
        snapshot_meta[index_path.relative_to(dest_root).as_posix()] = signed_file_meta(index_path, version=1)
    return namespace_packages, snapshot_meta


def _refresh_signed_metadata(dest_root: pathlib.Path, role_keys: dict[str, Any], registry_time: str) -> dict[str, int]:
    namespace_packages, snapshot_meta = _collect_package_maps(dest_root)

    for namespace, package_map in namespace_packages.items():
        targets_path = dest_root / "trust" / "targets" / f"{namespace}.json"
        targets_version = _read_signed_version(targets_path) + 1
        targets_signed = {
            "type": "targets",
            "spec_version": "1",
            "version": targets_version,
            "expires": expires_in(30),
            "namespace": namespace,
            "packages": package_map,
        }
        write_json_file(targets_path, sign_payload(targets_signed, role_keys["targets"]))
        snapshot_meta[targets_path.relative_to(dest_root).as_posix()] = signed_file_meta(targets_path, version=targets_version)

    checkpoint_path = dest_root / "log" / "checkpoint.json"
    checkpoint_version = _read_signed_version(checkpoint_path) + 1
    leaf_hashes = [
        sha256_bytes(canonical_json_bytes(load_json_file(path, f"registry v2 log leaf {path.name}")))
        for path in _leaf_sequence_paths(dest_root)
    ]
    checkpoint_signed = {
        "type": "checkpoint",
        "spec_version": "1",
        "version": checkpoint_version,
        "generated_at": registry_time,
        "tree_size": len(leaf_hashes),
        "root_hash": _log_root_hash(leaf_hashes),
    }
    write_json_file(checkpoint_path, sign_payload(checkpoint_signed, role_keys["log"]))

    snapshot_path = dest_root / "trust" / "snapshot.json"
    snapshot_version = _read_signed_version(snapshot_path) + 1
    snapshot_signed = {
        "type": "snapshot",
        "spec_version": "1",
        "version": snapshot_version,
        "expires": expires_in(7),
        "meta": snapshot_meta,
        "log_meta": {
            "log/checkpoint.json": signed_file_meta(checkpoint_path, version=checkpoint_version),
        },
    }
    write_json_file(snapshot_path, sign_payload(snapshot_signed, role_keys["snapshot"]))

    timestamp_path = dest_root / "trust" / "timestamp.json"
    timestamp_version = _read_signed_version(timestamp_path) + 1
    timestamp_signed = {
        "type": "timestamp",
        "spec_version": "1",
        "version": timestamp_version,
        "expires": expires_in(1),
        "meta": {
            "trust/snapshot.json": signed_file_meta(snapshot_path, version=snapshot_version),
        },
    }
    write_json_file(timestamp_path, sign_payload(timestamp_signed, role_keys["timestamp"]))

    return {
        "checkpoint_version": checkpoint_version,
        "snapshot_version": snapshot_version,
        "timestamp_version": timestamp_version,
        "namespaces": len(namespace_packages),
    }


def _append_release_record(dest_root: pathlib.Path, record: dict[str, Any], archive_path: pathlib.Path) -> dict[str, Any]:
    package_name = require_string(record.get("package"), "registry v2 publish record package")
    validate_package_name(package_name, "registry v2 publish record package")
    package_version = require_string(record.get("version"), "registry v2 publish record version")
    artifact = require_object(record.get("source_artifact"), "registry v2 publish source artifact")
    require_sha256_digest(artifact.get("sha256"), "registry v2 publish source artifact sha256")
    artifact_relative_path = normalize_registry_relative_path(
        require_string(artifact.get("path"), "registry v2 publish source artifact path"),
        "registry v2 publish source artifact path",
    )
    artifact_dest_path = dest_root / pathlib.PurePosixPath(artifact_relative_path)
    copy_if_missing_or_same(archive_path, artifact_dest_path)

    index_path = dest_root / index_path_for_package(package_name)
    existing_records: list[dict[str, Any]] = []
    if index_path.exists():
        existing_lines = [line for line in index_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        for line in existing_lines:
            payload = json.loads(line)
            existing_records.append(require_object(payload, f"registry v2 existing index record in {index_path}"))
        for payload in existing_records:
            if require_string(payload.get("version"), f"registry v2 existing version in {index_path}") == package_version:
                raise RegistryV2Error(f"package version is already published in registry v2: {package_name}@{package_version}")
    ensure_parent(index_path)
    with index_path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, sort_keys=True, ensure_ascii=False) + "\n")

    leaf_paths = _leaf_sequence_paths(dest_root)
    sequence = len(leaf_paths) + 1
    namespace, _ = split_package_name(package_name)
    leaf = {
        "schema_version": 1,
        "sequence": sequence,
        "namespace": namespace,
        "package": package_name,
        "version": package_version,
        "release_revision": 1,
        "index_path": index_path.relative_to(dest_root).as_posix(),
        "index_record_sha256": sha256_bytes(canonical_json_bytes(record)),
        "source_artifact_sha256": require_sha256_digest(artifact.get("sha256"), "registry v2 publish source artifact sha256"),
        "source_artifact_path": artifact_relative_path,
    }
    leaf_path = dest_root / "log" / "leaves" / f"{sequence:012d}.json"
    write_json_file(leaf_path, leaf)
    return {
        "index_path": index_path,
        "leaf_path": leaf_path,
        "sequence": sequence,
    }


def publish_to_registry_v2(
    dest_root_value: str,
    key_dir_value: str,
    *,
    archive_path_value: str | None = None,
    manifest_path_value: str | None = None,
    pafio_bin_value: str | None = None,
    package_name: str | None = None,
    output_path_value: str | None = None,
    registry_name: str = "pafio-registry-v2",
    publisher_id: str = "local-publisher",
) -> dict[str, Any]:
    if archive_path_value is None and manifest_path_value is None:
        raise RegistryV2Error("registry v2 publish requires --archive-path or --manifest-path")
    if archive_path_value is not None and manifest_path_value is not None:
        raise RegistryV2Error("registry v2 publish accepts either --archive-path or --manifest-path, not both")

    dest_root = normalize_local_root(dest_root_value)
    key_dir = normalize_local_root(key_dir_value)
    role_keys = load_role_keys(key_dir)
    registry_time = utc_now().strftime("%Y-%m-%dT%H:%M:%SZ")
    created_root = _initialize_registry_root(dest_root, role_keys, registry_name=registry_name, registry_time=registry_time)

    candidate_payload: dict[str, Any] | None = None
    if archive_path_value is not None:
        archive_path = _canonical_archive_path(archive_path_value)
    else:
        manifest_path = _canonical_archive_path(manifest_path_value or "pafio.toml")
        pafio_bin = _canonical_archive_path(pafio_bin_value or str(pathlib.Path(__file__).resolve().parents[2] / "scripts" / "pafio"))
        output_path = _canonical_archive_path(output_path_value) if output_path_value is not None else None
        candidate_payload = _prepare_publish_candidate(pafio_bin, manifest_path, package_name, output_path)
        archive_path = _canonical_archive_path(require_string(candidate_payload.get("archive_path"), "pafio publish dry-run archive_path"))

    if not archive_path.exists():
        raise RegistryV2Error(f"registry v2 publish archive was not found: {archive_path}")

    record, _ = _extract_record_from_archive(
        archive_path,
        publisher_id=publisher_id,
        published_at=registry_time,
    )
    append_result = _append_release_record(dest_root, record, archive_path)
    metadata_versions = _refresh_signed_metadata(dest_root, role_keys, registry_time)

    return {
        "ok": True,
        "registry_root": str(dest_root),
        "created_root": created_root,
        "package": record["package"],
        "version": record["version"],
        "publisher_id": publisher_id,
        "published_at": registry_time,
        "archive_path": str(archive_path),
        "archive_sha256": record["source_artifact"]["sha256"],
        "archive_size_bytes": record["source_artifact"]["size_bytes"],
        "artifact_path": record["source_artifact"]["path"],
        "index_path": append_result["index_path"].relative_to(dest_root).as_posix(),
        "log_leaf_path": append_result["leaf_path"].relative_to(dest_root).as_posix(),
        "sequence": append_result["sequence"],
        "dependencies": len(record["dependencies"]),
        "dev_dependencies": len(record["dev_dependencies"]),
        "checkpoint_version": metadata_versions["checkpoint_version"],
        "snapshot_version": metadata_versions["snapshot_version"],
        "timestamp_version": metadata_versions["timestamp_version"],
        "namespaces": metadata_versions["namespaces"],
        "candidate": candidate_payload,
    }
