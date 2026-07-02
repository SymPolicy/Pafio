#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_DIR = ROOT / "contracts" / "registry-v2" / "v1"
CONTRACT_PATH = CONTRACT_DIR / "registry-v2.contract.json"
EXAMPLES_PATH = CONTRACT_DIR / "registry-v2.examples.json"

EXPECTED_SCHEMA_MAP = {
    "config": "config.schema.json",
    "root": "signed-root.schema.json",
    "timestamp": "signed-timestamp.schema.json",
    "snapshot": "signed-snapshot.schema.json",
    "namespace_targets": "signed-namespace-targets.schema.json",
    "package_index_record": "package-index-record.schema.json",
    "log_checkpoint": "signed-checkpoint.schema.json",
    "log_leaf": "transparency-log-leaf.schema.json",
}

EXPECTED_EXAMPLES = {
    "config",
    "root",
    "timestamp",
    "snapshot",
    "namespace_targets",
    "package_index_record",
    "checkpoint",
    "transparency_log_leaf",
}


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def require_object(value: Any, context: str, errors: list[str]) -> dict[str, Any]:
    if not isinstance(value, dict):
        errors.append(f"{context} must be a JSON object")
        return {}
    return value


def require_string(value: Any, context: str, errors: list[str]) -> str:
    if not isinstance(value, str) or not value:
        errors.append(f"{context} must be a non-empty string")
        return ""
    return value


def validate_contract(contract: dict[str, Any], errors: list[str]) -> None:
    require(contract.get("schema_version") == 1, "contract.schema_version must equal 1", errors)
    require(contract.get("protocol") == "pafio-static-registry", "contract.protocol drift detected", errors)
    require(contract.get("protocol_version") == 2, "contract.protocol_version must equal 2", errors)
    read_plane = require_object(contract.get("read_plane"), "contract.read_plane", errors)
    require(read_plane.get("marker") == "config.json", "contract.read_plane.marker must equal config.json", errors)
    require(read_plane.get("trust_root") == "trust/root.json", "contract.read_plane.trust_root drift detected", errors)
    capabilities = require_object(contract.get("capabilities"), "contract.capabilities", errors)
    for key in (
        "append_only_index",
        "source_artifacts",
        "binary_artifacts",
        "transparency_log",
        "signed_metadata",
        "delegated_namespace_targets",
    ):
        require(capabilities.get(key) is True, f"contract.capabilities.{key} must equal true", errors)
    schemas = require_object(contract.get("schemas"), "contract.schemas", errors)
    require(schemas == EXPECTED_SCHEMA_MAP, "contract.schemas drift detected", errors)
    for schema_name, relative_path in EXPECTED_SCHEMA_MAP.items():
        schema_path = CONTRACT_DIR / relative_path
        require(schema_path.exists(), f"missing schema file for {schema_name}: {schema_path}", errors)
        payload = load_json(schema_path)
        require_object(payload, f"schema {relative_path}", errors)


def validate_envelope(name: str, payload: Any, expected_type: str, errors: list[str]) -> None:
    envelope = require_object(payload, f"examples.{name}", errors)
    signed = require_object(envelope.get("signed"), f"examples.{name}.signed", errors)
    signatures = envelope.get("signatures")
    require(isinstance(signatures, list) and len(signatures) >= 1, f"examples.{name}.signatures must be a non-empty array", errors)
    require(signed.get("type") == expected_type, f"examples.{name}.signed.type must equal {expected_type!r}", errors)
    require(signed.get("spec_version") == "1", f"examples.{name}.signed.spec_version must equal '1'", errors)


def validate_examples(examples: dict[str, Any], errors: list[str]) -> None:
    require(set(examples.keys()) == EXPECTED_EXAMPLES, "registry-v2 examples key set drift detected", errors)
    config = require_object(examples.get("config"), "examples.config", errors)
    require(config.get("protocol") == "pafio-static-registry", "examples.config.protocol drift detected", errors)
    require(config.get("protocol_version") == 2, "examples.config.protocol_version must equal 2", errors)
    validate_envelope("root", examples.get("root"), "root", errors)
    validate_envelope("timestamp", examples.get("timestamp"), "timestamp", errors)
    validate_envelope("snapshot", examples.get("snapshot"), "snapshot", errors)
    validate_envelope("namespace_targets", examples.get("namespace_targets"), "targets", errors)
    validate_envelope("checkpoint", examples.get("checkpoint"), "checkpoint", errors)

    record = require_object(examples.get("package_index_record"), "examples.package_index_record", errors)
    require(record.get("package") == "acme/util", "examples.package_index_record.package drift detected", errors)
    source_artifact = require_object(record.get("source_artifact"), "examples.package_index_record.source_artifact", errors)
    require_string(source_artifact.get("path"), "examples.package_index_record.source_artifact.path", errors)
    binaries = record.get("binary_artifacts")
    require(isinstance(binaries, list) and len(binaries) == 1, "examples.package_index_record.binary_artifacts must contain exactly one entry", errors)

    leaf = require_object(examples.get("transparency_log_leaf"), "examples.transparency_log_leaf", errors)
    require(leaf.get("sequence") == 1, "examples.transparency_log_leaf.sequence must equal 1", errors)
    require(leaf.get("package") == "acme/util", "examples.transparency_log_leaf.package drift detected", errors)


def main() -> int:
    contract = require_object(load_json(CONTRACT_PATH), "registry-v2 contract", [])
    examples = require_object(load_json(EXAMPLES_PATH), "registry-v2 examples", [])
    errors: list[str] = []
    validate_contract(contract, errors)
    validate_examples(examples, errors)
    if errors:
        sys.stderr.write("\n".join(errors) + "\n")
        return 1
    print(json.dumps({"ok": True, "contract": str(CONTRACT_PATH), "examples": str(EXAMPLES_PATH)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
