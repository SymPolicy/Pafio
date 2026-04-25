from __future__ import annotations

import base64
import binascii
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Any
from urllib.parse import unquote, urlsplit


REGISTRY_OPENSSL_TIMEOUT_SECONDS = 30.0
REGISTRY_SUBPROCESS_TIMEOUT_SECONDS = 600.0
PACKAGE_NAME_RE = re.compile(r"^[a-z0-9][a-z0-9_-]*/[a-z0-9][a-z0-9_-]*$")


class RegistryV2Error(RuntimeError):
    """Raised when the registry v2 import or verification flow fails."""


@dataclass(frozen=True)
class RoleKey:
    role: str
    keyid: str
    private_key_path: pathlib.Path
    public_key_path: pathlib.Path
    public_key_pem: str


def normalize_local_root(value: str) -> pathlib.Path:
    parsed = urlsplit(value)
    if parsed.scheme and parsed.scheme != "file":
        raise RegistryV2Error("registry v2 tools support only local paths or file:// roots")
    if parsed.scheme == "file":
        path = pathlib.Path(unquote(parsed.path))
    else:
        path = pathlib.Path(value)
    return path.resolve()


def canonical_json_bytes(value: Any) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    ).encode("utf-8")


def json_text(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def load_json_file(path: pathlib.Path, context: str) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as err:
        raise RegistryV2Error(f"{context} is not valid JSON: {path}") from err
    if not isinstance(payload, dict):
        raise RegistryV2Error(f"{context} must be a JSON object: {path}")
    return payload


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def ensure_parent(path: pathlib.Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def atomic_write_text(path: pathlib.Path, content: str) -> None:
    ensure_parent(path)
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        dir=str(path.parent),
        delete=False,
    ) as handle:
        handle.write(content)
        temp_path = pathlib.Path(handle.name)
    temp_path.replace(path)


def atomic_write_bytes(path: pathlib.Path, content: bytes) -> None:
    ensure_parent(path)
    with tempfile.NamedTemporaryFile(
        "wb",
        dir=str(path.parent),
        delete=False,
    ) as handle:
        handle.write(content)
        temp_path = pathlib.Path(handle.name)
    temp_path.replace(path)


def write_json_file(path: pathlib.Path, payload: dict[str, Any]) -> None:
    atomic_write_text(path, json_text(payload))


def require_object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise RegistryV2Error(f"{context} must be a JSON object")
    return value


def require_array(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise RegistryV2Error(f"{context} must be a JSON array")
    return value


def require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise RegistryV2Error(f"{context} must be a non-empty string")
    return value


def optional_string(value: Any, context: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise RegistryV2Error(f"{context} must be a string when present")
    return value


def require_bool(value: Any, context: str) -> bool:
    if not isinstance(value, bool):
        raise RegistryV2Error(f"{context} must be a boolean")
    return value


def require_int(value: Any, context: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise RegistryV2Error(f"{context} must be an integer")
    return value


def validate_package_name(package_name: str, context: str = "package name") -> tuple[str, str]:
    if not PACKAGE_NAME_RE.fullmatch(package_name):
        raise RegistryV2Error(f"{context} must match namespace/name: {package_name}")
    parts = package_name.split("/", 1)
    return parts[0], parts[1]


def split_package_name(package_name: str) -> tuple[str, str]:
    return validate_package_name(package_name)


def normalize_registry_relative_path(relative_path: str, context: str = "registry object path") -> str:
    if not relative_path or relative_path.endswith("/"):
        raise RegistryV2Error(f"{context} must be a non-empty POSIX-relative path: {relative_path!r}")
    if relative_path.startswith("/") or "\\" in relative_path:
        raise RegistryV2Error(f"{context} must be a POSIX-relative path inside the registry root: {relative_path!r}")
    if any(ord(ch) < 32 for ch in relative_path):
        raise RegistryV2Error(f"{context} must not contain control characters: {relative_path!r}")
    parts = relative_path.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise RegistryV2Error(f"{context} must be canonical and must not escape the registry root: {relative_path!r}")
    return relative_path


def require_sha256_digest(value: Any, context: str) -> str:
    digest = require_string(value, context)
    if len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest):
        raise RegistryV2Error(f"{context} must be a lowercase sha256 digest")
    return digest


def artifact_bucket_path(prefix: str, digest: str, suffix: str) -> pathlib.Path:
    return pathlib.Path(prefix) / digest[:2] / digest[2:4] / f"{digest}{suffix}"


def index_path_for_package(package_name: str) -> pathlib.Path:
    namespace, short_name = split_package_name(package_name)
    return pathlib.Path("index") / namespace / f"{short_name}.jsonl"


def parse_semver_key(version: str) -> tuple[int, int, int, str]:
    parts = version.split(".")
    if len(parts) == 3 and all(part.isdigit() for part in parts):
        return (int(parts[0]), int(parts[1]), int(parts[2]), "")
    return (0, 0, 0, version)


def utc_now() -> datetime:
    return datetime.now(tz=UTC)


def expires_in(days: int) -> str:
    return (utc_now() + timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%SZ")


def openssl_run(args: list[str], *, input_bytes: bytes | None = None) -> subprocess.CompletedProcess[bytes]:
    try:
        proc = subprocess.run(
            ["openssl", *args],
            input=input_bytes,
            capture_output=True,
            check=False,
            timeout=REGISTRY_OPENSSL_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as err:
        raise RegistryV2Error(f"openssl {' '.join(args)} timed out after {REGISTRY_OPENSSL_TIMEOUT_SECONDS:g}s") from err
    if proc.returncode != 0:
        stderr = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RegistryV2Error(f"openssl {' '.join(args)} failed: {stderr}")
    return proc


def file_keyid(public_key_path: pathlib.Path) -> str:
    der = openssl_run(
        ["pkey", "-pubin", "-in", str(public_key_path), "-outform", "DER"],
    ).stdout
    return sha256_bytes(der)


def load_role_keys(key_dir: pathlib.Path) -> dict[str, RoleKey]:
    manifest_path = key_dir / "keys.json"
    manifest = load_json_file(manifest_path, "registry v2 key manifest")
    roles = require_object(manifest.get("roles"), "registry v2 key manifest roles")
    loaded: dict[str, RoleKey] = {}
    for role, payload in roles.items():
        role_object = require_object(payload, f"registry v2 key manifest role '{role}'")
        keyid = require_string(role_object.get("keyid"), f"registry v2 key manifest role '{role}' keyid")
        private_rel = require_string(
            role_object.get("private_key_path"),
            f"registry v2 key manifest role '{role}' private_key_path",
        )
        public_rel = require_string(
            role_object.get("public_key_path"),
            f"registry v2 key manifest role '{role}' public_key_path",
        )
        private_key_path = (key_dir / private_rel).resolve()
        public_key_path = (key_dir / public_rel).resolve()
        if not private_key_path.exists():
            raise RegistryV2Error(f"registry v2 private key not found for role '{role}': {private_key_path}")
        if not public_key_path.exists():
            raise RegistryV2Error(f"registry v2 public key not found for role '{role}': {public_key_path}")
        loaded[role] = RoleKey(
            role=role,
            keyid=keyid,
            private_key_path=private_key_path,
            public_key_path=public_key_path,
            public_key_pem=public_key_path.read_text(encoding="utf-8"),
        )
    return loaded


def sign_payload(signed_payload: dict[str, Any], role_key: RoleKey) -> dict[str, Any]:
    message = canonical_json_bytes(signed_payload)
    with tempfile.TemporaryDirectory(prefix="spio-registry-v2-sign-") as temp_dir:
        temp_root = pathlib.Path(temp_dir)
        message_path = temp_root / "message.json"
        signature_path = temp_root / "message.sig"
        atomic_write_bytes(message_path, message)
        openssl_run(
            [
                "pkeyutl",
                "-sign",
                "-inkey",
                str(role_key.private_key_path),
                "-rawin",
                "-in",
                str(message_path),
                "-out",
                str(signature_path),
            ]
        )
        signature = base64.b64encode(signature_path.read_bytes()).decode("ascii")
    return {
        "signed": signed_payload,
        "signatures": [
            {
                "keyid": role_key.keyid,
                "sig": signature,
            }
        ],
    }


def verify_signature(signed_payload: dict[str, Any], signature_b64: str, public_key_pem: str) -> None:
    try:
        signature = base64.b64decode(signature_b64, validate=True)
    except (ValueError, binascii.Error):
        raise RegistryV2Error("signature is not valid base64")
    with tempfile.TemporaryDirectory(prefix="spio-registry-v2-verify-") as temp_dir:
        temp_root = pathlib.Path(temp_dir)
        message_path = temp_root / "message.json"
        signature_path = temp_root / "message.sig"
        public_path = temp_root / "role.pub"
        atomic_write_bytes(message_path, canonical_json_bytes(signed_payload))
        atomic_write_bytes(signature_path, signature)
        atomic_write_text(public_path, public_key_pem)
        openssl_run(
            [
                "pkeyutl",
                "-verify",
                "-pubin",
                "-inkey",
                str(public_path),
                "-rawin",
                "-in",
                str(message_path),
                "-sigfile",
                str(signature_path),
            ]
        )


def signed_file_meta(path: pathlib.Path, *, version: int) -> dict[str, Any]:
    return {
        "version": version,
        "length": path.stat().st_size,
        "hashes": {
            "sha256": sha256_file(path),
        },
    }


def copy_if_missing_or_same(source: pathlib.Path, destination: pathlib.Path) -> str:
    ensure_parent(destination)
    if destination.exists():
        if sha256_file(source) != sha256_file(destination):
            raise RegistryV2Error(f"destination object already exists with different content: {destination}")
        return "reused"
    shutil.copyfile(source, destination)
    return "copied"
