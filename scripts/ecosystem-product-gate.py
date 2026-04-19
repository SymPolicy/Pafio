#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
import urllib.request
import urllib.error
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SPIO = ROOT / "scripts" / "spio"
SAMPLE_GATE = ROOT / "scripts" / "ecosystem-sample-workflow-gate.py"
HOSTED_SERVER = ROOT / "scripts" / "spio-hosted-serve.py"
VIEW_APP_ROOT = ROOT.parent / "styio-view" / "frontend" / "styio_view_app"
PRODUCT_REPORT_MARKER = "STYIO_VIEW_PRODUCT_REPORT "


def run_step(
    name: str,
    command: list[str],
    *,
    cwd: pathlib.Path | None = None,
    env: dict[str, str] | None = None,
) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            command,
            cwd=str(cwd or ROOT),
            capture_output=True,
            text=True,
            env=env,
        )
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
    return {
        "name": name,
        "kind": "command",
        "command": command,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "ok": proc.returncode == 0,
    }


def load_json(text: str, context: str) -> dict[str, Any]:
    try:
        payload = json.loads(text)
    except json.JSONDecodeError as err:
        raise RuntimeError(f"{context} did not emit valid JSON") from err
    if not isinstance(payload, dict):
        raise RuntimeError(f"{context} must emit a top-level JSON object")
    return payload


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


def write_hosted_workspace(root: pathlib.Path) -> pathlib.Path:
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "main.styio"
    test_path = root / "tests" / "smoke.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    test_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/view-product\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = true\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"product\"\n"
        "path = \"src/main.styio\"\n\n"
        "[[test]]\n"
        "name = \"smoke\"\n"
        "path = \"tests/smoke.styio\"\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"view-product\")\n", encoding="utf-8")
    test_path.write_text(">_(\"view-product-smoke\")\n", encoding="utf-8")
    return manifest_path


def write_hosted_workspace_project(root: pathlib.Path) -> pathlib.Path:
    root_manifest = root / "spio.toml"
    app_manifest = root / "packages" / "app" / "spio.toml"
    tool_manifest = root / "packages" / "tool" / "spio.toml"
    app_source = root / "packages" / "app" / "src" / "main.styio"
    app_test = root / "packages" / "app" / "tests" / "smoke.styio"
    tool_source = root / "packages" / "tool" / "src" / "main.styio"
    tool_test = root / "packages" / "tool" / "tests" / "smoke.styio"
    app_source.parent.mkdir(parents=True, exist_ok=True)
    app_test.parent.mkdir(parents=True, exist_ok=True)
    tool_source.parent.mkdir(parents=True, exist_ok=True)
    tool_test.parent.mkdir(parents=True, exist_ok=True)
    root_manifest.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[workspace]\n"
        "members = [\"packages/app\", \"packages/tool\"]\n"
        "resolver = \"1\"\n",
        encoding="utf-8",
    )
    app_manifest.write_text(
        "[spio]\n"
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
        "[spio]\n"
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
    app_source.write_text(">_(\"view-hosted-app\")\n", encoding="utf-8")
    app_test.write_text(">_(\"view-hosted-app-smoke\")\n", encoding="utf-8")
    tool_source.write_text(">_(\"view-hosted-tool\")\n", encoding="utf-8")
    tool_test.write_text(">_(\"view-hosted-tool-smoke\")\n", encoding="utf-8")
    return root_manifest


def write_hosted_failing_dependency_workspace(root: pathlib.Path) -> pathlib.Path:
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    missing_remote = root / "missing-feed.git"
    manifest_path.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/broken-fetch\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = false\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"broken-fetch\"\n"
        "path = \"src/main.styio\"\n\n"
        "[dependencies]\n"
        f"feed = {{ package = \"acme/feed\", git = \"{missing_remote.resolve()}\", rev = \"deadbeef\" }}\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"broken-fetch\")\n", encoding="utf-8")
    return manifest_path


def write_registry_publish_package(root: pathlib.Path) -> pathlib.Path:
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "lib.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[spio]\n"
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
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "main.styio"
    test_path = root / "tests" / "smoke.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    test_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/registry-client\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n\n"
        "publish = true\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"client\"\n"
        "path = \"src/main.styio\"\n\n"
        "[[test]]\n"
        "name = \"smoke\"\n"
        "path = \"tests/smoke.styio\"\n\n"
        "[dependencies]\n"
        f"registry_feed = {{ package = \"acme/registry-feed\", version = \"0.2.0\", registry = \"{fetch_root}\" }}\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"registry-client\")\n", encoding="utf-8")
    test_path.write_text(">_(\"registry-client-smoke\")\n", encoding="utf-8")
    return manifest_path


def write_registry_missing_consumer_project(
    root: pathlib.Path, fetch_root: str
) -> pathlib.Path:
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/registry-missing-client\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"missing-client\"\n"
        "path = \"src/main.styio\"\n\n"
        "[dependencies]\n"
        f"registry_feed = {{ package = \"acme/registry-feed\", version = \"9.9.9\", registry = \"{fetch_root}\" }}\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"registry-missing-client\")\n", encoding="utf-8")
    return manifest_path


def write_hosted_registry_failure_workspace(
    root: pathlib.Path, fetch_root: str
) -> pathlib.Path:
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/broken-fetch\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = false\n\n"
        "[toolchain]\n"
        "channel = \"stable\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"broken-fetch\"\n"
        "path = \"src/main.styio\"\n\n"
        "[dependencies]\n"
        f"registry_feed = {{ package = \"acme/registry-feed\", version = \"9.9.9\", registry = \"{fetch_root}\" }}\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"broken-fetch\")\n", encoding="utf-8")
    return manifest_path


def start_hosted_server(spio_bin: pathlib.Path) -> tuple[subprocess.Popen[str], dict[str, Any]]:
    proc = subprocess.Popen(
        [
            sys.executable,
            str(HOSTED_SERVER),
            "--spio-bin",
            str(spio_bin),
            "--port",
            "0",
        ],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert proc.stdout is not None
    ready_line = proc.stdout.readline().strip()
    if not ready_line:
        stderr = ""
        if proc.stderr is not None:
            stderr = proc.stderr.read().strip()
        raise RuntimeError(stderr or "hosted server did not emit a ready payload")
    try:
        ready = json.loads(ready_line)
    except json.JSONDecodeError as err:
        raise RuntimeError("hosted server emitted invalid ready payload") from err
    if not isinstance(ready, dict) or "base_url" not in ready:
        raise RuntimeError("hosted server ready payload is missing base_url")
    return proc, ready


def stop_hosted_server(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def health_check(base_url: str) -> dict[str, Any]:
    url = f"{base_url}/health"
    with urllib.request.urlopen(url, timeout=5) as response:
        payload = json.loads(response.read().decode("utf-8"))
    if not isinstance(payload, dict):
        raise RuntimeError("hosted health endpoint did not return a JSON object")
    return payload


def open_workspace(
    *,
    base_url: str,
    workspace_root: pathlib.Path,
    manifest_path: pathlib.Path,
) -> dict[str, Any]:
    payload = json.dumps(
        {
            "workspace_root": str(workspace_root),
            "manifest_path": str(manifest_path),
            "platform": "hosted-product-gate",
        }
    ).encode("utf-8")
    request = urllib.request.Request(
        f"{base_url}/workspaces/open",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            decoded = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as err:
        body = err.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"hosted open-workspace failed: {body}") from err
    if not isinstance(decoded, dict):
        raise RuntimeError("hosted open-workspace response was not a JSON object")
    return decoded


def parse_product_reports(text: str, *, step_name: str) -> list[dict[str, Any]]:
    reports: list[dict[str, Any]] = []
    for line in text.splitlines():
        marker_index = line.find(PRODUCT_REPORT_MARKER)
        if marker_index < 0:
            continue
        raw = line[marker_index + len(PRODUCT_REPORT_MARKER) :].strip()
        if not raw:
            continue
        try:
            payload = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if not isinstance(payload, dict):
            continue
        report = dict(payload)
        report.setdefault("source_step", step_name)
        reports.append(report)
    return reports


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the cross-repo Styio product workflow gate.",
    )
    parser.add_argument("--styio-bin", required=True)
    parser.add_argument("--spio-bin", default=str(DEFAULT_SPIO))
    parser.add_argument("--json", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    styio_bin = pathlib.Path(args.styio_bin).resolve()
    spio_bin = pathlib.Path(args.spio_bin).resolve()

    steps: list[dict[str, Any]] = []
    scenario_reports: list[dict[str, Any]] = []
    overall_ok = True

    sample_step = run_step(
        "baseline-sample-workflow",
        [
            sys.executable,
            str(SAMPLE_GATE),
            "--styio-bin",
            str(styio_bin),
            "--spio-bin",
            str(spio_bin),
            "--json",
        ],
        cwd=ROOT.parent,
    )
    steps.append(sample_step)
    overall_ok = overall_ok and sample_step["ok"]

    with tempfile.TemporaryDirectory(prefix="styio_product_gate_") as temp_root_str:
        temp_root = pathlib.Path(temp_root_str)
        workspace_root_local = temp_root / "desktop-local-workspace"
        manifest_path_local = write_hosted_workspace(workspace_root_local)
        workspace_root_local_multi = temp_root / "desktop-local-workspace-multi"
        manifest_path_local_multi = write_hosted_workspace_project(
            workspace_root_local_multi
        )
        workspace_root_local_failure = (
            temp_root / "desktop-local-workspace-failing-deps"
        )
        manifest_path_local_failure = write_hosted_failing_dependency_workspace(
            workspace_root_local_failure
        )
        registry_root_local = temp_root / "desktop-local-registry-root"
        registry_root_local.mkdir(parents=True, exist_ok=True)
        workspace_root_local_registry_publish = (
            temp_root / "desktop-local-workspace-registry-publish"
        )
        manifest_path_local_registry_publish = write_registry_publish_package(
            workspace_root_local_registry_publish
        )
        workspace_root_local_registry_consume = (
            temp_root / "desktop-local-workspace-registry-consume"
        )
        manifest_path_local_registry_consume = write_registry_consumer_project(
            workspace_root_local_registry_consume,
            registry_root_local.resolve().as_uri(),
        )
        workspace_root_local_registry_missing = (
            temp_root / "desktop-local-workspace-registry-missing-consume"
        )
        manifest_path_local_registry_missing = write_registry_missing_consumer_project(
            workspace_root_local_registry_missing,
            registry_root_local.resolve().as_uri(),
        )
        workspace_root = temp_root / "hosted-workspace"
        manifest_path = write_hosted_workspace(workspace_root)
        workspace_root_multi = temp_root / "hosted-workspace-multi"
        manifest_path_multi = write_hosted_workspace_project(workspace_root_multi)
        workspace_root_failure = temp_root / "hosted-workspace-failing-deps"
        manifest_path_failure = write_hosted_failing_dependency_workspace(
            workspace_root_failure
        )
        registry_root_hosted = temp_root / "hosted-registry-root"
        registry_root_hosted.mkdir(parents=True, exist_ok=True)
        workspace_root_registry_publish = temp_root / "hosted-workspace-registry-publish"
        manifest_path_registry_publish = write_registry_publish_package(
            workspace_root_registry_publish
        )
        workspace_root_registry_consume = temp_root / "hosted-workspace-registry-consume"
        manifest_path_registry_consume = write_registry_consumer_project(
            workspace_root_registry_consume,
            registry_root_hosted.resolve().as_uri(),
        )
        workspace_root_registry_missing = (
            temp_root / "hosted-workspace-registry-missing-consume"
        )
        manifest_path_registry_missing = write_hosted_registry_failure_workspace(
            workspace_root_registry_missing,
            registry_root_hosted.resolve().as_uri(),
        )
        alternate_styio_bin = create_machine_info_alias(
            temp_root / "styio-switch-alias",
            delegate_binary=str(styio_bin),
            version="0.0.2",
            channel="stable",
        )
        local_spio_home = temp_root / "desktop-local-spio-home"

        hosted_proc: subprocess.Popen[str] | None = None
        try:
            local_view_env = os.environ.copy()
            local_view_env.update(
                {
                    "STYIO_VIEW_PRODUCT_GATE": "1",
                    "STYIO_VIEW_PRODUCT_WORKSPACE_ROOT": str(workspace_root_local),
                    "STYIO_VIEW_PRODUCT_MANIFEST_PATH": str(manifest_path_local),
                    "STYIO_VIEW_PRODUCT_WORKSPACE2_ROOT": str(
                        workspace_root_local_multi
                    ),
                    "STYIO_VIEW_PRODUCT_MANIFEST2_PATH": str(
                        manifest_path_local_multi
                    ),
                    "STYIO_VIEW_PRODUCT_WORKSPACE3_ROOT": str(
                        workspace_root_local_failure
                    ),
                    "STYIO_VIEW_PRODUCT_MANIFEST3_PATH": str(
                        manifest_path_local_failure
                    ),
                    "STYIO_VIEW_PRODUCT_WORKSPACE4_ROOT": str(
                        workspace_root_local_registry_publish
                    ),
                    "STYIO_VIEW_PRODUCT_MANIFEST4_PATH": str(
                        manifest_path_local_registry_publish
                    ),
                    "STYIO_VIEW_PRODUCT_WORKSPACE5_ROOT": str(
                        workspace_root_local_registry_consume
                    ),
                    "STYIO_VIEW_PRODUCT_MANIFEST5_PATH": str(
                        manifest_path_local_registry_consume
                    ),
                    "STYIO_VIEW_PRODUCT_WORKSPACE6_ROOT": str(
                        workspace_root_local_registry_missing
                    ),
                    "STYIO_VIEW_PRODUCT_MANIFEST6_PATH": str(
                        manifest_path_local_registry_missing
                    ),
                    "STYIO_VIEW_PRODUCT_REGISTRY_ROOT": str(registry_root_local),
                    "STYIO_VIEW_SPIO_BIN": str(spio_bin),
                    "STYIO_VIEW_PRODUCT_STYIO_BIN": str(styio_bin),
                    "STYIO_VIEW_PRODUCT_STYIO_ALT_BIN": str(alternate_styio_bin),
                    "SPIO_HOME": str(local_spio_home),
                }
            )
            local_view_step = run_step(
                "view-local-product-workflow",
                [
                    "flutter",
                    "test",
                    "test/local_product_workflow_test.dart",
                ],
                cwd=VIEW_APP_ROOT,
                env=local_view_env,
            )
            steps.append(local_view_step)
            overall_ok = overall_ok and local_view_step["ok"]
            local_reports = parse_product_reports(
                local_view_step["stdout"], step_name=local_view_step["name"]
            )
            scenario_reports.extend(local_reports)
            local_report_step_ok = bool(local_reports)
            steps.append(
                {
                    "name": "view-local-product-report",
                    "kind": "report",
                    "command": ["extract-product-report", local_view_step["name"]],
                    "returncode": 0 if local_report_step_ok else 1,
                    "stdout": json.dumps(
                        {"scenario_count": len(local_reports)}, sort_keys=True
                    ),
                    "stderr": ""
                    if local_report_step_ok
                    else "local product workflow emitted no structured scenario report",
                    "ok": local_report_step_ok,
                }
            )
            overall_ok = overall_ok and local_report_step_ok

            hosted_proc, ready = start_hosted_server(spio_bin)
            steps.append(
                {
                    "name": "hosted-server-ready",
                    "kind": "action",
                    "command": ["spio-hosted-serve"],
                    "returncode": 0,
                    "stdout": json.dumps(ready, sort_keys=True),
                    "stderr": "",
                    "ok": True,
                }
            )

            health = health_check(str(ready["base_url"]))
            steps.append(
                {
                    "name": "hosted-server-health",
                    "kind": "action",
                    "command": ["GET", f"{ready['base_url']}/health"],
                    "returncode": 0,
                    "stdout": json.dumps(health, sort_keys=True),
                    "stderr": "",
                    "ok": health.get("ok") is True,
                }
            )
            overall_ok = overall_ok and health.get("ok") is True

            opened = open_workspace(
                base_url=str(ready["base_url"]),
                workspace_root=workspace_root,
                manifest_path=manifest_path,
            )
            opened_workspace = opened.get("workspace") if isinstance(opened.get("workspace"), dict) else {}
            workspace_id = opened_workspace.get("workspaceId")
            steps.append(
                {
                    "name": "hosted-open-workspace",
                    "kind": "action",
                    "command": ["POST", f"{ready['base_url']}/workspaces/open"],
                    "returncode": 0,
                    "stdout": json.dumps(opened, sort_keys=True),
                    "stderr": "",
                    "ok": isinstance(workspace_id, str) and bool(workspace_id),
                }
            )
            overall_ok = overall_ok and isinstance(workspace_id, str) and bool(workspace_id)

            opened_multi = open_workspace(
                base_url=str(ready["base_url"]),
                workspace_root=workspace_root_multi,
                manifest_path=manifest_path_multi,
            )
            opened_workspace_multi = (
                opened_multi.get("workspace")
                if isinstance(opened_multi.get("workspace"), dict)
                else {}
            )
            workspace_id_multi = opened_workspace_multi.get("workspaceId")
            steps.append(
                {
                    "name": "hosted-open-workspace-multi",
                    "kind": "action",
                    "command": ["POST", f"{ready['base_url']}/workspaces/open"],
                    "returncode": 0,
                    "stdout": json.dumps(opened_multi, sort_keys=True),
                    "stderr": "",
                    "ok": isinstance(workspace_id_multi, str)
                    and bool(workspace_id_multi),
                }
            )
            overall_ok = overall_ok and isinstance(workspace_id_multi, str) and bool(
                workspace_id_multi
            )

            opened_failure = open_workspace(
                base_url=str(ready["base_url"]),
                workspace_root=workspace_root_failure,
                manifest_path=manifest_path_failure,
            )
            opened_workspace_failure = (
                opened_failure.get("workspace")
                if isinstance(opened_failure.get("workspace"), dict)
                else {}
            )
            workspace_id_failure = opened_workspace_failure.get("workspaceId")
            steps.append(
                {
                    "name": "hosted-open-workspace-failing-deps",
                    "kind": "action",
                    "command": ["POST", f"{ready['base_url']}/workspaces/open"],
                    "returncode": 0,
                    "stdout": json.dumps(opened_failure, sort_keys=True),
                    "stderr": "",
                    "ok": isinstance(workspace_id_failure, str)
                    and bool(workspace_id_failure),
                }
            )
            overall_ok = overall_ok and isinstance(
                workspace_id_failure, str
            ) and bool(workspace_id_failure)

            flutter_env = os.environ.copy()
            flutter_env.update(
                {
                    "STYIO_VIEW_PRODUCT_GATE": "1",
                    "STYIO_VIEW_HOSTED_URL": str(ready["base_url"]),
                    "STYIO_VIEW_HOSTED_WORKSPACE_ROOT": str(workspace_root),
                    "STYIO_VIEW_HOSTED_MANIFEST_PATH": str(manifest_path),
                    "STYIO_VIEW_HOSTED_WORKSPACE_ID": str(workspace_id or ""),
                    "STYIO_VIEW_HOSTED_WORKSPACE2_ROOT": str(workspace_root_multi),
                    "STYIO_VIEW_HOSTED_MANIFEST2_PATH": str(manifest_path_multi),
                    "STYIO_VIEW_HOSTED_WORKSPACE2_ID": str(workspace_id_multi or ""),
                    "STYIO_VIEW_HOSTED_WORKSPACE3_ROOT": str(workspace_root_failure),
                    "STYIO_VIEW_HOSTED_MANIFEST3_PATH": str(manifest_path_failure),
                    "STYIO_VIEW_HOSTED_WORKSPACE3_ID": str(
                        workspace_id_failure or ""
                    ),
                    "STYIO_VIEW_HOSTED_WORKSPACE4_ROOT": str(
                        workspace_root_registry_publish
                    ),
                    "STYIO_VIEW_HOSTED_MANIFEST4_PATH": str(
                        manifest_path_registry_publish
                    ),
                    "STYIO_VIEW_HOSTED_WORKSPACE5_ROOT": str(
                        workspace_root_registry_consume
                    ),
                    "STYIO_VIEW_HOSTED_MANIFEST5_PATH": str(
                        manifest_path_registry_consume
                    ),
                    "STYIO_VIEW_HOSTED_WORKSPACE6_ROOT": str(
                        workspace_root_registry_missing
                    ),
                    "STYIO_VIEW_HOSTED_MANIFEST6_PATH": str(
                        manifest_path_registry_missing
                    ),
                    "STYIO_VIEW_PRODUCT_REGISTRY_ROOT": str(registry_root_hosted),
                    "STYIO_VIEW_PRODUCT_STYIO_BIN": str(styio_bin),
                    "STYIO_VIEW_PRODUCT_STYIO_ALT_BIN": str(alternate_styio_bin),
                    "STYIO_VIEW_FORCE_HOSTED_ROUTE": "1",
                }
            )
            view_step = run_step(
                "view-hosted-product-workflow",
                [
                    "flutter",
                    "test",
                    "test/hosted_product_workflow_test.dart",
                ],
                cwd=VIEW_APP_ROOT,
                env=flutter_env,
            )
            steps.append(view_step)
            overall_ok = overall_ok and view_step["ok"]
            hosted_reports = parse_product_reports(
                view_step["stdout"], step_name=view_step["name"]
            )
            scenario_reports.extend(hosted_reports)
            hosted_report_step_ok = bool(hosted_reports)
            steps.append(
                {
                    "name": "view-hosted-product-report",
                    "kind": "report",
                    "command": ["extract-product-report", view_step["name"]],
                    "returncode": 0 if hosted_report_step_ok else 1,
                    "stdout": json.dumps(
                        {"scenario_count": len(hosted_reports)}, sort_keys=True
                    ),
                    "stderr": ""
                    if hosted_report_step_ok
                    else "hosted product workflow emitted no structured scenario report",
                    "ok": hosted_report_step_ok,
                }
            )
            overall_ok = overall_ok and hosted_report_step_ok
        finally:
            if hosted_proc is not None:
                stop_hosted_server(hosted_proc)

    payload = {
        "gate": "ecosystem-product-gate",
        "ok": overall_ok,
        "report": {
            "scenario_count": len(scenario_reports),
            "scenarios": scenario_reports,
        },
        "steps": steps,
    }
    if args.json:
        print(json.dumps(payload, sort_keys=True))
    else:
        for step in steps:
            status = "PASS" if step["ok"] else "FAIL"
            print(f"[{status}] {step['name']}")
            if step["stdout"]:
                print(step["stdout"].rstrip())
            if step["stderr"]:
                print(step["stderr"].rstrip(), file=sys.stderr)
        print(json.dumps({"ok": overall_ok}, sort_keys=True))
    return 0 if overall_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
