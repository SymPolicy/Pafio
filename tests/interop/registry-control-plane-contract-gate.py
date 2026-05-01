#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = ROOT / "contracts/registry-control-plane/v1/registry-control-plane.contract.json"
EXAMPLES_PATH = ROOT / "contracts/registry-control-plane/v1/registry-control-plane.examples.json"

EXPECTED_OPERATION_SNAPSHOT = [
    ("registryStatus", "GET", "/status"),
    ("registryDescriptor", "GET", "/descriptor"),
    ("publishRelease", "POST", "/publish"),
    ("verifyRegistry", "POST", "/verify"),
]


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def resolve_shape(contract: dict[str, Any], name: str) -> dict[str, Any]:
    shapes = contract.get("shapes", {})
    if name not in shapes:
        raise KeyError(f"unknown shape: {name}")
    return shapes[name]


def is_integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def is_iso_datetime(value: str) -> bool:
    from datetime import datetime
    candidate = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        datetime.fromisoformat(candidate)
        return True
    except ValueError:
        return False


def validate_spec(contract: dict[str, Any], spec: dict[str, Any], value: Any, path: str, errors: list[str]) -> None:
    if "ref" in spec:
        validate_shape(contract, spec["ref"], value, path, errors)
        return

    spec_type = spec.get("type")
    if spec_type == "string":
        if not isinstance(value, str):
            errors.append(f"{path}: expected string")
            return
        min_length = spec.get("min_length")
        if min_length is not None and len(value) < int(min_length):
            errors.append(f"{path}: expected string length >= {min_length}")
        if spec.get("format") == "date-time" and not is_iso_datetime(value):
            errors.append(f"{path}: expected ISO 8601 date-time string")
        return
    if spec_type == "integer":
        if not is_integer(value):
            errors.append(f"{path}: expected integer")
        return
    if spec_type == "boolean":
        if not isinstance(value, bool):
            errors.append(f"{path}: expected boolean")
        return
    errors.append(f"{path}: unsupported spec type {spec_type!r}")


def validate_shape(contract: dict[str, Any], shape_name: str, value: Any, path: str, errors: list[str]) -> None:
    shape = resolve_shape(contract, shape_name)
    kind = shape.get("kind")
    if kind == "none":
        if value is not None:
            errors.append(f"{path}: expected no request body")
        return
    if kind != "object":
        errors.append(f"{path}: unsupported shape kind {kind!r}")
        return
    if not isinstance(value, dict):
        errors.append(f"{path}: expected object")
        return
    required = shape.get("required", {})
    optional = shape.get("optional", {})
    allowed = set(required) | set(optional)
    for field_name, field_spec in required.items():
        if field_name not in value:
            errors.append(f"{path}: missing required field {field_name!r}")
            continue
        validate_spec(contract, field_spec, value[field_name], f"{path}.{field_name}", errors)
    for field_name, field_value in value.items():
        if field_name not in allowed:
            errors.append(f"{path}: unexpected field {field_name!r}")
            continue
        if field_name in optional:
            validate_spec(contract, optional[field_name], field_value, f"{path}.{field_name}", errors)


def assert_valid(contract: dict[str, Any], shape_name: str, value: Any, label: str) -> None:
    errors: list[str] = []
    validate_shape(contract, shape_name, value, label, errors)
    if errors:
        raise AssertionError("\n".join(errors))


def main() -> int:
    contract = load_json(CONTRACT_PATH)
    examples = load_json(EXAMPLES_PATH)
    if contract.get("schema_version") != 1:
        print("schema_version must be 1", file=sys.stderr)
        return 1
    if contract.get("base_path") != "/api/spio-registry-control/v1":
        print("base_path drift detected", file=sys.stderr)
        return 1
    snapshot = [(item["id"], item["method"], item["path"]) for item in contract["operations"]]
    if snapshot != EXPECTED_OPERATION_SNAPSHOT:
        print(f"operation snapshot drift detected: {snapshot!r}", file=sys.stderr)
        return 1
    for operation in contract["operations"]:
        operation_id = operation["id"]
        if operation_id not in examples:
            print(f"missing examples for {operation_id}", file=sys.stderr)
            return 1
        pack = examples[operation_id]
        assert_valid(contract, operation["request_shape"], pack.get("request"), f"{operation_id}.request")
        assert_valid(contract, operation["success_shape"], pack["success"], f"{operation_id}.success")
        assert_valid(contract, operation["failure_shape"], pack["failure"], f"{operation_id}.failure")
    print(f"[registry-control-plane-contract-gate] ok base_path={contract['base_path']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
