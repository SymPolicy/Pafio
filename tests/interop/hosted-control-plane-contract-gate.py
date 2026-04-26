#!/usr/bin/env python3

from __future__ import annotations

import argparse
import copy
import json
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = ROOT / "contracts/hosted-control-plane/v1/hosted-control-plane.contract.json"
EXAMPLES_PATH = ROOT / "contracts/hosted-control-plane/v1/hosted-control-plane.examples.json"

EXPECTED_OPERATION_SNAPSHOT = [
    ("openWorkspace", "POST", "/workspaces/open"),
    ("projectGraph", "GET", "/workspaces/{workspace_id}/project-graph"),
    ("toolInstall", "POST", "/workspaces/{workspace_id}/tool/install"),
    ("toolUse", "POST", "/workspaces/{workspace_id}/tool/use"),
    ("toolPin", "POST", "/workspaces/{workspace_id}/tool/pin"),
    ("toolClearPin", "POST", "/workspaces/{workspace_id}/tool/clear-pin"),
    ("fetchDependencies", "POST", "/workspaces/{workspace_id}/dependencies/fetch"),
    ("vendorDependencies", "POST", "/workspaces/{workspace_id}/dependencies/vendor"),
    ("runWorkflow", "POST", "/workspaces/{workspace_id}/execution/run"),
    ("buildWorkflow", "POST", "/workspaces/{workspace_id}/execution/build"),
    ("testWorkflow", "POST", "/workspaces/{workspace_id}/execution/test"),
    ("packProject", "POST", "/workspaces/{workspace_id}/deployment/pack"),
    ("preparePublish", "POST", "/workspaces/{workspace_id}/deployment/preflight"),
    ("publishToRegistry", "POST", "/workspaces/{workspace_id}/deployment/publish"),
]


@dataclass
class ValidationContext:
    contract: dict[str, Any]
    errors: list[str]


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_contract_pack() -> tuple[dict[str, Any], dict[str, Any]]:
    return load_json(CONTRACT_PATH), load_json(EXAMPLES_PATH)


def resolve_shape(contract: dict[str, Any], name: str) -> dict[str, Any]:
    shapes = contract.get("shapes", {})
    if name not in shapes:
        raise KeyError(f"unknown shape: {name}")
    return shapes[name]


def is_integer(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def is_iso_datetime(value: str) -> bool:
    candidate = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        datetime.fromisoformat(candidate)
        return True
    except ValueError:
        return False


def validate_spec(ctx: ValidationContext, spec: dict[str, Any], value: Any, path: str) -> None:
    if "ref" in spec:
        validate_shape(ctx, spec["ref"], value, path)
        return

    spec_type = spec.get("type")
    if spec_type == "string":
        if not isinstance(value, str):
            ctx.errors.append(f"{path}: expected string")
            return
        min_length = spec.get("min_length")
        if min_length is not None and len(value) < int(min_length):
            ctx.errors.append(f"{path}: expected string length >= {min_length}")
        value_format = spec.get("format")
        if value_format == "date-time" and not is_iso_datetime(value):
            ctx.errors.append(f"{path}: expected ISO 8601 date-time string")
        return

    if spec_type == "integer":
        if not is_integer(value):
            ctx.errors.append(f"{path}: expected integer")
        return

    if spec_type == "boolean":
        if not isinstance(value, bool):
            ctx.errors.append(f"{path}: expected boolean")
        return

    if spec_type == "enum":
        allowed = spec.get("values", [])
        if value not in allowed:
            ctx.errors.append(f"{path}: expected one of {allowed!r}")
        return

    if spec_type == "array":
        if not isinstance(value, list):
            ctx.errors.append(f"{path}: expected array")
            return
        item_spec = spec.get("items")
        if not isinstance(item_spec, dict):
            ctx.errors.append(f"{path}: array items spec missing")
            return
        for index, entry in enumerate(value):
            validate_spec(ctx, item_spec, entry, f"{path}[{index}]")
        return

    if spec_type == "map":
        if not isinstance(value, dict):
            ctx.errors.append(f"{path}: expected object map")
            return
        value_spec = spec.get("values")
        if not isinstance(value_spec, dict):
            ctx.errors.append(f"{path}: map values spec missing")
            return
        for key, entry in value.items():
            if not isinstance(key, str):
                ctx.errors.append(f"{path}: expected string map keys")
                continue
            validate_spec(ctx, value_spec, entry, f"{path}.{key}")
        return

    if spec_type == "object":
        if not isinstance(value, dict):
            ctx.errors.append(f"{path}: expected object")
        return

    ctx.errors.append(f"{path}: unsupported spec type {spec_type!r}")


def validate_shape(ctx: ValidationContext, shape_name: str, value: Any, path: str) -> None:
    shape = resolve_shape(ctx.contract, shape_name)
    kind = shape.get("kind")
    if kind == "none":
        if value is not None:
            ctx.errors.append(f"{path}: expected no request body")
        return

    if kind != "object":
        ctx.errors.append(f"{path}: unsupported shape kind {kind!r}")
        return

    if not isinstance(value, dict):
        ctx.errors.append(f"{path}: expected object for shape {shape_name}")
        return

    required = shape.get("required", {})
    optional = shape.get("optional", {})
    allowed = set(required) | set(optional)

    for field_name, field_spec in required.items():
        if field_name not in value:
            ctx.errors.append(f"{path}: missing required field {field_name!r}")
            continue
        validate_spec(ctx, field_spec, value[field_name], f"{path}.{field_name}")

    for field_name, field_value in value.items():
        if field_name not in allowed:
            ctx.errors.append(f"{path}: unexpected field {field_name!r}")
            continue
        if field_name in optional:
            validate_spec(ctx, optional[field_name], field_value, f"{path}.{field_name}")


def assert_valid(contract: dict[str, Any], shape_name: str, value: Any, label: str) -> None:
    ctx = ValidationContext(contract=contract, errors=[])
    validate_shape(ctx, shape_name, value, label)
    if ctx.errors:
        raise AssertionError("\n".join(ctx.errors))


def check_unit(contract: dict[str, Any], examples: dict[str, Any]) -> None:
    assert contract.get("schema_version") == 1, "schema_version must be 1"
    assert contract.get("base_path") == "/api/styio-hosted/v1", "base_path drift detected"

    operations = contract.get("operations", [])
    assert isinstance(operations, list) and operations, "operations must be a non-empty list"
    assert len(operations) == len(EXPECTED_OPERATION_SNAPSHOT), "unexpected operation count"

    seen_ids: set[str] = set()
    seen_routes: set[tuple[str, str]] = set()
    for operation in operations:
        operation_id = operation.get("id")
        method = operation.get("method")
        path = operation.get("path")
        assert isinstance(operation_id, str) and operation_id, "operation id must be non-empty"
        assert operation_id not in seen_ids, f"duplicate operation id: {operation_id}"
        seen_ids.add(operation_id)
        assert isinstance(method, str) and method in {"GET", "POST"}, f"invalid method for {operation_id}"
        assert isinstance(path, str) and path.startswith("/workspaces/"), f"invalid path for {operation_id}"
        route_key = (method, path)
        assert route_key not in seen_routes, f"duplicate route tuple: {route_key}"
        seen_routes.add(route_key)

        for key in ("request_shape", "success_shape", "failure_shape"):
            shape_name = operation.get(key)
            assert isinstance(shape_name, str) and shape_name, f"{operation_id} missing {key}"
            resolve_shape(contract, shape_name)

        assert operation_id in examples, f"examples missing operation {operation_id}"


def check_examples(contract: dict[str, Any], examples: dict[str, Any]) -> None:
    for operation in contract["operations"]:
        operation_id = operation["id"]
        pack = examples[operation_id]
        assert_valid(contract, operation["request_shape"], pack.get("request"), f"{operation_id}.request")
        assert_valid(contract, operation["success_shape"], pack["success"], f"{operation_id}.success")
        assert_valid(contract, operation["failure_shape"], pack["failure"], f"{operation_id}.failure")


def collect_snapshot(contract: dict[str, Any]) -> list[tuple[str, str, str]]:
    return [
        (operation["id"], operation["method"], operation["path"])
        for operation in contract["operations"]
    ]


def check_regression(contract: dict[str, Any]) -> None:
    snapshot = collect_snapshot(contract)
    assert snapshot == EXPECTED_OPERATION_SNAPSHOT, (
        "operation snapshot drift detected\n"
        f"expected: {EXPECTED_OPERATION_SNAPSHOT}\n"
        f"actual:   {snapshot}"
    )


def mutate_drop_required(shape_name: str, sample: Any, contract: dict[str, Any]) -> list[tuple[str, Any]]:
    shape = resolve_shape(contract, shape_name)
    if shape.get("kind") != "object" or not isinstance(sample, dict):
        return []
    mutations: list[tuple[str, Any]] = []
    for field_name in shape.get("required", {}):
        mutated = copy.deepcopy(sample)
        mutated.pop(field_name, None)
        mutations.append((f"drop {field_name}", mutated))
    return mutations


def find_first_mutation(contract: dict[str, Any], spec: dict[str, Any], value: Any) -> Any | None:
    if "ref" in spec:
        return find_first_shape_mutation(contract, spec["ref"], value)

    spec_type = spec.get("type")
    if spec_type == "string" and isinstance(value, str):
        return 7
    if spec_type == "integer" and is_integer(value):
        return "wrong"
    if spec_type == "boolean" and isinstance(value, bool):
        return "wrong"
    if spec_type == "enum":
        return "__invalid__"
    if spec_type == "array" and isinstance(value, list) and value:
        nested = find_first_mutation(contract, spec["items"], value[0])
        if nested is not None:
            mutated = copy.deepcopy(value)
            mutated[0] = nested
            return mutated
    if spec_type == "map" and isinstance(value, dict) and value:
        first_key = next(iter(value))
        nested = find_first_mutation(contract, spec["values"], value[first_key])
        if nested is not None:
            mutated = copy.deepcopy(value)
            mutated[first_key] = nested
            return mutated
    if spec_type == "object" and isinstance(value, dict):
        return "wrong"
    return None


def find_first_shape_mutation(contract: dict[str, Any], shape_name: str, sample: Any) -> Any | None:
    shape = resolve_shape(contract, shape_name)
    if shape.get("kind") != "object" or not isinstance(sample, dict):
        return None
    for field_name, field_spec in shape.get("required", {}).items():
        if field_name not in sample:
            continue
        nested = find_first_mutation(contract, field_spec, sample[field_name])
        if nested is not None:
            mutated = copy.deepcopy(sample)
            mutated[field_name] = nested
            return mutated
    for field_name, field_spec in shape.get("optional", {}).items():
        if field_name not in sample:
            continue
        nested = find_first_mutation(contract, field_spec, sample[field_name])
        if nested is not None:
            mutated = copy.deepcopy(sample)
            mutated[field_name] = nested
            return mutated
    return None


def expect_invalid(contract: dict[str, Any], shape_name: str, sample: Any, label: str) -> None:
    ctx = ValidationContext(contract=contract, errors=[])
    validate_shape(ctx, shape_name, sample, label)
    if not ctx.errors:
        raise AssertionError(f"{label}: mutation unexpectedly validated")


def check_fuzz(contract: dict[str, Any], examples: dict[str, Any]) -> None:
    for operation in contract["operations"]:
        operation_id = operation["id"]
        pack = examples[operation_id]
        for variant_key, shape_key in (
            ("request", "request_shape"),
            ("success", "success_shape"),
            ("failure", "failure_shape"),
        ):
            shape_name = operation[shape_key]
            sample = pack.get(variant_key)
            assert_valid(contract, shape_name, sample, f"{operation_id}.{variant_key}")
            for description, mutated in mutate_drop_required(shape_name, sample, contract):
                expect_invalid(
                    contract,
                    shape_name,
                    mutated,
                    f"{operation_id}.{variant_key}.{description}",
                )
            mutated_nested = find_first_shape_mutation(contract, shape_name, sample)
            if mutated_nested is not None:
                expect_invalid(
                    contract,
                    shape_name,
                    mutated_nested,
                    f"{operation_id}.{variant_key}.nested-type-mutation",
                )


def run(mode: str) -> None:
    contract, examples = load_contract_pack()

    if mode == "unit":
        check_unit(contract, examples)
        return
    if mode == "integration":
        check_unit(contract, examples)
        check_examples(contract, examples)
        return
    if mode == "regression":
        check_regression(contract)
        return
    if mode == "smoke":
        check_examples(contract, examples)
        return
    if mode == "fuzz":
        check_fuzz(contract, examples)
        return
    raise AssertionError(f"unsupported mode: {mode}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--mode",
        required=True,
        choices=["unit", "integration", "regression", "smoke", "fuzz"],
    )
    args = parser.parse_args()
    try:
        run(args.mode)
    except Exception as exc:  # noqa: BLE001
        print(f"[hosted-control-plane-contract-gate] {exc}", file=sys.stderr)
        return 1
    print(f"[hosted-control-plane-contract-gate] mode={args.mode} ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
