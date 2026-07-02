#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_PAFIO = ROOT / "scripts" / "pafio"


def run_step(
    name: str,
    command: list[str],
    *,
    env: dict[str, str] | None = None,
    cwd: pathlib.Path | None = None,
) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            command,
            cwd=str(cwd or ROOT),
            capture_output=True,
            text=True,
            env=env,
        )
        return {
            "name": name,
            "kind": "command",
            "command": command,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "ok": proc.returncode == 0,
        }
    except OSError as err:
        return {
            "name": name,
            "kind": "command",
            "command": command,
            "returncode": 127,
            "stdout": "",
            "stderr": str(err),
            "ok": False,
        }


def run_action(
    name: str,
    *,
    action: str,
    message: str,
) -> dict[str, Any]:
    return {
        "name": name,
        "kind": "action",
        "command": [action],
        "returncode": 0,
        "stdout": message,
        "stderr": "",
        "ok": True,
    }


def load_json(text: str, context: str) -> dict[str, Any]:
    try:
        payload = json.loads(text)
    except json.JSONDecodeError as err:
        raise RuntimeError(f"{context} did not emit valid JSON") from err
    if not isinstance(payload, dict):
        raise RuntimeError(f"{context} must emit a top-level JSON object")
    return payload


def load_step_json(step: dict[str, Any], context: str) -> dict[str, Any]:
    primary = step["stdout"] if str(step["stdout"]).strip() else step["stderr"]
    return load_json(primary, context)


def ensure_command(command: list[str] | Any) -> list[str]:
    if not isinstance(command, list) or not all(isinstance(item, str) for item in command):
        raise RuntimeError("step command must resolve to a list[str]")
    return command


def canonical(path: pathlib.Path) -> str:
    return str(path.resolve())


def run_git(repo_root: pathlib.Path, *args: str) -> str:
    proc = subprocess.run(
        ["git", *args],
        cwd=str(repo_root.parent if args and args[0] == "init" else repo_root),
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "git command failed")
    return proc.stdout.strip()


def write_executable(path: pathlib.Path, content: str) -> pathlib.Path:
    path.write_text(content, encoding="utf-8")
    path.chmod(0o755)
    return path


def create_machine_info_alias(
    path: pathlib.Path,
    *,
    delegate_binary: str,
    version: str,
    channel: str,
) -> pathlib.Path:
    proc = subprocess.run(
        [delegate_binary, "--machine-info=json"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "could not probe delegate styio machine-info: "
            + (proc.stderr.strip() or proc.stdout.strip() or f"exit {proc.returncode}")
        )
    payload = load_json(proc.stdout, "delegate machine-info")
    payload["compiler_version"] = version
    payload["channel"] = channel
    payload["active_integration_phase"] = "compile-plan-live"
    supported_contracts = payload.get("supported_contracts")
    if isinstance(supported_contracts, dict):
        supported_contracts["compile_plan"] = [1]
        supported_contracts["runtime_events"] = [1]
    supported_contract_versions = payload.get("supported_contract_versions")
    if isinstance(supported_contract_versions, dict):
        supported_contract_versions["compile_plan"] = [1]
        supported_contract_versions["runtime_events"] = [1]
    feature_flags = payload.get("feature_flags")
    if isinstance(feature_flags, dict):
        feature_flags["compile_plan_consumer"] = True
        feature_flags["project_execution_via_compile_plan"] = True
        feature_flags["runtime_event_stream"] = True
    script = (
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--machine-info=json\" ]; then\n"
        f"  printf '%s\\n' {shlex.quote(json.dumps(payload, sort_keys=True))}\n"
        "  exit 0\n"
        "fi\n"
        f"exec {shlex.quote(delegate_binary)} \"$@\"\n"
    )
    return write_executable(path, script)


def create_workspace_git_repo(repo_root: pathlib.Path, util_version: str) -> str:
    (repo_root / "packages" / "feed" / "src").mkdir(parents=True, exist_ok=True)
    (repo_root / "packages" / "util" / "src").mkdir(parents=True, exist_ok=True)
    (repo_root / "pafio.toml").write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[workspace]\n"
        "members = [\"packages/feed\", \"packages/util\"]\n"
        "resolver = \"1\"\n",
        encoding="utf-8",
    )
    (repo_root / "packages" / "feed" / "pafio.toml").write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/feed\"\n"
        "version = \"1.2.0\"\n"
        "edition = \"2026\"\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[lib]\n"
        "path = \"src/lib.styio\"\n\n"
        "[dependencies]\n"
        "util = { package = \"acme/util\", path = \"../util\" }\n",
        encoding="utf-8",
    )
    (repo_root / "packages" / "feed" / "src" / "lib.styio").write_text(
        "// feed fixture\n",
        encoding="utf-8",
    )
    (repo_root / "packages" / "util" / "pafio.toml").write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/util\"\n"
        f"version = \"{util_version}\"\n"
        "edition = \"2026\"\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[lib]\n"
        "path = \"src/lib.styio\"\n",
        encoding="utf-8",
    )
    (repo_root / "packages" / "util" / "src" / "lib.styio").write_text(
        "// util fixture\n",
        encoding="utf-8",
    )
    run_git(repo_root, "init", "--initial-branch=main", repo_root.name)
    run_git(
        repo_root,
        "-c",
        "user.email=styio-gate@example.com",
        "-c",
        "user.name=styio-gate",
        "add",
        ".",
    )
    run_git(
        repo_root,
        "-c",
        "user.email=styio-gate@example.com",
        "-c",
        "user.name=styio-gate",
        "commit",
        "--quiet",
        "-m",
        "initial",
    )
    return run_git(repo_root, "rev-parse", "HEAD")


def write_registry_publish_package(root: pathlib.Path) -> pathlib.Path:
    manifest_path = root / "pafio.toml"
    source_path = root / "src" / "lib.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/registry-feed\"\n"
        "version = \"0.2.0\"\n"
        "edition = \"2026\"\n"
        "publish = true\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[lib]\n"
        "path = \"src/lib.styio\"\n",
        encoding="utf-8",
    )
    source_path.write_text("// registry-feed fixture\n", encoding="utf-8")
    return manifest_path


def write_registry_consumer_project(root: pathlib.Path, fetch_root: str) -> pathlib.Path:
    manifest_path = root / "pafio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/registry-client\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"client\"\n"
        "path = \"src/main.styio\"\n\n"
        "[dependencies]\n"
        f"registry_feed = {{ package = \"acme/registry-feed\", version = \"0.2.0\", registry = \"{fetch_root}\" }}\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"registry-client\")\n", encoding="utf-8")
    return manifest_path


def write_single_package_project(root: pathlib.Path) -> dict[str, Any]:
    manifest_path = root / "pafio.toml"
    source_path = root / "src" / "main.styio"
    test_path = root / "tests" / "smoke.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    test_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/sample\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = true\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"sample\"\n"
        "path = \"src/main.styio\"\n\n"
        "[[test]]\n"
        "name = \"smoke\"\n"
        "path = \"tests/smoke.styio\"\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"sample\")\n", encoding="utf-8")
    test_path.write_text(">_(\"smoke\")\n", encoding="utf-8")
    return {
        "name": "single-package-managed-toolchain",
        "manifest_path": manifest_path,
        "workspace_root": root,
        "package_count": 1,
        "selected_package": "acme/sample",
        "selected_package_id": "workspace:acme/sample@0.1.0",
        "run_target_name": "sample",
        "test_target_name": "smoke",
        "publish_package_root": root,
    }


def write_workspace_project(root: pathlib.Path) -> dict[str, Any]:
    root_manifest = root / "pafio.toml"
    app_manifest = root / "packages" / "app" / "pafio.toml"
    tool_manifest = root / "packages" / "tool" / "pafio.toml"
    app_source = root / "packages" / "app" / "src" / "main.styio"
    app_test = root / "packages" / "app" / "tests" / "smoke.styio"
    tool_source = root / "packages" / "tool" / "src" / "main.styio"
    tool_test = root / "packages" / "tool" / "tests" / "smoke.styio"
    app_source.parent.mkdir(parents=True, exist_ok=True)
    app_test.parent.mkdir(parents=True, exist_ok=True)
    tool_source.parent.mkdir(parents=True, exist_ok=True)
    tool_test.parent.mkdir(parents=True, exist_ok=True)
    root_manifest.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[workspace]\n"
        "members = [\"packages/app\", \"packages/tool\"]\n"
        "resolver = \"1\"\n",
        encoding="utf-8",
    )
    app_manifest.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/app\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = true\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"app\"\n"
        "path = \"src/main.styio\"\n\n"
        "[[test]]\n"
        "name = \"smoke\"\n"
        "path = \"tests/smoke.styio\"\n",
        encoding="utf-8",
    )
    tool_manifest.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/tool\"\n"
        "version = \"0.2.0\"\n"
        "edition = \"2026\"\n"
        "publish = true\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"tool\"\n"
        "path = \"src/main.styio\"\n\n"
        "[[test]]\n"
        "name = \"smoke\"\n"
        "path = \"tests/smoke.styio\"\n",
        encoding="utf-8",
    )
    app_source.write_text(">_(\"app\")\n", encoding="utf-8")
    app_test.write_text(">_(\"app-smoke\")\n", encoding="utf-8")
    tool_source.write_text(">_(\"tool\")\n", encoding="utf-8")
    tool_test.write_text(">_(\"tool-smoke\")\n", encoding="utf-8")
    return {
        "name": "workspace-package-selection",
        "manifest_path": root_manifest,
        "workspace_root": root,
        "package_count": 2,
        "selected_package": "acme/tool",
        "selected_package_id": "workspace:acme/tool@0.2.0",
        "run_target_name": "tool",
        "test_target_name": "smoke",
        "publish_package_root": root / "packages" / "tool",
    }


def write_offline_project(root: pathlib.Path) -> dict[str, Any]:
    git_repo = root / "remote-feed"
    rev = create_workspace_git_repo(git_repo, "0.9.0")
    manifest_path = root / "pafio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[pafio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/app\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = false\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"app\"\n"
        "path = \"src/main.styio\"\n\n"
        "[dependencies]\n"
        f"feed = {{ package = \"acme/feed\", git = \"{canonical(git_repo)}\", rev = \"{rev}\" }}\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"app\")\n", encoding="utf-8")
    return {
        "name": "offline-vendored-git",
        "manifest_path": manifest_path,
        "workspace_root": root,
        "package_count": 3,
        "selected_package": "acme/app",
        "selected_package_id": "workspace:acme/app@0.1.0",
        "run_target_name": "app",
        "publish_package_root": root,
        "expected_git_packages": 2,
        "expected_registry_packages": 0,
    }


def write_registry_project(root: pathlib.Path) -> dict[str, Any]:
    registry_root = root / "registry-root"
    registry_root.mkdir(parents=True, exist_ok=True)
    fetch_root = registry_root.resolve().as_uri()
    publish_manifest_path = write_registry_publish_package(root / "publish")
    consumer_manifest_path = write_registry_consumer_project(root / "consume", fetch_root)
    return {
        "name": "registry-hosted-source",
        "manifest_path": consumer_manifest_path,
        "workspace_root": consumer_manifest_path.parent,
        "package_count": 2,
        "selected_package": "acme/registry-client",
        "selected_package_id": "workspace:acme/registry-client@0.1.0",
        "run_target_name": "client",
        "publish_manifest_path": publish_manifest_path,
        "publish_package_root": publish_manifest_path.parent,
        "registry_publish_root": registry_root,
        "registry_fetch_root": fetch_root,
        "registry_dependency_package": "acme/registry-feed",
        "registry_dependency_version": "0.2.0",
    }


def validate_check_payload(
    payload: dict[str, Any],
    *,
    expected_package_count: int,
    expected_offline: bool,
    expected_binary: str | None = None,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "check":
        errors.append("check payload field 'command' must equal 'check'")
    if payload.get("compiler_checked") is not True:
        errors.append("check payload field 'compiler_checked' must equal true")
    if payload.get("packages") != expected_package_count:
        errors.append(
            f"check payload field 'packages' must equal {expected_package_count}"
        )
    if payload.get("offline") is not expected_offline:
        errors.append(f"check payload field 'offline' must equal {expected_offline}")
    styio_payload = payload.get("styio")
    if not isinstance(styio_payload, dict):
        errors.append("check payload field 'styio' must be an object")
    else:
        if styio_payload.get("integration_phase") != "compile-plan-live":
            errors.append("check payload styio.integration_phase must equal 'compile-plan-live'")
        if expected_binary is not None and styio_payload.get("binary") != expected_binary:
            errors.append(
                f"check payload styio.binary must equal {expected_binary!r}"
            )
    return errors


def validate_fetch_payload(
    payload: dict[str, Any],
    *,
    expected_package_count: int,
    expected_offline: bool,
    expected_git_packages: int | None = None,
    expected_registry_packages: int | None = None,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "fetch":
        errors.append("fetch payload field 'command' must equal 'fetch'")
    if payload.get("packages") != expected_package_count:
        errors.append(
            f"fetch payload field 'packages' must equal {expected_package_count}"
        )
    if payload.get("offline") is not expected_offline:
        errors.append(f"fetch payload field 'offline' must equal {expected_offline}")
    if expected_git_packages is not None and payload.get("git_packages") != expected_git_packages:
        errors.append(
            f"fetch payload field 'git_packages' must equal {expected_git_packages}"
        )
    if (
        expected_registry_packages is not None
        and payload.get("registry_packages") != expected_registry_packages
    ):
        errors.append(
            f"fetch payload field 'registry_packages' must equal {expected_registry_packages}"
        )
    return errors


def validate_vendor_payload(
    payload: dict[str, Any],
    *,
    workspace_root: pathlib.Path,
    expected_offline: bool,
    expected_package_count: int,
    expected_git_snapshots: int | None = None,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "vendor":
        errors.append("vendor payload field 'command' must equal 'vendor'")
    if payload.get("packages") != expected_package_count:
        errors.append(
            f"vendor payload field 'packages' must equal {expected_package_count}"
        )
    if payload.get("offline") is not expected_offline:
        errors.append(f"vendor payload field 'offline' must equal {expected_offline}")
    vendor_root = payload.get("vendor_root")
    metadata_path = payload.get("metadata_path")
    expected_vendor_root = str(workspace_root / ".pafio" / "vendor")
    expected_metadata_path = str(workspace_root / ".pafio" / "vendor" / "pafio-vendor.json")
    if vendor_root != expected_vendor_root:
        errors.append(
            f"vendor payload field 'vendor_root' must equal {expected_vendor_root!r}"
        )
    if metadata_path != expected_metadata_path:
        errors.append(
            f"vendor payload field 'metadata_path' must equal {expected_metadata_path!r}"
        )
    if not pathlib.Path(expected_metadata_path).is_file():
        errors.append(f"vendor metadata file is missing: {expected_metadata_path}")
    if expected_git_snapshots is not None and payload.get("git_snapshots") != expected_git_snapshots:
        errors.append(
            f"vendor payload field 'git_snapshots' must equal {expected_git_snapshots}"
        )
    return errors


def validate_execution_payload(
    payload: dict[str, Any],
    *,
    command_name: str,
    intent: str,
    target_kind: str,
    target_name: str,
    expected_package: str,
    expected_package_id: str,
    expected_package_count: int,
    expected_workspace_root: pathlib.Path,
    expected_offline: bool,
    expected_binary: str | None = None,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != command_name:
        errors.append(f"{command_name} payload field 'command' must equal {command_name!r}")
    if payload.get("mode") != "execute":
        errors.append(f"{command_name} payload field 'mode' must equal 'execute'")
    if payload.get("workflow_payload_version") != 1:
        errors.append(f"{command_name} payload field 'workflow_payload_version' must equal 1")
    if payload.get("intent") != intent:
        errors.append(f"{command_name} payload field 'intent' must equal {intent!r}")
    if payload.get("packages") != expected_package_count:
        errors.append(
            f"{command_name} payload field 'packages' must equal {expected_package_count}"
        )
    if payload.get("workspace_root") != str(expected_workspace_root):
        errors.append(
            f"{command_name} payload field 'workspace_root' must equal {str(expected_workspace_root)!r}"
        )
    if payload.get("offline") is not expected_offline:
        errors.append(
            f"{command_name} payload field 'offline' must equal {expected_offline}"
        )
    entry = payload.get("entry")
    if not isinstance(entry, dict):
        errors.append(f"{command_name} payload field 'entry' must be an object")
    else:
        if entry.get("package") != expected_package:
            errors.append(
                f"{command_name} payload entry.package must equal {expected_package!r}"
            )
        if entry.get("package_id") != expected_package_id:
            errors.append(
                f"{command_name} payload entry.package_id must equal {expected_package_id!r}"
            )
        if entry.get("target_kind") != target_kind:
            errors.append(
                f"{command_name} payload entry.target_kind must equal {target_kind!r}"
            )
        if entry.get("target_name") != target_name:
            errors.append(
                f"{command_name} payload entry.target_name must equal {target_name!r}"
            )
    receipt = payload.get("receipt")
    if not isinstance(receipt, dict):
        errors.append(f"{command_name} payload field 'receipt' must be an object")
    else:
        if receipt.get("intent") != intent:
            errors.append(f"{command_name} receipt intent must equal {intent!r}")
        if receipt.get("executed") is not True:
            errors.append(f"{command_name} receipt executed must equal true")
        receipt_entry = receipt.get("entry")
        if not isinstance(receipt_entry, dict):
            errors.append(f"{command_name} receipt entry must be an object")
        else:
            if receipt_entry.get("package_id") != expected_package_id:
                errors.append(
                    f"{command_name} receipt entry.package_id must equal {expected_package_id!r}"
                )
            if receipt_entry.get("target_kind") != target_kind:
                errors.append(
                    f"{command_name} receipt entry.target_kind must equal {target_kind!r}"
                )
            if receipt_entry.get("target_name") != target_name:
                errors.append(
                    f"{command_name} receipt entry.target_name must equal {target_name!r}"
                )
        outputs = receipt.get("outputs")
        if not isinstance(outputs, dict):
            errors.append(f"{command_name} receipt outputs must be an object")
        else:
            runtime_events_path = outputs.get("runtime_events_path")
            if not isinstance(runtime_events_path, str) or not pathlib.Path(runtime_events_path).is_file():
                errors.append(
                    f"{command_name} receipt outputs.runtime_events_path must reference an existing file"
                )
    diagnostics = payload.get("diagnostics")
    if not isinstance(diagnostics, list):
        errors.append(f"{command_name} payload field 'diagnostics' must be an array")
    elif diagnostics:
        errors.append(f"{command_name} payload diagnostics must be empty for the sample workflow")
    runtime_events = payload.get("runtime_events")
    if not isinstance(runtime_events, list) or not runtime_events:
        errors.append(f"{command_name} payload field 'runtime_events' must be a non-empty array")
    else:
        event_kinds = {event.get("eventKind") for event in runtime_events if isinstance(event, dict)}
        required = {
            "compile.started",
            "unit.entered",
            "run.started",
            "run.finished",
            "unit.exited",
            "compile.finished",
        }
        if target_kind == "test":
            required |= {"unit.test.started", "unit.test.finished"}
        missing = sorted(kind for kind in required if kind not in event_kinds)
        if missing:
            errors.append(
                f"{command_name} payload runtime_events are missing required event kinds: {', '.join(missing)}"
            )
    diagnostics_path = payload.get("diagnostics_path")
    if not isinstance(diagnostics_path, str) or not pathlib.Path(diagnostics_path).is_file():
        errors.append(f"{command_name} payload diagnostics_path must reference an existing file")
    receipt_path = payload.get("receipt_path")
    if not isinstance(receipt_path, str) or not pathlib.Path(receipt_path).is_file():
        errors.append(f"{command_name} payload receipt_path must reference an existing file")
    runtime_events_path = payload.get("runtime_events_path")
    if not isinstance(runtime_events_path, str) or not pathlib.Path(runtime_events_path).is_file():
        errors.append(
            f"{command_name} payload runtime_events_path must reference an existing file"
        )
    styio_payload = payload.get("styio")
    if not isinstance(styio_payload, dict):
        errors.append(f"{command_name} payload field 'styio' must be an object")
    else:
        if styio_payload.get("integration_phase") != "compile-plan-live":
            errors.append(f"{command_name} payload styio.integration_phase must equal 'compile-plan-live'")
        if expected_binary is not None and styio_payload.get("binary") != expected_binary:
            errors.append(
                f"{command_name} payload styio.binary must equal {expected_binary!r}"
            )
    return errors


def validate_publish_payload(
    payload: dict[str, Any],
    *,
    expected_package: str,
    expected_package_root: pathlib.Path,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "publish":
        errors.append("publish payload field 'command' must equal 'publish'")
    if payload.get("mode") != "dry-run":
        errors.append("publish payload field 'mode' must equal 'dry-run'")
    if payload.get("package") != expected_package:
        errors.append(f"publish payload field 'package' must equal {expected_package!r}")
    if payload.get("package_root") != str(expected_package_root):
        errors.append(
            f"publish payload field 'package_root' must equal {str(expected_package_root)!r}"
        )
    expected_manifest_path = str(expected_package_root / "pafio.toml")
    if payload.get("manifest_path") != expected_manifest_path:
        errors.append(
            f"publish payload field 'manifest_path' must equal {expected_manifest_path!r}"
        )
    archive_path = payload.get("archive_path")
    if not isinstance(archive_path, str):
        errors.append("publish payload field 'archive_path' must be a string")
    else:
        archive_file = pathlib.Path(archive_path)
        if not archive_file.is_file():
            errors.append(f"publish archive is missing: {archive_path}")
        expected_dist = expected_package_root / "dist"
        if archive_file.parent != expected_dist:
            errors.append(
                f"publish archive parent must equal {expected_dist}, got {archive_file.parent}"
            )
    return errors


def validate_registry_publish_payload(
    payload: dict[str, Any],
    *,
    registry_root: pathlib.Path,
    expected_package_root: pathlib.Path,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "publish":
        errors.append("registry publish payload field 'command' must equal 'publish'")
    if payload.get("mode") != "publish":
        errors.append("registry publish payload field 'mode' must equal 'publish'")
    if payload.get("transport") != "filesystem":
        errors.append("registry publish payload field 'transport' must equal 'filesystem'")
    if payload.get("package") != "acme/registry-feed":
        errors.append("registry publish payload field 'package' must equal 'acme/registry-feed'")
    if payload.get("version") != "0.2.0":
        errors.append("registry publish payload field 'version' must equal '0.2.0'")
    if payload.get("package_root") != str(expected_package_root):
        errors.append(
            f"registry publish payload field 'package_root' must equal {str(expected_package_root)!r}"
        )
    if payload.get("registry_root") != str(registry_root):
        errors.append(
            f"registry publish payload field 'registry_root' must equal {str(registry_root)!r}"
        )
    for key in ["archive_path", "registry_marker_path", "registry_blob_path", "registry_entry_path"]:
        value = payload.get(key)
        if not isinstance(value, str) or not pathlib.Path(value).exists():
            errors.append(f"registry publish payload field {key!r} must reference an existing path")
    return errors


def validate_project_graph_registry_payload(
    payload: dict[str, Any],
    *,
    manifest_path: pathlib.Path,
    expected_registry_root: str,
    expected_dependency_package: str,
    expected_dependency_version: str,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "project-graph":
        errors.append("project-graph payload field 'command' must equal 'project-graph'")
    if payload.get("manifest_path") != str(manifest_path):
        errors.append(
            f"project-graph payload field 'manifest_path' must equal {str(manifest_path)!r}"
        )
    dependencies = payload.get("dependencies")
    if not isinstance(dependencies, list) or not dependencies:
        errors.append("project-graph payload field 'dependencies' must be a non-empty array")
    else:
        runtime_registry = next(
            (
                item
                for item in dependencies
                if isinstance(item, dict)
                and item.get("source_kind") == "registry"
                and item.get("package") == expected_dependency_package
            ),
            None,
        )
        if runtime_registry is None:
            errors.append("project-graph payload must contain the registry dependency entry")
        else:
            if runtime_registry.get("registry") != expected_registry_root:
                errors.append(
                    f"project-graph registry dependency root must equal {expected_registry_root!r}"
                )
            if runtime_registry.get("version") != expected_dependency_version:
                errors.append(
                    f"project-graph registry dependency version must equal {expected_dependency_version!r}"
                )
    distribution = payload.get("package_distribution")
    if not isinstance(distribution, dict):
        errors.append("project-graph payload field 'package_distribution' must be an object")
    else:
        registry_sources = distribution.get("registry_sources")
        if not isinstance(registry_sources, list) or not registry_sources:
            errors.append("project-graph package_distribution.registry_sources must be non-empty")
        else:
            entry = next(
                (
                    item
                    for item in registry_sources
                    if isinstance(item, dict) and item.get("registry_root") == expected_registry_root
                ),
                None,
            )
            if entry is None:
                errors.append("project-graph package_distribution must include the registry root")
            elif entry.get("transport") != "file":
                errors.append("project-graph registry source transport must equal 'file'")
    source_state = payload.get("source_state")
    if not isinstance(source_state, dict):
        errors.append("project-graph payload field 'source_state' must be an object")
    else:
        if source_state.get("declared_registry_dependencies") != 1:
            errors.append("project-graph source_state.declared_registry_dependencies must equal 1")
        registry_cache = source_state.get("registry_cache")
        if not isinstance(registry_cache, dict):
            errors.append("project-graph source_state.registry_cache must be an object")
        else:
            if not any(
                registry_cache.get(key) is True
                for key in ["index_present", "blobs_present", "checkouts_present"]
            ):
                errors.append(
                    "project-graph source_state.registry_cache must materialize at least one registry cache surface"
                )
    return errors


def validate_expected_error_payload(
    payload: dict[str, Any],
    *,
    command_name: str,
    category: str,
    message_fragment: str,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != command_name:
        errors.append(
            f"{command_name} expected-failure payload field 'command' must equal {command_name!r}"
        )
    if payload.get("category") != category:
        errors.append(
            f"{command_name} expected-failure payload field 'category' must equal {category!r}"
        )
    message = payload.get("message")
    if not isinstance(message, str) or message_fragment not in message:
        errors.append(
            f"{command_name} expected-failure payload message must contain {message_fragment!r}"
        )
    return errors


def validate_tool_install_payload(
    payload: dict[str, Any],
    *,
    styio_bin: str,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "tool install":
        errors.append("tool install payload field 'command' must equal 'tool install'")
    if payload.get("source_binary") != styio_bin:
        errors.append(f"tool install payload source_binary must equal {styio_bin!r}")
    if payload.get("integration_phase") != "compile-plan-live":
        errors.append("tool install payload integration_phase must equal 'compile-plan-live'")
    for key in [
        "install_binary_path",
        "install_metadata_path",
        "managed_binary_path",
        "current_metadata_path",
    ]:
        value = payload.get(key)
        if not isinstance(value, str) or not pathlib.Path(value).exists():
            errors.append(f"tool install payload field {key!r} must reference an existing path")
    if not isinstance(payload.get("compiler_version"), str) or not payload["compiler_version"]:
        errors.append("tool install payload compiler_version must be a non-empty string")
    if not isinstance(payload.get("channel"), str) or not payload["channel"]:
        errors.append("tool install payload channel must be a non-empty string")
    return errors


def validate_tool_use_payload(
    payload: dict[str, Any],
    *,
    expected_version: str,
    expected_channel: str,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "tool use":
        errors.append("tool use payload field 'command' must equal 'tool use'")
    if payload.get("compiler_version") != expected_version:
        errors.append(
            f"tool use payload compiler_version must equal {expected_version!r}"
        )
    if payload.get("channel") != expected_channel:
        errors.append(f"tool use payload channel must equal {expected_channel!r}")
    if payload.get("integration_phase") != "compile-plan-live":
        errors.append("tool use payload integration_phase must equal 'compile-plan-live'")
    return errors


def validate_tool_pin_payload(
    payload: dict[str, Any],
    *,
    manifest_path: pathlib.Path,
    expected_version: str,
    expected_channel: str,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "tool pin":
        errors.append("tool pin payload field 'command' must equal 'tool pin'")
    if payload.get("mode") != "write":
        errors.append("tool pin payload field 'mode' must equal 'write'")
    if payload.get("manifest_path") != str(manifest_path):
        errors.append(
            f"tool pin payload manifest_path must equal {str(manifest_path)!r}"
        )
    if payload.get("compiler_version") != expected_version:
        errors.append(
            f"tool pin payload compiler_version must equal {expected_version!r}"
        )
    if payload.get("channel") != expected_channel:
        errors.append(f"tool pin payload channel must equal {expected_channel!r}")
    pin_path = payload.get("pin_path")
    if not isinstance(pin_path, str) or not pathlib.Path(pin_path).is_file():
        errors.append("tool pin payload pin_path must reference an existing file")
    return errors


def validate_tool_status_payload(
    payload: dict[str, Any],
    *,
    manifest_path: pathlib.Path,
    expected_version: str,
    expected_channel: str,
    expected_install_binary_path: str,
    expected_current_version: str | None = None,
    expected_current_channel: str | None = None,
    expected_current_binary_path: str | None = None,
    expected_installed_count: int | None = None,
) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "tool status":
        errors.append("tool status payload field 'command' must equal 'tool status'")
    if payload.get("schema_version") != 1:
        errors.append("tool status payload schema_version must equal 1")
    if payload.get("manifest_path") != str(manifest_path):
        errors.append(
            f"tool status payload manifest_path must equal {str(manifest_path)!r}"
        )
    toolchain = payload.get("toolchain")
    if not isinstance(toolchain, dict):
        errors.append("tool status payload toolchain must be an object")
    else:
        if toolchain.get("source") != "project-pin":
            errors.append("tool status payload toolchain.source must equal 'project-pin'")
        if toolchain.get("version") != expected_version:
            errors.append(
                f"tool status payload toolchain.version must equal {expected_version!r}"
            )
        if toolchain.get("channel") != expected_channel:
            errors.append(
                f"tool status payload toolchain.channel must equal {expected_channel!r}"
            )
        if toolchain.get("candidate_binary_path") != expected_install_binary_path:
            errors.append(
                f"tool status payload toolchain.candidate_binary_path must equal {expected_install_binary_path!r}"
            )
    project_pin = payload.get("project_pin")
    if not isinstance(project_pin, dict):
        errors.append("tool status payload project_pin must be an object")
    else:
        if project_pin.get("version") != expected_version:
            errors.append(
                f"tool status payload project_pin.version must equal {expected_version!r}"
            )
        if project_pin.get("channel") != expected_channel:
            errors.append(
                f"tool status payload project_pin.channel must equal {expected_channel!r}"
            )
        if project_pin.get("install_present") is not True:
            errors.append("tool status payload project_pin.install_present must equal true")
        if project_pin.get("install_binary_path") != expected_install_binary_path:
            errors.append(
                f"tool status payload project_pin.install_binary_path must equal {expected_install_binary_path!r}"
            )
    active_compiler = payload.get("active_compiler")
    if not isinstance(active_compiler, dict):
        errors.append("tool status payload active_compiler must be an object")
    else:
        if active_compiler.get("compiler_version") != expected_version:
            errors.append(
                f"tool status payload active_compiler.compiler_version must equal {expected_version!r}"
            )
        if active_compiler.get("channel") != expected_channel:
            errors.append(
                f"tool status payload active_compiler.channel must equal {expected_channel!r}"
            )
    current_compiler = payload.get("current_compiler")
    if not isinstance(current_compiler, dict):
        errors.append("tool status payload current_compiler must be an object")
    else:
        if current_compiler.get("compiler_version") != (
            expected_current_version or expected_version
        ):
            errors.append(
                "tool status payload current_compiler.compiler_version must equal "
                f"{(expected_current_version or expected_version)!r}"
            )
        if current_compiler.get("channel") != (
            expected_current_channel or expected_channel
        ):
            errors.append(
                "tool status payload current_compiler.channel must equal "
                f"{(expected_current_channel or expected_channel)!r}"
            )
    managed = payload.get("managed_toolchains")
    if not isinstance(managed, dict):
        errors.append("tool status payload managed_toolchains must be an object")
    else:
        installed = managed.get("installed")
        if not isinstance(installed, list) or not installed:
            errors.append("tool status payload managed_toolchains.installed must be a non-empty array")
        elif expected_installed_count is not None and len(installed) != expected_installed_count:
            errors.append(
                "tool status payload managed_toolchains.installed size must equal "
                f"{expected_installed_count}"
            )
        expected_current_binary = expected_current_binary_path or expected_install_binary_path
        if managed.get("current_binary") != expected_current_binary:
            errors.append(
                "tool status payload managed_toolchains.current_binary must equal "
                f"{expected_current_binary!r}"
            )
    notes = payload.get("notes")
    if not isinstance(notes, list):
        errors.append("tool status payload notes must be an array")
    elif notes:
        errors.append("tool status payload notes must be empty for the managed sample workflow")
    return errors


def build_single_package_step_specs(context: dict[str, Any], styio_bin: str) -> list[dict[str, Any]]:
    manifest_path = str(context["manifest_path"])
    run_target_name = str(context["run_target_name"])
    test_target_name = str(context["test_target_name"])
    return [
        {
            "name": "tool_install_primary",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "install",
                "--styio-bin",
                styio_bin,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_tool_install_payload(
                payload,
                styio_bin=styio_bin,
            ),
            "after": lambda payload, state: state.update(
                {
                    "primary_version": payload["compiler_version"],
                    "primary_channel": payload["channel"],
                    "primary_install_binary_path": payload["install_binary_path"],
                    "primary_managed_binary_path": payload["managed_binary_path"],
                }
            ),
        },
        {
            "name": "tool_use_primary",
            "command": lambda state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "use",
                "--version",
                state["primary_version"],
                "--channel",
                state["primary_channel"],
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_use_payload(
                payload,
                expected_version=state["primary_version"],
                expected_channel=state["primary_channel"],
            ),
        },
        {
            "name": "tool_pin_primary",
            "command": lambda state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "pin",
                "--manifest-path",
                manifest_path,
                "--version",
                state["primary_version"],
                "--channel",
                state["primary_channel"],
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_pin_payload(
                payload,
                manifest_path=context["manifest_path"],
                expected_version=state["primary_version"],
                expected_channel=state["primary_channel"],
            ),
            "after": lambda payload, state: state.update({"primary_pin_path": payload["pin_path"]}),
        },
        {
            "name": "tool_status_primary",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "status",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_status_payload(
                payload,
                manifest_path=context["manifest_path"],
                expected_version=state["primary_version"],
                expected_channel=state["primary_channel"],
                expected_install_binary_path=state["primary_install_binary_path"],
                expected_current_binary_path=state["primary_managed_binary_path"],
                expected_installed_count=1,
            ),
        },
        {
            "name": "tool_install_alternate",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "install",
                "--styio-bin",
                context["alternate_styio_bin"],
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_tool_install_payload(
                payload,
                styio_bin=context["alternate_styio_bin"],
            ),
            "after": lambda payload, state: state.update(
                {
                    "alternate_version": payload["compiler_version"],
                    "alternate_channel": payload["channel"],
                    "alternate_install_binary_path": payload["install_binary_path"],
                    "alternate_managed_binary_path": payload["managed_binary_path"],
                }
            ),
        },
        {
            "name": "tool_use_alternate",
            "command": lambda state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "use",
                "--version",
                state["alternate_version"],
                "--channel",
                state["alternate_channel"],
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_use_payload(
                payload,
                expected_version=state["alternate_version"],
                expected_channel=state["alternate_channel"],
            ),
        },
        {
            "name": "tool_pin_alternate",
            "command": lambda state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "pin",
                "--manifest-path",
                manifest_path,
                "--version",
                state["alternate_version"],
                "--channel",
                state["alternate_channel"],
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_pin_payload(
                payload,
                manifest_path=context["manifest_path"],
                expected_version=state["alternate_version"],
                expected_channel=state["alternate_channel"],
            ),
            "after": lambda payload, state: state.update({"alternate_pin_path": payload["pin_path"]}),
        },
        {
            "name": "tool_status_alternate",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "status",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_status_payload(
                payload,
                manifest_path=context["manifest_path"],
                expected_version=state["alternate_version"],
                expected_channel=state["alternate_channel"],
                expected_install_binary_path=state["alternate_install_binary_path"],
                expected_current_binary_path=state["alternate_managed_binary_path"],
                expected_installed_count=2,
            ),
        },
        {
            "name": "check_switched",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "check",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_check_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=False,
                expected_binary=state["alternate_install_binary_path"],
            ),
        },
        {
            "name": "fetch_switched",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "fetch",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_fetch_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=False,
            ),
        },
        {
            "name": "vendor_switched",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "vendor",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_vendor_payload(
                payload,
                workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_package_count=int(context["package_count"]),
            ),
        },
        {
            "name": "run_switched",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "run",
                "--manifest-path",
                manifest_path,
                "--bin",
                run_target_name,
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_execution_payload(
                payload,
                command_name="run",
                intent="run",
                target_kind="bin",
                target_name=str(context["run_target_name"]),
                expected_package=str(context["selected_package"]),
                expected_package_id=str(context["selected_package_id"]),
                expected_package_count=int(context["package_count"]),
                expected_workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_binary=state["alternate_install_binary_path"],
            ),
        },
        {
            "name": "tool_use_primary_return",
            "command": lambda state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "use",
                "--version",
                state["primary_version"],
                "--channel",
                state["primary_channel"],
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_use_payload(
                payload,
                expected_version=state["primary_version"],
                expected_channel=state["primary_channel"],
            ),
        },
        {
            "name": "tool_pin_primary_return",
            "command": lambda state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "pin",
                "--manifest-path",
                manifest_path,
                "--version",
                state["primary_version"],
                "--channel",
                state["primary_channel"],
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_pin_payload(
                payload,
                manifest_path=context["manifest_path"],
                expected_version=state["primary_version"],
                expected_channel=state["primary_channel"],
            ),
        },
        {
            "name": "tool_status_primary_return",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "tool",
                "status",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_tool_status_payload(
                payload,
                manifest_path=context["manifest_path"],
                expected_version=state["primary_version"],
                expected_channel=state["primary_channel"],
                expected_install_binary_path=state["primary_install_binary_path"],
                expected_current_binary_path=state["primary_managed_binary_path"],
                expected_installed_count=2,
            ),
        },
        {
            "name": "test_primary_return",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "test",
                "--manifest-path",
                manifest_path,
                "--test",
                test_target_name,
            ],
            "expect_ok": True,
            "validator": lambda payload, state: validate_execution_payload(
                payload,
                command_name="test",
                intent="test",
                target_kind="test",
                target_name=str(context["test_target_name"]),
                expected_package=str(context["selected_package"]),
                expected_package_id=str(context["selected_package_id"]),
                expected_package_count=int(context["package_count"]),
                expected_workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_binary=state["primary_install_binary_path"],
            ),
        },
        {
            "name": "publish_preflight_primary_return",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "publish",
                "--manifest-path",
                manifest_path,
                "--dry-run",
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_publish_payload(
                payload,
                expected_package=str(context["selected_package"]),
                expected_package_root=context["publish_package_root"],
            ),
        },
    ]


def build_workspace_step_specs(context: dict[str, Any], styio_bin: str) -> list[dict[str, Any]]:
    manifest_path = str(context["manifest_path"])
    selected_package = str(context["selected_package"])
    return [
        {
            "name": "check",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "check",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_check_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=False,
                expected_binary=styio_bin,
            ),
        },
        {
            "name": "fetch",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "fetch",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_fetch_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=False,
            ),
        },
        {
            "name": "vendor",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "vendor",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_vendor_payload(
                payload,
                workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_package_count=int(context["package_count"]),
            ),
        },
        {
            "name": "run_requires_package",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "run",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
            ],
            "expect_ok": False,
            "validator": lambda payload, _state: validate_expected_error_payload(
                payload,
                command_name="run",
                category="PlanError",
                message_fragment="workspace build is ambiguous",
            ),
        },
        {
            "name": "test_requires_package",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "test",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
                "--test",
                str(context["test_target_name"]),
            ],
            "expect_ok": False,
            "validator": lambda payload, _state: validate_expected_error_payload(
                payload,
                command_name="test",
                category="PlanError",
                message_fragment="workspace build is ambiguous",
            ),
        },
        {
            "name": "publish_requires_package",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "publish",
                "--manifest-path",
                manifest_path,
                "--dry-run",
            ],
            "expect_ok": False,
            "validator": lambda payload, _state: validate_expected_error_payload(
                payload,
                command_name="publish",
                category="PublishError",
                message_fragment="workspace publish is ambiguous",
            ),
        },
        {
            "name": "run",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "run",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
                "--package",
                selected_package,
                "--bin",
                str(context["run_target_name"]),
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_execution_payload(
                payload,
                command_name="run",
                intent="run",
                target_kind="bin",
                target_name=str(context["run_target_name"]),
                expected_package=str(context["selected_package"]),
                expected_package_id=str(context["selected_package_id"]),
                expected_package_count=int(context["package_count"]),
                expected_workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_binary=styio_bin,
            ),
        },
        {
            "name": "test",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "test",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
                "--package",
                selected_package,
                "--test",
                str(context["test_target_name"]),
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_execution_payload(
                payload,
                command_name="test",
                intent="test",
                target_kind="test",
                target_name=str(context["test_target_name"]),
                expected_package=str(context["selected_package"]),
                expected_package_id=str(context["selected_package_id"]),
                expected_package_count=int(context["package_count"]),
                expected_workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_binary=styio_bin,
            ),
        },
        {
            "name": "publish_preflight",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "publish",
                "--manifest-path",
                manifest_path,
                "--dry-run",
                "--package",
                selected_package,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_publish_payload(
                payload,
                expected_package=str(context["selected_package"]),
                expected_package_root=context["publish_package_root"],
            ),
        },
    ]


def build_offline_step_specs(context: dict[str, Any], styio_bin: str) -> list[dict[str, Any]]:
    manifest_path = str(context["manifest_path"])
    return [
        {
            "name": "vendor",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "vendor",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_vendor_payload(
                payload,
                workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_package_count=int(context["package_count"]),
                expected_git_snapshots=1,
            ),
        },
        {
            "name": "reset_pafio_home",
            "action": lambda _state: (
                shutil.rmtree(context["pafio_home"], ignore_errors=True),
                run_action(
                    "reset_pafio_home",
                    action="reset-pafio-home",
                    message=f"removed managed cache root {context['pafio_home']}",
                ),
            )[1],
        },
        {
            "name": "fetch_offline",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "fetch",
                "--offline",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_fetch_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=True,
                expected_git_packages=int(context["expected_git_packages"]),
                expected_registry_packages=int(context["expected_registry_packages"]),
            ),
        },
        {
            "name": "check_offline",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "check",
                "--offline",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_check_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=True,
                expected_binary=styio_bin,
            ),
        },
        {
            "name": "run_offline",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "run",
                "--offline",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
                "--bin",
                str(context["run_target_name"]),
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_execution_payload(
                payload,
                command_name="run",
                intent="run",
                target_kind="bin",
                target_name=str(context["run_target_name"]),
                expected_package=str(context["selected_package"]),
                expected_package_id=str(context["selected_package_id"]),
                expected_package_count=int(context["package_count"]),
                expected_workspace_root=context["workspace_root"],
                expected_offline=True,
                expected_binary=styio_bin,
            ),
        },
    ]


def build_registry_step_specs(context: dict[str, Any], styio_bin: str) -> list[dict[str, Any]]:
    manifest_path = str(context["manifest_path"])
    publish_manifest_path = str(context["publish_manifest_path"])
    return [
        {
            "name": "publish_registry_package",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "publish",
                "--manifest-path",
                publish_manifest_path,
                "--registry",
                str(context["registry_publish_root"]),
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_registry_publish_payload(
                payload,
                registry_root=context["registry_publish_root"],
                expected_package_root=context["publish_package_root"],
            ),
        },
        {
            "name": "republish_registry_conflict",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "publish",
                "--manifest-path",
                publish_manifest_path,
                "--registry",
                str(context["registry_publish_root"]),
            ],
            "expect_ok": False,
            "validator": lambda payload, _state: validate_expected_error_payload(
                payload,
                command_name="publish",
                category="PublishError",
                message_fragment="already published",
            ),
        },
        {
            "name": "fetch_registry",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "fetch",
                "--manifest-path",
                manifest_path,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_fetch_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=False,
                expected_git_packages=0,
                expected_registry_packages=1,
            ),
        },
        {
            "name": "project_graph_registry",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "project-graph",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_project_graph_registry_payload(
                payload,
                manifest_path=context["manifest_path"],
                expected_registry_root=str(context["registry_fetch_root"]),
                expected_dependency_package=str(context["registry_dependency_package"]),
                expected_dependency_version=str(context["registry_dependency_version"]),
            ),
        },
        {
            "name": "check_registry",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "check",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_check_payload(
                payload,
                expected_package_count=int(context["package_count"]),
                expected_offline=False,
                expected_binary=styio_bin,
            ),
        },
        {
            "name": "run_registry",
            "command": lambda _state: [
                context["pafio_bin"],
                "--json",
                "run",
                "--manifest-path",
                manifest_path,
                "--styio-bin",
                styio_bin,
                "--bin",
                str(context["run_target_name"]),
            ],
            "expect_ok": True,
            "validator": lambda payload, _state: validate_execution_payload(
                payload,
                command_name="run",
                intent="run",
                target_kind="bin",
                target_name=str(context["run_target_name"]),
                expected_package=str(context["selected_package"]),
                expected_package_id=str(context["selected_package_id"]),
                expected_package_count=int(context["package_count"]),
                expected_workspace_root=context["workspace_root"],
                expected_offline=False,
                expected_binary=styio_bin,
            ),
        },
    ]


def build_step_specs(context: dict[str, Any], styio_bin: str) -> list[dict[str, Any]]:
    if context["name"] == "single-package-managed-toolchain":
        return build_single_package_step_specs(context, styio_bin)
    if context["name"] == "workspace-package-selection":
        return build_workspace_step_specs(context, styio_bin)
    if context["name"] == "offline-vendored-git":
        return build_offline_step_specs(context, styio_bin)
    if context["name"] == "registry-hosted-source":
        return build_registry_step_specs(context, styio_bin)
    raise RuntimeError(f"unknown scenario: {context['name']}")


def build_scenario_context(
    name: str,
    temp_root: pathlib.Path,
    pafio_bin: str,
    styio_bin: str,
) -> dict[str, Any]:
    if name == "single-package-managed-toolchain":
        context = write_single_package_project(temp_root)
    elif name == "workspace-package-selection":
        context = write_workspace_project(temp_root)
    elif name == "offline-vendored-git":
        context = write_offline_project(temp_root)
    elif name == "registry-hosted-source":
        context = write_registry_project(temp_root)
    else:
        raise RuntimeError(f"unknown scenario: {name}")
    context["pafio_bin"] = pafio_bin
    context["pafio_home"] = temp_root / ".pafio-home"
    if name == "single-package-managed-toolchain":
        alternate_version = "0.0.2"
        alternate_channel = "stable"
        context["alternate_styio_bin"] = str(
            create_machine_info_alias(
                temp_root / "styio-switch-alias",
                delegate_binary=styio_bin,
                version=alternate_version,
                channel=alternate_channel,
            )
        )
    return context


def run_scenario(
    name: str,
    *,
    styio_bin: str,
    pafio_bin: str,
    keep_temp: bool,
) -> dict[str, Any]:
    temp_root = pathlib.Path(tempfile.mkdtemp(prefix=f"styio-ecosystem-{name}-"))
    context = build_scenario_context(name, temp_root, pafio_bin, styio_bin)
    env = dict(os.environ)
    env["PAFIO_HOME"] = str(context["pafio_home"])
    steps: list[dict[str, Any]] = []
    validation_errors: list[str] = []
    state: dict[str, Any] = {}
    step_specs = build_step_specs(context, styio_bin)
    try:
        for step_spec in step_specs:
            step_name = step_spec["name"]
            label = f"{name}::{step_name}"
            if "action" in step_spec:
                step = step_spec["action"](state)
                steps.append(step)
                if not step["ok"]:
                    validation_errors.append(f"{label} action failed")
                continue

            try:
                command = ensure_command(step_spec["command"](state))
            except Exception as err:
                validation_errors.append(f"{label} could not build command: {err}")
                continue

            step = run_step(step_name, command, env=env, cwd=ROOT)
            steps.append(step)
            expect_ok = bool(step_spec["expect_ok"])
            if expect_ok:
                if not step["ok"]:
                    validation_errors.append(
                        f"{label} failed with exit code {step['returncode']}: {step['stderr'] or step['stdout']}"
                    )
                    continue
            else:
                if step["ok"]:
                    validation_errors.append(f"{label} unexpectedly succeeded")
                    continue

            try:
                payload = load_step_json(step, label)
            except RuntimeError as err:
                validation_errors.append(str(err))
                continue
            validation_errors.extend(step_spec["validator"](payload, state))
            if "after" in step_spec:
                step_spec["after"](payload, state)
    finally:
        if not keep_temp:
            shutil.rmtree(temp_root, ignore_errors=True)

    expected_failure_names = {
        spec["name"]
        for spec in step_specs
        if spec.get("kind") != "action" and spec.get("expect_ok") is False
    }
    ok = (
        not validation_errors
        and all(
            step["ok"]
            for step in steps
            if step["kind"] == "action" or step["name"] not in expected_failure_names
        )
        and all(
            not step["ok"]
            for step in steps
            if step["name"] in expected_failure_names
        )
    )
    return {
        "name": name,
        "ok": ok,
        "sample_workspace_root": str(temp_root),
        "pafio_home": str(context["pafio_home"]),
        "steps": steps,
        "validation_errors": validation_errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="ecosystem-sample-workflow-gate.py")
    parser.add_argument("--styio-bin", required=True, help="published styio binary used for workflow checks")
    parser.add_argument("--pafio-bin", default=str(DEFAULT_PAFIO), help="pafio wrapper or binary used for package-manager checks")
    parser.add_argument("--keep-temp", action="store_true", help="keep the generated sample workspaces on disk")
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    args = parser.parse_args(argv)

    styio_bin = canonical(pathlib.Path(args.styio_bin))
    pafio_bin = canonical(pathlib.Path(args.pafio_bin))
    scenario_names = [
        "single-package-managed-toolchain",
        "workspace-package-selection",
        "offline-vendored-git",
        "registry-hosted-source",
    ]

    scenarios = [
        run_scenario(
            name,
            styio_bin=styio_bin,
            pafio_bin=pafio_bin,
            keep_temp=args.keep_temp,
        )
        for name in scenario_names
    ]
    validation_errors = [
        f"{scenario['name']}: {error}"
        for scenario in scenarios
        for error in scenario["validation_errors"]
    ]
    ok = not validation_errors and all(bool(scenario["ok"]) for scenario in scenarios)
    payload = {
        "ok": ok,
        "styio_bin": styio_bin,
        "pafio_bin": pafio_bin,
        "scenarios": scenarios,
        "sample_workspace_roots": [scenario["sample_workspace_root"] for scenario in scenarios],
        "validation_errors": validation_errors,
    }

    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        if ok:
            print(
                "ecosystem sample workflow gate passed: managed toolchain switch, workspace package selection, offline vendored workflow, and registry-hosted sources are green"
            )
        else:
            print("ecosystem sample workflow gate failed", file=sys.stderr)
            for error in validation_errors:
                print(f"- {error}", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
