from __future__ import annotations

import json
import pathlib

from .common import RegistryV2Error, file_keyid, json_text, openssl_run


ROLE_NAMES = ("root", "timestamp", "snapshot", "targets", "log")


def _generate_role_key(output_dir: pathlib.Path, role: str, *, force: bool) -> dict[str, str]:
    private_dir = output_dir / "private"
    public_dir = output_dir / "public"
    private_dir.mkdir(parents=True, exist_ok=True)
    public_dir.mkdir(parents=True, exist_ok=True)

    private_path = private_dir / f"{role}.pem"
    public_path = public_dir / f"{role}.pem"
    if not force and (private_path.exists() or public_path.exists()):
        raise RegistryV2Error(f"registry v2 key already exists for role '{role}' in {output_dir}")

    openssl_run(["genpkey", "-algorithm", "Ed25519", "-out", str(private_path)])
    openssl_run(
        [
            "pkey",
            "-in",
            str(private_path),
            "-pubout",
            "-out",
            str(public_path),
        ]
    )
    return {
        "keyid": file_keyid(public_path),
        "private_key_path": str(private_path.relative_to(output_dir)),
        "public_key_path": str(public_path.relative_to(output_dir)),
    }


def generate_key_directory(output_dir: pathlib.Path, *, force: bool = False) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema_version": 1,
        "algorithm": "ed25519",
        "roles": {
            role: _generate_role_key(output_dir, role, force=force)
            for role in ROLE_NAMES
        },
    }
    (output_dir / "keys.json").write_text(json_text(manifest), encoding="utf-8")
    return manifest
