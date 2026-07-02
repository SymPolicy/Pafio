from __future__ import annotations

import hashlib
import json
import pathlib
from dataclasses import dataclass, field
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin, urlsplit
from urllib.request import urlopen

from .common import (
    RegistryV2Error,
    canonical_json_bytes,
    normalize_registry_relative_path,
    normalize_local_root,
    parse_semver_key,
    require_array,
    require_bool,
    require_int,
    require_object,
    require_sha256_digest,
    require_string,
    sha256_bytes,
    split_package_name,
    verify_signature,
)


HTTP_READ_TIMEOUT_SECONDS = 10.0
HTTP_METADATA_MAX_BYTES = 16 * 1024 * 1024
HTTP_INDEX_MAX_BYTES = 16 * 1024 * 1024
HTTP_ARTIFACT_MAX_BYTES = 512 * 1024 * 1024

JSON_MEDIA_TYPES = {"application/json", "application/octet-stream"}
INDEX_MEDIA_TYPES = {"application/x-ndjson", "application/jsonl", "text/plain", "application/octet-stream"}
ARTIFACT_MEDIA_TYPES = {"application/x-tar", "application/tar", "application/octet-stream"}


def _remote_constraints(relative_path: str) -> tuple[int, set[str], str]:
    if relative_path.endswith(".json"):
        return HTTP_METADATA_MAX_BYTES, JSON_MEDIA_TYPES, "JSON metadata"
    if relative_path.endswith(".jsonl"):
        return HTTP_INDEX_MAX_BYTES, INDEX_MEDIA_TYPES, "package index"
    if relative_path.startswith("artifacts/source/") or relative_path.startswith("artifacts/binary/"):
        return HTTP_ARTIFACT_MAX_BYTES, ARTIFACT_MEDIA_TYPES, "artifact"
    return HTTP_METADATA_MAX_BYTES, JSON_MEDIA_TYPES | INDEX_MEDIA_TYPES, "registry object"


def _response_media_type(response: Any) -> str:
    headers = getattr(response, "headers", None)
    if headers is None:
        return ""
    get_content_type = getattr(headers, "get_content_type", None)
    if callable(get_content_type):
        return str(get_content_type()).lower()
    value = headers.get("Content-Type", "") if hasattr(headers, "get") else ""
    return str(value).split(";", 1)[0].strip().lower()


def _response_content_length(response: Any) -> int | None:
    headers = getattr(response, "headers", None)
    value = headers.get("Content-Length") if headers is not None and hasattr(headers, "get") else None
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError) as err:
        raise RegistryV2Error("registry v2 remote object has an invalid Content-Length header") from err


def _read_limited_response(response: Any, *, location: str, max_bytes: int) -> bytes:
    declared_length = _response_content_length(response)
    if declared_length is not None and declared_length > max_bytes:
        raise RegistryV2Error(f"registry v2 remote object exceeds {max_bytes} bytes: {location}")
    chunks: list[bytes] = []
    total = 0
    while True:
        chunk = response.read(min(1024 * 1024, max_bytes + 1 - total))
        if not chunk:
            break
        total += len(chunk)
        if total > max_bytes:
            raise RegistryV2Error(f"registry v2 remote object exceeds {max_bytes} bytes: {location}")
        chunks.append(chunk)
    return b"".join(chunks)


@dataclass
class RootReader:
    root_value: str
    local_root: pathlib.Path | None = None
    remote_root: str | None = None
    _cache: dict[str, bytes] = field(default_factory=dict)

    def __post_init__(self) -> None:
        parsed = urlsplit(self.root_value)
        if parsed.scheme in ("", "file"):
            self.local_root = normalize_local_root(self.root_value)
            return
        if parsed.scheme in ("http", "https"):
            self.remote_root = self.root_value.rstrip("/") + "/"
            return
        raise RegistryV2Error("registry v2 verify supports only local paths, file:// roots, or http(s) roots")

    def _normalize_relative(self, relative_path: str) -> str:
        return normalize_registry_relative_path(relative_path, "registry v2 relative object path")

    def _local_path(self, relative_path: str) -> pathlib.Path:
        if self.local_root is None:
            raise RegistryV2Error("registry v2 reader is not bound to a local root")
        relative = pathlib.PurePosixPath(self._normalize_relative(relative_path))
        candidate = (self.local_root / relative).resolve()
        try:
            candidate.relative_to(self.local_root)
        except ValueError as err:
            raise RegistryV2Error(f"registry v2 object path escapes local root: {relative_path}") from err
        return candidate

    def location(self, relative_path: str) -> str:
        normalized = self._normalize_relative(relative_path)
        if self.local_root is not None:
            return str(self._local_path(normalized))
        assert self.remote_root is not None
        return urljoin(self.remote_root, normalized)

    def read_bytes(self, relative_path: str) -> bytes:
        normalized = self._normalize_relative(relative_path)
        cached = self._cache.get(normalized)
        if cached is not None:
            return cached
        if self.local_root is not None:
            location = self._local_path(normalized)
            try:
                payload = location.read_bytes()
            except OSError as err:
                raise RegistryV2Error(f"registry v2 object could not be read: {location}") from err
        else:
            assert self.remote_root is not None
            location = urljoin(self.remote_root, normalized)
            max_bytes, media_types, object_kind = _remote_constraints(normalized)
            try:
                with urlopen(location, timeout=HTTP_READ_TIMEOUT_SECONDS) as response:
                    media_type = _response_media_type(response)
                    if media_type not in media_types:
                        raise RegistryV2Error(
                            f"registry v2 remote {object_kind} has unsupported media type '{media_type or '<missing>'}': {location}"
                        )
                    payload = _read_limited_response(response, location=location, max_bytes=max_bytes)
            except (HTTPError, TimeoutError, URLError) as err:
                raise RegistryV2Error(f"registry v2 object could not be fetched: {location}") from err
        self._cache[normalized] = payload
        return payload

    def load_json(self, relative_path: str, context: str) -> dict[str, Any]:
        location = self.location(relative_path)
        try:
            payload = json.loads(self.read_bytes(relative_path).decode("utf-8"))
        except UnicodeDecodeError as err:
            raise RegistryV2Error(f"{context} is not valid UTF-8: {location}") from err
        except json.JSONDecodeError as err:
            raise RegistryV2Error(f"{context} is not valid JSON: {location}") from err
        if not isinstance(payload, dict):
            raise RegistryV2Error(f"{context} must be a JSON object: {location}")
        return payload

    def object_length(self, relative_path: str) -> int:
        return len(self.read_bytes(relative_path))

    def object_sha256(self, relative_path: str) -> str:
        return sha256_bytes(self.read_bytes(relative_path))


def _validate_envelope(payload: dict[str, Any], context: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    signed = require_object(payload.get("signed"), f"{context} signed payload")
    signatures = require_array(payload.get("signatures"), f"{context} signatures")
    if not signatures:
        raise RegistryV2Error(f"{context} must contain at least one signature")
    return signed, [require_object(item, f"{context} signature") for item in signatures]


def _verify_role_envelope(
    payload: dict[str, Any],
    *,
    context: str,
    required_type: str,
    allowed_keyids: set[str],
    key_lookup: dict[str, str],
) -> dict[str, Any]:
    signed, signatures = _validate_envelope(payload, context)
    if require_string(signed.get("type"), f"{context} type") != required_type:
        raise RegistryV2Error(f"{context} type must equal '{required_type}'")
    if require_string(signed.get("spec_version"), f"{context} spec_version") != "1":
        raise RegistryV2Error(f"{context} spec_version must equal '1'")
    valid_signatures = 0
    for signature in signatures:
        keyid = require_string(signature.get("keyid"), f"{context} signature keyid")
        signature_value = require_string(signature.get("sig"), f"{context} signature value")
        if keyid not in allowed_keyids:
            continue
        public_key_pem = key_lookup.get(keyid)
        if public_key_pem is None:
            continue
        verify_signature(signed, signature_value, public_key_pem)
        valid_signatures += 1
    if valid_signatures < 1:
        raise RegistryV2Error(f"{context} does not contain a valid trusted signature")
    return signed


def verify_registry_root(root_path_value: str) -> dict[str, Any]:
    root_reader = RootReader(root_path_value)
    config = root_reader.load_json("config.json", "registry v2 config")
    if config.get("protocol") != "pafio-static-registry" or config.get("protocol_version") != 2:
        raise RegistryV2Error("registry v2 config does not declare the expected protocol/version")
    capabilities = require_object(config.get("capabilities"), "registry v2 config capabilities")
    require_bool(capabilities.get("append_only_index"), "registry v2 config capabilities.append_only_index")
    require_bool(capabilities.get("source_artifacts"), "registry v2 config capabilities.source_artifacts")
    require_bool(capabilities.get("binary_artifacts"), "registry v2 config capabilities.binary_artifacts")
    require_bool(capabilities.get("transparency_log"), "registry v2 config capabilities.transparency_log")

    root_envelope = root_reader.load_json("trust/root.json", "registry v2 root metadata")
    root_signed, root_signatures = _validate_envelope(root_envelope, "registry v2 root metadata")
    if require_string(root_signed.get("type"), "registry v2 root metadata type") != "root":
        raise RegistryV2Error("registry v2 root metadata type must equal 'root'")
    keys = require_object(root_signed.get("keys"), "registry v2 root metadata keys")
    roles = require_object(root_signed.get("roles"), "registry v2 root metadata roles")
    root_role = require_object(roles.get("root"), "registry v2 root metadata role 'root'")
    root_keyids = set(require_array(root_role.get("keyids"), "registry v2 root metadata root keyids"))
    key_lookup: dict[str, str] = {}
    for keyid, key_payload in keys.items():
        key_object = require_object(key_payload, f"registry v2 key '{keyid}'")
        keyval = require_object(key_object.get("keyval"), f"registry v2 key '{keyid}' keyval")
        key_lookup[keyid] = require_string(keyval.get("public"), f"registry v2 key '{keyid}' public key")

    root_valid_signatures = 0
    for signature in root_signatures:
        keyid = require_string(signature.get("keyid"), "registry v2 root signature keyid")
        if keyid not in root_keyids:
            continue
        verify_signature(
            root_signed,
            require_string(signature.get("sig"), "registry v2 root signature value"),
            key_lookup[keyid],
        )
        root_valid_signatures += 1
    if root_valid_signatures < 1:
        raise RegistryV2Error("registry v2 root metadata does not contain a valid root signature")

    timestamp_role = require_object(roles.get("timestamp"), "registry v2 role 'timestamp'")
    snapshot_role = require_object(roles.get("snapshot"), "registry v2 role 'snapshot'")
    targets_role = require_object(roles.get("targets"), "registry v2 role 'targets'")
    log_role = require_object(roles.get("log"), "registry v2 role 'log'")

    timestamp_signed = _verify_role_envelope(
        root_reader.load_json("trust/timestamp.json", "registry v2 timestamp metadata"),
        context="registry v2 timestamp metadata",
        required_type="timestamp",
        allowed_keyids=set(require_array(timestamp_role.get("keyids"), "registry v2 timestamp keyids")),
        key_lookup=key_lookup,
    )
    snapshot_signed = _verify_role_envelope(
        root_reader.load_json("trust/snapshot.json", "registry v2 snapshot metadata"),
        context="registry v2 snapshot metadata",
        required_type="snapshot",
        allowed_keyids=set(require_array(snapshot_role.get("keyids"), "registry v2 snapshot keyids")),
        key_lookup=key_lookup,
    )
    checkpoint_signed = _verify_role_envelope(
        root_reader.load_json("log/checkpoint.json", "registry v2 transparency checkpoint"),
        context="registry v2 transparency checkpoint",
        required_type="checkpoint",
        allowed_keyids=set(require_array(log_role.get("keyids"), "registry v2 log keyids")),
        key_lookup=key_lookup,
    )

    timestamp_meta = require_object(timestamp_signed.get("meta"), "registry v2 timestamp meta")
    snapshot_meta = require_object(
        timestamp_meta.get("trust/snapshot.json"),
        "registry v2 timestamp meta trust/snapshot.json",
    )
    if require_int(snapshot_meta.get("length"), "registry v2 timestamp snapshot length") != root_reader.object_length("trust/snapshot.json"):
        raise RegistryV2Error("registry v2 timestamp metadata snapshot length does not match file size")
    snapshot_hashes = require_object(snapshot_meta.get("hashes"), "registry v2 timestamp snapshot hashes")
    if require_sha256_digest(snapshot_hashes.get("sha256"), "registry v2 timestamp snapshot sha256") != root_reader.object_sha256("trust/snapshot.json"):
        raise RegistryV2Error("registry v2 timestamp metadata snapshot sha256 does not match file digest")

    namespace_targets_count = 0
    index_files_count = 0
    release_count = 0

    snapshot_meta_entries = require_object(snapshot_signed.get("meta"), "registry v2 snapshot meta")
    log_meta_entries = require_object(snapshot_signed.get("log_meta"), "registry v2 snapshot log_meta")
    checkpoint_meta = require_object(
        log_meta_entries.get("log/checkpoint.json"),
        "registry v2 snapshot log_meta log/checkpoint.json",
    )
    if require_sha256_digest(
        require_object(checkpoint_meta.get("hashes"), "registry v2 checkpoint hashes").get("sha256"),
        "registry v2 checkpoint sha256",
    ) != root_reader.object_sha256("log/checkpoint.json"):
        raise RegistryV2Error("registry v2 snapshot checkpoint sha256 does not match file digest")

    trusted_targets_keyids = set(require_array(targets_role.get("keyids"), "registry v2 targets keyids"))

    for relative_path, meta in snapshot_meta_entries.items():
        normalized_relative_path = normalize_registry_relative_path(relative_path, "registry v2 snapshot meta path")
        relative = pathlib.PurePosixPath(normalized_relative_path)
        meta_object = require_object(meta, f"registry v2 snapshot meta entry '{relative_path}'")
        expected_hash = require_sha256_digest(
            require_object(meta_object.get("hashes"), f"registry v2 snapshot hashes for '{relative_path}'").get("sha256"),
            f"registry v2 snapshot sha256 for '{relative_path}'",
        )
        if expected_hash != root_reader.object_sha256(normalized_relative_path):
            raise RegistryV2Error(f"registry v2 snapshot hash mismatch for {relative_path}")
        if relative.parts[:2] == ("trust", "targets"):
            namespace_targets_count += 1
            targets_signed = _verify_role_envelope(
                root_reader.load_json(normalized_relative_path, f"registry v2 targets metadata '{relative_path}'"),
                context=f"registry v2 targets metadata '{relative_path}'",
                required_type="targets",
                allowed_keyids=trusted_targets_keyids,
                key_lookup=key_lookup,
            )
            namespace = require_string(targets_signed.get("namespace"), f"registry v2 targets namespace '{relative_path}'")
            packages = require_object(targets_signed.get("packages"), f"registry v2 targets packages '{relative_path}'")
            for package_name, package_payload in packages.items():
                package_object = require_object(package_payload, f"registry v2 targets package '{package_name}'")
                package_namespace, _ = split_package_name(package_name)
                if package_namespace != namespace:
                    raise RegistryV2Error(f"registry v2 targets package '{package_name}' does not match namespace '{namespace}'")
                index_relative_path = normalize_registry_relative_path(
                    require_string(
                        package_object.get("index_path"),
                        f"registry v2 targets package '{package_name}' index_path",
                    ),
                    f"registry v2 targets package '{package_name}' index_path",
                )
                lines = [
                    line
                    for line in root_reader.read_bytes(index_relative_path).decode("utf-8").splitlines()
                    if line.strip()
                ]
                if not lines:
                    raise RegistryV2Error(f"registry v2 index file is empty: {root_reader.location(index_relative_path)}")
                index_files_count += 1
                versions = []
                record_digests: dict[str, str] = {}
                for line in lines:
                    record = require_object(json.loads(line), f"registry v2 index record in {root_reader.location(index_relative_path)}")
                    if require_string(record.get("package"), f"registry v2 index record package in {index_relative_path}") != package_name:
                        raise RegistryV2Error(f"registry v2 index record package mismatch in {root_reader.location(index_relative_path)}")
                    version = require_string(record.get("version"), f"registry v2 index record version in {index_relative_path}")
                    source_artifact = require_object(record.get("source_artifact"), f"registry v2 source artifact in {index_relative_path}")
                    artifact_digest = require_sha256_digest(
                        source_artifact.get("sha256"),
                        f"registry v2 source artifact sha256 in {index_relative_path}",
                    )
                    artifact_size_bytes = require_int(
                        source_artifact.get("size_bytes"),
                        f"registry v2 source artifact size_bytes in {index_relative_path}",
                    )
                    artifact_relative_path = normalize_registry_relative_path(
                        require_string(
                            source_artifact.get("path"),
                            f"registry v2 source artifact path in {index_relative_path}",
                        ),
                        f"registry v2 source artifact path in {index_relative_path}",
                    )
                    if root_reader.object_sha256(artifact_relative_path) != artifact_digest:
                        raise RegistryV2Error(f"registry v2 source artifact digest mismatch: {root_reader.location(artifact_relative_path)}")
                    if root_reader.object_length(artifact_relative_path) != artifact_size_bytes:
                        raise RegistryV2Error(f"registry v2 source artifact size mismatch: {root_reader.location(artifact_relative_path)}")
                    record_digest = sha256_bytes(canonical_json_bytes(record))
                    record_digests[version] = record_digest
                    versions.append(version)
                    release_count += 1
                latest_version = require_string(
                    package_object.get("latest_version"),
                    f"registry v2 latest_version for '{package_name}'",
                )
                if latest_version != sorted(versions, key=parse_semver_key)[-1]:
                    raise RegistryV2Error(f"registry v2 latest_version mismatch for {package_name}")
                releases = require_object(package_object.get("releases"), f"registry v2 releases for '{package_name}'")
                for version, release_payload in releases.items():
                    release_object = require_object(release_payload, f"registry v2 release '{package_name}@{version}'")
                    if require_sha256_digest(
                        release_object.get("index_record_sha256"),
                        f"registry v2 release index_record_sha256 for '{package_name}@{version}'",
                    ) != record_digests.get(version):
                        raise RegistryV2Error(f"registry v2 release digest mismatch for {package_name}@{version}")

    tree_size = require_int(checkpoint_signed.get("tree_size"), "registry v2 checkpoint tree_size")
    root_hash = require_sha256_digest(checkpoint_signed.get("root_hash"), "registry v2 checkpoint root_hash")
    leaf_hashes: list[str] = []
    for sequence in range(1, tree_size + 1):
        leaf_relative_path = f"log/leaves/{sequence:012d}.json"
        leaf = root_reader.load_json(leaf_relative_path, f"registry v2 log leaf {sequence}")
        if require_int(leaf.get("sequence"), f"registry v2 log leaf {sequence} sequence") != sequence:
            raise RegistryV2Error(f"registry v2 log leaf sequence mismatch: {root_reader.location(leaf_relative_path)}")
        leaf_hashes.append(sha256_bytes(canonical_json_bytes(leaf)))
    computed_root = bytes(32)
    for leaf_hash in leaf_hashes:
        computed_root = hashlib.sha256(computed_root + bytes.fromhex(leaf_hash)).digest()
    if computed_root.hex() != root_hash:
        raise RegistryV2Error("registry v2 transparency checkpoint root hash mismatch")

    return {
        "ok": True,
        "root": root_reader.remote_root if root_reader.remote_root is not None else str(root_reader.local_root),
        "namespaces": namespace_targets_count,
        "index_files": index_files_count,
        "releases": release_count,
        "tree_size": tree_size,
    }
