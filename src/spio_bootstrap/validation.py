from __future__ import annotations

import re
from dataclasses import dataclass


NAME_RE = re.compile(r"^[a-z0-9][a-z0-9_-]*/[a-z0-9][a-z0-9_-]*$")
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")


class ValidationError(ValueError):
    """Raised when a manifest or lockfile violates bootstrap rules."""


@dataclass(frozen=True)
class SourceKinds:
    path: bool
    git: bool
    version: bool


def _expect_table(doc: dict, key: str) -> dict:
    value = doc.get(key)
    if not isinstance(value, dict):
        raise ValidationError(f"missing or invalid [{key}] table")
    return value


def _validate_dependency_table(table: dict, table_name: str) -> None:
    for alias, value in table.items():
        if not isinstance(alias, str) or not alias:
            raise ValidationError(f"invalid dependency alias in [{table_name}]")
        if not isinstance(value, dict):
            raise ValidationError(f"dependency '{alias}' in [{table_name}] must be an inline table")

        kinds = SourceKinds(
            path="path" in value,
            git="git" in value,
            version="version" in value,
        )
        if sum((kinds.path, kinds.git, kinds.version)) != 1:
            raise ValidationError(
                f"dependency '{alias}' in [{table_name}] must declare exactly one source kind"
            )
        if kinds.git and "rev" not in value:
            raise ValidationError(f"dependency '{alias}' in [{table_name}] requires 'rev' when 'git' is used")


def validate_manifest_document(doc: dict) -> None:
    spio_meta = _expect_table(doc, "spio")
    if spio_meta.get("manifest-version") != 1:
        raise ValidationError("spio manifest-version must be 1")

    has_package = isinstance(doc.get("package"), dict)
    has_workspace = isinstance(doc.get("workspace"), dict)
    if not has_package and not has_workspace:
        raise ValidationError("manifest must contain [package], [workspace], or both")

    if has_package:
        package = _expect_table(doc, "package")
        name = package.get("name")
        version = package.get("version")
        if not isinstance(name, str) or not NAME_RE.match(name):
            raise ValidationError("package.name must match namespace/name")
        if not isinstance(version, str) or not SEMVER_RE.match(version):
            raise ValidationError("package.version must be strict semver x.y.z")

    for dep_table_name in ("dependencies", "dev-dependencies"):
        value = doc.get(dep_table_name)
        if value is None:
            continue
        if not isinstance(value, dict):
            raise ValidationError(f"[{dep_table_name}] must be a table")
        _validate_dependency_table(value, dep_table_name)


def validate_lock_document(doc: dict) -> None:
    if doc.get("lock-version") != 1:
        raise ValidationError("lock-version must be 1")
    packages = doc.get("package", [])
    if packages is None:
        packages = []
    if not isinstance(packages, list):
        raise ValidationError("lockfile package entries must be an array of tables")
    for package in packages:
        if not isinstance(package, dict):
            raise ValidationError("lockfile package entry must be a table")
        name = package.get("name")
        version = package.get("version")
        if name is not None and (not isinstance(name, str) or not NAME_RE.match(name)):
            raise ValidationError("lockfile package name must match namespace/name")
        if version is not None and (not isinstance(version, str) or not SEMVER_RE.match(version)):
            raise ValidationError("lockfile package version must be strict semver x.y.z")
