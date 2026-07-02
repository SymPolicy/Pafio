#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_PAFIO = ROOT / "scripts" / "pafio"
DEFAULT_FIXTURE_MANIFEST = ROOT / "tests" / "unit" / "fixtures" / "manifests" / "ok-single-package" / "pafio.toml"
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")
EDITION_RE = re.compile(r"^\d+$")


def run_step(name: str, command: list[str], *, env: dict[str, str] | None = None) -> dict:
    try:
        proc = subprocess.run(
            command,
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            env=env,
        )
        return {
            "name": name,
            "command": command,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "ok": proc.returncode == 0,
        }
    except OSError as err:
        return {
            "name": name,
            "command": command,
            "returncode": 127,
            "stdout": "",
            "stderr": str(err),
            "ok": False,
        }


def load_json(stdout_text: str, context: str) -> dict:
    try:
        payload = json.loads(stdout_text)
    except json.JSONDecodeError as err:
        raise RuntimeError(f"{context} did not emit valid JSON") from err
    if not isinstance(payload, dict):
        raise RuntimeError(f"{context} must emit a top-level JSON object")
    return payload


def load_json_file(path: pathlib.Path, context: str) -> dict:
    if not path.exists():
        raise RuntimeError(f"{context} is missing: {path}")
    if not path.is_file():
        raise RuntimeError(f"{context} is not a file: {path}")
    return load_json(path.read_text(encoding="utf-8"), context)


def validate_machine_info(payload: dict, *, require_compile_plan: bool) -> list[str]:
    errors: list[str] = []

    tool = payload.get("tool")
    if tool != "styio":
        errors.append("machine-info field 'tool' must equal 'styio'")

    compiler_version = payload.get("compiler_version")
    if not isinstance(compiler_version, str) or not SEMVER_RE.match(compiler_version):
        errors.append("machine-info field 'compiler_version' must be strict semver x.y.z")

    channel = payload.get("channel")
    if not isinstance(channel, str) or not channel:
        errors.append("machine-info field 'channel' must be a non-empty string")

    integration_phase = payload.get("active_integration_phase")
    if not isinstance(integration_phase, str) or not integration_phase:
        errors.append("machine-info field 'active_integration_phase' must be a non-empty string")

    supported_contracts = payload.get("supported_contracts")
    if not isinstance(supported_contracts, dict):
        errors.append("machine-info field 'supported_contracts' must be an object")
    else:
        compile_plan = supported_contracts.get("compile_plan")
        if not isinstance(compile_plan, list) or not all(isinstance(item, int) for item in compile_plan):
            errors.append("machine-info field 'supported_contracts.compile_plan' must be an array of integers")
        elif require_compile_plan and 1 not in compile_plan:
            errors.append("machine-info must advertise compile-plan v1 support when --require-compile-plan is set")

    supported_contract_versions = payload.get("supported_contract_versions")
    if not isinstance(supported_contract_versions, dict):
        errors.append("machine-info field 'supported_contract_versions' must be an object")
    elif supported_contract_versions.get("machine_info") != [1]:
        errors.append("machine-info must advertise supported_contract_versions.machine_info = [1]")

    supported_adapter_modes = payload.get("supported_adapter_modes")
    if not isinstance(supported_adapter_modes, list) or not all(isinstance(item, str) for item in supported_adapter_modes):
        errors.append("machine-info field 'supported_adapter_modes' must be an array of strings")
    elif "cli" not in supported_adapter_modes:
        errors.append("machine-info must advertise 'cli' in supported_adapter_modes")

    feature_flags = payload.get("feature_flags")
    if not isinstance(feature_flags, dict):
        errors.append("machine-info field 'feature_flags' must be an object")
    elif feature_flags.get("jsonl_diagnostics") is not True:
        errors.append("machine-info feature_flags must declare jsonl_diagnostics = true")

    capabilities = payload.get("capabilities")
    if not isinstance(capabilities, list) or not all(isinstance(item, str) for item in capabilities):
        errors.append("machine-info field 'capabilities' must be an array of strings")
    else:
        for required in ("machine_info_json", "jsonl_diagnostics"):
            if required not in capabilities:
                errors.append(f"machine-info capabilities are missing required baseline capability '{required}'")

    edition_max = payload.get("edition_max")
    if not isinstance(edition_max, str) or not EDITION_RE.match(edition_max):
        errors.append("machine-info field 'edition_max' must be a numeric string")

    return errors


def write_temp_project(root: pathlib.Path) -> pathlib.Path:
    manifest_path = root / "pafio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/interop\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = false\n\n"
        "[toolchain]\n"
        "channel = \"nightly\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"interop\"\n"
        "path = \"src/main.styio\"\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"interop\")\n", encoding="utf-8")
    return manifest_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="styio-interface-gate.py")
    parser.add_argument("--styio-bin", required=True, help="published styio binary to validate")
    parser.add_argument("--pafio-bin", default=str(DEFAULT_PAFIO), help="pafio wrapper used for black-box checks")
    parser.add_argument("--manifest-path", help="manifest used for the pafio compatibility check")
    parser.add_argument("--require-compile-plan", action="store_true", help="also validate direct compile-plan execution")
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    args = parser.parse_args(argv)

    styio_bin = str(pathlib.Path(args.styio_bin).resolve())
    pafio_bin = str(pathlib.Path(args.pafio_bin).resolve())
    check_manifest_path = pathlib.Path(args.manifest_path).resolve() if args.manifest_path else DEFAULT_FIXTURE_MANIFEST

    steps: list[dict] = []
    validation_errors: list[str] = []
    temp_root: pathlib.Path | None = None

    machine_info_step = run_step("machine_info", [styio_bin, "--machine-info=json"])
    steps.append(machine_info_step)

    machine_info_payload: dict | None = None
    if machine_info_step["ok"]:
        try:
            machine_info_payload = load_json(machine_info_step["stdout"], "styio --machine-info=json")
            validation_errors.extend(
                validate_machine_info(machine_info_payload, require_compile_plan=args.require_compile_plan)
            )
        except RuntimeError as err:
            validation_errors.append(str(err))

    compatibility_step = run_step(
        "pafio_check",
        [
            pafio_bin,
            "--json",
            "check",
            "--manifest-path",
            str(check_manifest_path),
            "--styio-bin",
            styio_bin,
        ],
        env=dict(os.environ),
    )
    steps.append(compatibility_step)

    if args.require_compile_plan:
        temp_root = pathlib.Path(tempfile.mkdtemp(prefix="pafio-styio-interface-gate-"))
        compile_manifest = write_temp_project(temp_root)
        dry_run_step = run_step(
            "pafio_build_dry_run",
            [
                pafio_bin,
                "--json",
                "build",
                "--dry-run",
                "--manifest-path",
                str(compile_manifest),
            ],
        )
        steps.append(dry_run_step)

        if dry_run_step["ok"]:
            try:
                dry_run_payload = load_json(dry_run_step["stdout"], "pafio build --dry-run")
                plan_path = pathlib.Path(dry_run_payload["plan_path"])
                build_root = pathlib.Path(dry_run_payload["build_root"])
                artifact_dir = pathlib.Path(dry_run_payload["artifact_dir"])
                diag_dir = pathlib.Path(dry_run_payload["diag_dir"])
                receipt_path = build_root / "receipt.json"
                diagnostics_path = diag_dir / "diagnostics.jsonl"

                compile_plan_step = run_step("compile_plan_execute", [styio_bin, "--compile-plan", str(plan_path)])
                steps.append(compile_plan_step)

                if compile_plan_step["ok"]:
                    for expected_dir, label in (
                        (build_root, "build_root"),
                        (artifact_dir, "artifact_dir"),
                        (diag_dir, "diag_dir"),
                    ):
                        if not expected_dir.exists() or not expected_dir.is_dir():
                            validation_errors.append(
                                f"compile-plan execution did not materialize outputs.{label}: {expected_dir}"
                            )
                    try:
                        receipt_payload = load_json_file(receipt_path, "compile-plan receipt")
                        if receipt_payload.get("intent") != dry_run_payload.get("intent"):
                            validation_errors.append(
                                f"compile-plan receipt intent mismatch: expected {dry_run_payload.get('intent')!r}, got {receipt_payload.get('intent')!r}"
                            )
                        receipt_outputs = receipt_payload.get("outputs")
                        if not isinstance(receipt_outputs, dict):
                            validation_errors.append("compile-plan receipt field 'outputs' must be an object")
                        else:
                            for key, expected_path in (
                                ("build_root", build_root),
                                ("artifact_dir", artifact_dir),
                                ("diag_dir", diag_dir),
                            ):
                                if receipt_outputs.get(key) != str(expected_path):
                                    validation_errors.append(
                                        f"compile-plan receipt outputs.{key} mismatch: expected {expected_path}, got {receipt_outputs.get(key)!r}"
                                    )
                    except RuntimeError as err:
                        validation_errors.append(str(err))

                    if not diagnostics_path.exists():
                        validation_errors.append(
                            f"compile-plan execution did not publish diagnostics artifact: {diagnostics_path}"
                        )
                    elif not diagnostics_path.is_file():
                        validation_errors.append(
                            f"compile-plan diagnostics path is not a file: {diagnostics_path}"
                        )
            except (RuntimeError, KeyError, TypeError) as err:
                validation_errors.append(f"compile-plan dry-run payload is invalid: {err}")

    ok = all(step["ok"] for step in steps) and not validation_errors

    summary = {
        "ok": ok,
        "styio_bin": styio_bin,
        "pafio_bin": pafio_bin,
        "require_compile_plan": args.require_compile_plan,
        "machine_info": machine_info_payload,
        "validation_errors": validation_errors,
        "steps": steps,
    }

    if args.json:
        sys.stdout.write(json.dumps(summary, sort_keys=True) + "\n")
    else:
        for step in steps:
            status = "OK" if step["ok"] else "FAIL"
            sys.stdout.write(f"[{status}] {step['name']}\n")
            if step["stdout"].strip():
                sys.stdout.write(step["stdout"])
                if not step["stdout"].endswith("\n"):
                    sys.stdout.write("\n")
            if step["stderr"].strip():
                sys.stderr.write(step["stderr"])
                if not step["stderr"].endswith("\n"):
                    sys.stderr.write("\n")
        for error in validation_errors:
            sys.stderr.write(f"[FAIL] {error}\n")
        sys.stdout.write(f"styio interface gate {'passed' if ok else 'failed'}\n")

    if temp_root is not None:
        shutil.rmtree(temp_root, ignore_errors=True)

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
