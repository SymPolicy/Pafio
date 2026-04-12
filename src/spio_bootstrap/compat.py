from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
import tomllib


ROOT = Path(__file__).resolve().parents[2]
COMPAT_MATRIX_PATH = ROOT / "contracts" / "compat" / "styio-support.toml"


class CompatibilityError(ValueError):
    """Raised when a compiler is outside the published compatibility window."""


class CompilerProbeError(RuntimeError):
    """Raised when the external compiler cannot be executed or queried."""


@dataclass(frozen=True)
class CompatibilityReport:
    binary: str
    compiler_version: str
    compiler_channel: str
    compiler_edition_max: str
    integration_phase: str
    supported_compile_plan_versions: list[int]
    capabilities: list[str]

    def to_dict(self) -> dict:
        return {
            "binary": self.binary,
            "compiler_version": self.compiler_version,
            "compiler_channel": self.compiler_channel,
            "compiler_edition_max": self.compiler_edition_max,
            "integration_phase": self.integration_phase,
            "supported_compile_plan_versions": self.supported_compile_plan_versions,
            "capabilities": self.capabilities,
        }


def resolve_styio_binary(explicit_path: str | None) -> Path | None:
    candidate = explicit_path or os.getenv("SPIO_STYIO_BIN")
    if not candidate:
        return None
    return Path(candidate)


def _parse_semver(text: str) -> tuple[int, int, int]:
    try:
        major, minor, patch = text.split(".")
        return int(major), int(minor), int(patch)
    except Exception as err:  # pragma: no cover - defensive guard
        raise CompatibilityError(f"invalid compiler version in handshake: {text}") from err


def _parse_edition(text: str) -> int:
    try:
        return int(text)
    except Exception as err:  # pragma: no cover - defensive guard
        raise CompatibilityError(f"invalid compiler edition_max in handshake: {text}") from err


def _load_compat_matrix() -> dict:
    return tomllib.loads(COMPAT_MATRIX_PATH.read_text())


def _probe_machine_info(binary: Path) -> dict:
    try:
        proc = subprocess.run(
            [str(binary), "--machine-info=json"],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as err:
        raise CompilerProbeError(f"failed to execute compiler '{binary}': {err}") from err

    if proc.returncode != 0:
        stderr = proc.stderr.strip() or proc.stdout.strip()
        raise CompilerProbeError(
            f"compiler '{binary}' rejected --machine-info=json"
            + (f": {stderr}" if stderr else "")
        )

    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError as err:
        raise CompatibilityError(f"compiler '{binary}' returned invalid machine-info JSON") from err

    required_fields = ("tool", "compiler_version", "channel", "supported_contracts", "capabilities", "edition_max")
    for field in required_fields:
        if field not in payload:
            raise CompatibilityError(f"compiler handshake missing required field: {field}")
    if not isinstance(payload["supported_contracts"], dict):
        raise CompatibilityError("compiler handshake field 'supported_contracts' must be an object")
    if not isinstance(payload["capabilities"], list):
        raise CompatibilityError("compiler handshake field 'capabilities' must be an array")
    if payload["tool"] != "styio":
        raise CompatibilityError("compiler handshake field 'tool' must equal 'styio'")

    return payload


def _find_matching_entry(machine_info: dict, compat_doc: dict) -> dict:
    compiler_version = _parse_semver(machine_info["compiler_version"])
    compiler_channel = machine_info["channel"]

    for entry in compat_doc.get("supported_styio", []):
        min_version = _parse_semver(entry["min"])
        max_version = _parse_semver(entry["max_exclusive"])
        if not (min_version <= compiler_version < max_version):
            continue
        if entry.get("channel") != compiler_channel:
            continue
        return entry

    raise CompatibilityError(
        "compiler version/channel is outside the published spio compatibility matrix"
    )


def check_compiler_compatibility(binary: Path) -> CompatibilityReport:
    machine_info = _probe_machine_info(binary)
    compat_doc = _load_compat_matrix()
    entry = _find_matching_entry(machine_info, compat_doc)

    compiler_capabilities = set(machine_info["capabilities"])
    required_capabilities = list(entry.get("required_capabilities", []))
    missing_capabilities = [cap for cap in required_capabilities if cap not in compiler_capabilities]
    if missing_capabilities:
        raise CompatibilityError(
            "compiler handshake is missing required capabilities: " + ", ".join(missing_capabilities)
        )

    supported_compile_plan_versions = machine_info["supported_contracts"].get("compile_plan", [])
    expected_compile_plan_versions = list(entry.get("supported_compile_plan_versions", []))
    if expected_compile_plan_versions and not all(
        version in supported_compile_plan_versions for version in expected_compile_plan_versions
    ):
        raise CompatibilityError(
            "compiler handshake does not provide the compile-plan versions required by this spio phase"
        )

    compiler_edition = _parse_edition(machine_info["edition_max"])
    supported_edition = _parse_edition(entry["edition_max"])
    if compiler_edition < supported_edition:
        raise CompatibilityError(
            "compiler edition_max is lower than the minimum edition supported by this spio phase"
        )

    return CompatibilityReport(
        binary=str(binary.resolve()),
        compiler_version=machine_info["compiler_version"],
        compiler_channel=machine_info["channel"],
        compiler_edition_max=machine_info["edition_max"],
        integration_phase=entry.get("integration_phase", "unspecified"),
        supported_compile_plan_versions=expected_compile_plan_versions,
        capabilities=sorted(compiler_capabilities),
    )
