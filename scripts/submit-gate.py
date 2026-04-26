#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import time
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[1]
CHECK_NO_BINARIES = ROOT / "scripts" / "check_no_binaries.py"
REPO_HYGIENE = ROOT / "scripts" / "repo-hygiene-check.py"
DOCS_AUDIT = ROOT / "scripts" / "docs-audit.py"
NATIVE_CHECK = ROOT / "scripts" / "native-check.sh"
EXTRACTABILITY_CHECK = ROOT / "scripts" / "extractability-check.sh"
PERF_GATE = ROOT / "scripts" / "perf-gate.py"
DELIVERY_GATE = ROOT / "scripts" / "delivery-gate.py"
STYIO_INTERFACE_GATE = ROOT / "scripts" / "styio-interface-gate.py"
ECOSYSTEM_CLI_DOC_GATE = ROOT / "scripts" / "ecosystem-cli-doc-gate.py"
DEFAULT_FEATURE_CONFIG = ROOT / "scripts" / "submit-gate.features.json"
DEFAULT_FEATURE_FLAGS = {
    "enable_release_profile": False,
    "enable_styio_compatibility": False,
    "enable_cloud_registry_checks": False,
}


@dataclass(frozen=True)
class Step:
    name: str
    command: list[str]
    cwd: pathlib.Path


def run_step(step: Step) -> dict:
    started = time.perf_counter()
    proc = subprocess.run(step.command, cwd=step.cwd, capture_output=True, text=True)
    duration_ms = (time.perf_counter() - started) * 1000.0
    return {
        "name": step.name,
        "command": step.command,
        "cwd": str(step.cwd),
        "returncode": proc.returncode,
        "duration_ms": round(duration_ms, 3),
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "ok": proc.returncode == 0,
    }


def load_feature_flags(config_path: pathlib.Path) -> tuple[dict[str, bool], list[str]]:
    warnings: list[str] = []
    flags = dict(DEFAULT_FEATURE_FLAGS)
    if not config_path.exists():
        warnings.append(
            f"feature config not found ({config_path}); release/styio/cloud features are disabled by default"
        )
        return flags, warnings

    try:
        payload = json.loads(config_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        warnings.append(
            f"feature config is invalid JSON ({config_path}); release/styio/cloud features remain disabled"
        )
        return flags, warnings
    if not isinstance(payload, dict):
        warnings.append(
            f"feature config must be a JSON object ({config_path}); release/styio/cloud features remain disabled"
        )
        return flags, warnings

    for key, default_value in DEFAULT_FEATURE_FLAGS.items():
        if key not in payload:
            warnings.append(f"feature flag '{key}' is missing in {config_path}; keeping default={default_value}")
            continue
        value = payload[key]
        if not isinstance(value, bool):
            warnings.append(
                f"feature flag '{key}' in {config_path} must be boolean; keeping default={default_value}"
            )
            continue
        flags[key] = value
    return flags, warnings


def gate_steps(profile: str) -> list[Step]:
    steps = [
        Step(
            "quality_no_binaries",
            [sys.executable, str(CHECK_NO_BINARIES), "--repo-root", str(ROOT), "--mode", "tracked"],
            ROOT,
        ),
        Step(
            "quality_repo_hygiene",
            [sys.executable, str(REPO_HYGIENE), "--repo-root", str(ROOT), "--mode", "tracked"],
            ROOT,
        ),
        Step("quality_docs_governance", [sys.executable, str(DOCS_AUDIT)], ROOT),
        Step(
            "quality_ecosystem_cli_docs",
            [sys.executable, str(ECOSYSTEM_CLI_DOC_GATE), "--json"],
            ROOT,
        ),
        Step("regression_native_check", [str(NATIVE_CHECK)], ROOT),
        Step("regression_extractability", [str(EXTRACTABILITY_CHECK)], ROOT),
        Step("performance_baseline", [sys.executable, str(PERF_GATE)], ROOT),
        Step("delivery_package", [sys.executable, str(DELIVERY_GATE)], ROOT),
    ]
    return steps


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="submit-gate.py")
    parser.add_argument("--profile", choices=["pre-push", "ci", "release"], default="pre-push")
    parser.add_argument("--styio-bin", help="optional styio binary for release compatibility checks")
    parser.add_argument(
        "--feature-config",
        type=pathlib.Path,
        default=DEFAULT_FEATURE_CONFIG,
        help="feature-flag JSON for release/styio/cloud gates",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    feature_flags, warnings = load_feature_flags(args.feature_config.resolve())
    steps = gate_steps(args.profile)

    if args.profile == "release" and not feature_flags["enable_release_profile"]:
        warnings.append("release profile extra checks are disabled by feature flags")
    elif args.profile == "release":
        if args.styio_bin and feature_flags["enable_styio_compatibility"]:
            steps.append(
                Step(
                    "styio_compatibility",
                    [sys.executable, str(STYIO_INTERFACE_GATE), "--styio-bin", args.styio_bin, "--json"],
                    ROOT,
                )
            )
        elif args.styio_bin and not feature_flags["enable_styio_compatibility"]:
            warnings.append(
                "styio compatibility check requested but disabled by feature flags; skipping styio_compatibility"
            )
        elif not args.styio_bin:
            warnings.append("release profile did not provide --styio-bin; skipping styio_compatibility")

        if not feature_flags["enable_cloud_registry_checks"]:
            warnings.append("cloud registry checks are disabled by feature flags")
    elif args.styio_bin:
        warnings.append("--styio-bin is ignored unless --profile release is used")

    results: list[dict] = []
    for step in steps:
        result = run_step(step)
        results.append(result)
        if not result["ok"]:
            break

    ok = all(result["ok"] for result in results)
    returncode = 0 if ok else 1
    payload = {
        "ok": ok,
        "profile": args.profile,
        "returncode": returncode,
        "feature_config": str(args.feature_config.resolve()),
        "feature_flags": feature_flags,
        "warnings": warnings,
        "steps": results,
    }

    if args.json:
        sys.stdout.write(json.dumps(payload, sort_keys=True) + "\n")
    else:
        for result in results:
            status = "OK" if result["ok"] else "FAIL"
            sys.stdout.write(f"[{status}] {result['name']} ({result['duration_ms']} ms)\n")
            if result["stdout"].strip():
                sys.stdout.write(result["stdout"])
                if not result["stdout"].endswith("\n"):
                    sys.stdout.write("\n")
            if result["stderr"].strip():
                sys.stderr.write(result["stderr"])
                if not result["stderr"].endswith("\n"):
                    sys.stderr.write("\n")
        for warning in warnings:
            sys.stdout.write(f"[WARN] {warning}\n")
        sys.stdout.write(f"submit gate {'passed' if ok else 'failed'} for profile={args.profile}\n")
    return returncode


if __name__ == "__main__":
    raise SystemExit(main())
