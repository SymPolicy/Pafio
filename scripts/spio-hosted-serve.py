#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import urlparse


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SPIO = ROOT / "scripts" / "spio"
API_PREFIX = "/api/styio-hosted/v1"


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def iso8601(value: datetime) -> str:
    return value.isoformat().replace("+00:00", "Z")


def load_json_text(text: str, *, context: str) -> dict[str, Any]:
    try:
        payload = json.loads(text)
    except json.JSONDecodeError as err:
        raise RuntimeError(f"{context} did not emit valid JSON") from err
    if not isinstance(payload, dict):
        raise RuntimeError(f"{context} must emit a top-level JSON object")
    return payload


def decode_payload(stdout: str, stderr: str) -> dict[str, Any] | None:
    primary = stdout.strip() or stderr.strip()
    if not primary.startswith("{"):
        return None
    try:
        payload = json.loads(primary)
    except json.JSONDecodeError:
        return None
    return payload if isinstance(payload, dict) else None


def resolve_path(value: str | None, *, workspace_root: pathlib.Path) -> pathlib.Path | None:
    if value is None or value == "":
        return None
    candidate = pathlib.Path(value)
    if candidate.is_absolute():
        return candidate
    return workspace_root / candidate


def read_jsonl(path: pathlib.Path | None) -> list[dict[str, Any]]:
    if path is None or not path.is_file():
        return []
    events: list[dict[str, Any]] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        try:
            decoded = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(decoded, dict):
            events.append(decoded)
    return events


def ensure_runtime_artifacts_inline(
    payload: dict[str, Any],
    *,
    workspace_root: pathlib.Path,
) -> dict[str, Any]:
    merged = dict(payload)
    diagnostics_path = resolve_path(
        merged.get("diagnostics_path") if isinstance(merged.get("diagnostics_path"), str) else None,
        workspace_root=workspace_root,
    )
    runtime_events_path = resolve_path(
        merged.get("runtime_events_path") if isinstance(merged.get("runtime_events_path"), str) else None,
        workspace_root=workspace_root,
    )
    receipt_path = resolve_path(
        merged.get("receipt_path") if isinstance(merged.get("receipt_path"), str) else None,
        workspace_root=workspace_root,
    )

    if "diagnostics" not in merged or not isinstance(merged["diagnostics"], list):
        merged["diagnostics"] = read_jsonl(diagnostics_path)
    if "runtime_events" not in merged or not isinstance(merged["runtime_events"], list):
        merged["runtime_events"] = read_jsonl(runtime_events_path)
    if "receipt" not in merged and receipt_path is not None and receipt_path.is_file():
        merged["receipt"] = load_json_text(
            receipt_path.read_text(encoding="utf-8"),
            context="workflow receipt",
        )
    return merged


def workspace_record_payload(record: "WorkspaceRecord") -> dict[str, Any]:
    payload: dict[str, Any] = {
        "workspaceId": record.workspace_id,
        "schemaVersion": "1",
        "ownerRef": record.owner_ref,
        "status": record.status,
        "entryUrl": record.entry_url,
        "createdAt": iso8601(record.created_at),
        "lastActiveAt": iso8601(record.last_active_at),
        "retentionDays": record.retention_days,
        "exportState": record.export_state,
    }
    if record.closed_at is not None:
        payload["closedAt"] = iso8601(record.closed_at)
    if record.retention_deadline is not None:
        payload["retentionDeadline"] = iso8601(record.retention_deadline)
    if record.core_file_export_url is not None:
        payload["coreFileExportUrl"] = record.core_file_export_url
    if record.core_file_export_expires_at is not None:
        payload["coreFileExportExpiresAt"] = iso8601(record.core_file_export_expires_at)
    return payload


@dataclass
class WorkspaceRecord:
    workspace_id: str
    workspace_root: pathlib.Path
    manifest_path: pathlib.Path
    platform: str
    spio_home: pathlib.Path
    owner_ref: str = "styio-view"
    status: str = "active"
    entry_url: str = ""
    export_state: str = "not_requested"
    retention_days: int = 7
    created_at: datetime = field(default_factory=utc_now)
    last_active_at: datetime = field(default_factory=utc_now)
    closed_at: datetime | None = None
    retention_deadline: datetime | None = None
    core_file_export_url: str | None = None
    core_file_export_expires_at: datetime | None = None

    def touch(self) -> None:
        self.last_active_at = utc_now()

    def close(self) -> None:
        self.status = "pending_deletion"
        self.closed_at = utc_now()
        self.retention_deadline = self.closed_at + timedelta(days=self.retention_days)
        self.touch()

    def refresh_export(self) -> None:
        export_dir = self.spio_home / "hosted-exports"
        export_dir.mkdir(parents=True, exist_ok=True)
        export_file = export_dir / f"{self.workspace_id}-core-files.json"
        payload = {
            "manifest": str(self.manifest_path),
            "workspace_root": str(self.workspace_root),
            "generated_at": iso8601(utc_now()),
            "core_files": sorted(
                str(path.relative_to(self.workspace_root))
                for path in self.workspace_root.rglob("*")
                if path.is_file()
                and path.suffix in {".styio", ".toml", ".lock"}
            ),
        }
        export_file.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        self.export_state = "ready"
        self.core_file_export_url = export_file.as_uri()
        self.core_file_export_expires_at = utc_now() + timedelta(days=1)
        self.touch()


class HostedWorkspaceServer:
    def __init__(self, *, spio_bin: pathlib.Path, bind: str, port: int):
        self.spio_bin = spio_bin
        self.bind = bind
        self.port = port
        self.records: dict[str, WorkspaceRecord] = {}

    def serve(self) -> None:
        server = ThreadingHTTPServer((self.bind, self.port), self._handler_type())
        host, port = server.server_address
        ready = {
            "api": "styio-hosted-control-plane",
            "version": 1,
            "bind": host,
            "port": port,
            "base_url": f"http://{host}:{port}{API_PREFIX}",
            "spio_bin": str(self.spio_bin),
        }
        print(json.dumps(ready), flush=True)
        server.serve_forever()

    def _handler_type(self) -> type[BaseHTTPRequestHandler]:
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: object) -> None:
                return

            def do_GET(self) -> None:  # noqa: N802
                outer.handle_request(self)

            def do_POST(self) -> None:  # noqa: N802
                outer.handle_request(self)

        return Handler

    def handle_request(self, handler: BaseHTTPRequestHandler) -> None:
        parsed = urlparse(handler.path)
        path = parsed.path

        if path == f"{API_PREFIX}/health":
            self._write_json(handler, HTTPStatus.OK, {"ok": True})
            return

        try:
            if path == f"{API_PREFIX}/workspaces/open" and handler.command == "POST":
                body = self._read_body_json(handler)
                response = self.open_workspace(body)
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/project-graph") and handler.command == "GET":
                workspace_id = self._workspace_id_from_path(path, suffix="/project-graph")
                response = self.project_graph(workspace_id)
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/tool/install") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/tool/install")
                response = self.tool_install(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/tool/use") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/tool/use")
                response = self.tool_use(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/tool/pin") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/tool/pin")
                response = self.tool_pin(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/tool/clear-pin") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/tool/clear-pin")
                response = self.tool_clear_pin(workspace_id)
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/dependencies/fetch") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/dependencies/fetch")
                response = self.fetch_dependencies(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/dependencies/vendor") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/dependencies/vendor")
                response = self.vendor_dependencies(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/execution/run") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/execution/run")
                response = self.run_workflow(workspace_id, "run", self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/execution/build") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/execution/build")
                response = self.run_workflow(workspace_id, "build", self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/execution/test") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/execution/test")
                response = self.run_workflow(workspace_id, "test", self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/deployment/pack") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/deployment/pack")
                response = self.pack_project(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/deployment/preflight") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/deployment/preflight")
                response = self.prepare_publish(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/deployment/publish") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/deployment/publish")
                response = self.publish_to_registry(workspace_id, self._read_body_json(handler))
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/close") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/close")
                response = self.close_workspace(workspace_id)
                self._write_json(handler, HTTPStatus.OK, response)
                return
            if path.endswith("/export") and handler.command == "POST":
                workspace_id = self._workspace_id_from_path(path, suffix="/export")
                response = self.export_workspace(workspace_id)
                self._write_json(handler, HTTPStatus.OK, response)
                return
        except KeyError:
            self._write_json(handler, HTTPStatus.NOT_FOUND, {"message": "workspace not found"})
            return
        except RuntimeError as err:
            self._write_json(handler, HTTPStatus.BAD_REQUEST, {"message": str(err)})
            return
        except Exception as err:  # pragma: no cover - defensive error surface
            self._write_json(handler, HTTPStatus.INTERNAL_SERVER_ERROR, {"message": str(err)})
            return

        self._write_json(handler, HTTPStatus.NOT_FOUND, {"message": "unknown hosted route"})

    def open_workspace(self, body: dict[str, Any]) -> dict[str, Any]:
        workspace_root = pathlib.Path(str(body.get("workspace_root", ""))).resolve()
        if not workspace_root.is_dir():
            raise RuntimeError(f"workspace root not found: {workspace_root}")
        manifest_value = body.get("manifest_path")
        manifest_path = (
            pathlib.Path(str(manifest_value)).resolve()
            if manifest_value
            else (workspace_root / "spio.toml").resolve()
        )
        if not manifest_path.is_file():
            raise RuntimeError(f"manifest not found: {manifest_path}")
        workspace_id = body.get("workspace_id")
        if isinstance(workspace_id, str) and workspace_id in self.records:
            record = self.records[workspace_id]
        else:
            workspace_id = f"hosted-{uuid.uuid4().hex[:12]}"
            spio_home_value = body.get("spio_home")
            if spio_home_value:
                spio_home = pathlib.Path(str(spio_home_value)).resolve()
                spio_home.mkdir(parents=True, exist_ok=True)
            else:
                spio_home = pathlib.Path(
                    tempfile.mkdtemp(prefix=f"spio-hosted-{workspace_id}-")
                )
            record = WorkspaceRecord(
                workspace_id=workspace_id,
                workspace_root=workspace_root,
                manifest_path=manifest_path,
                platform=str(body.get("platform", "unknown")),
                spio_home=spio_home,
                entry_url=f"{API_PREFIX}/workspaces/{workspace_id}",
            )
            self.records[workspace_id] = record
        record.touch()
        payload = self._run_spio_json(
            record,
            ["project-graph", "--json", "--manifest-path", str(record.manifest_path)],
        )
        return {
            "workspace": workspace_record_payload(record),
            "payload": payload,
        }

    def project_graph(self, workspace_id: str) -> dict[str, Any]:
        record = self.records[workspace_id]
        record.touch()
        payload = self._run_spio_json(
            record,
            ["project-graph", "--json", "--manifest-path", str(record.manifest_path)],
        )
        return {
            "workspace": workspace_record_payload(record),
            "payload": payload,
        }

    def tool_install(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        styio_bin = body.get("styio_binary_path")
        if not isinstance(styio_bin, str) or not styio_bin:
            raise RuntimeError("tool install requires styio_binary_path")
        return self._run_spio_command(
            record,
            "tool install",
            ["--json", "tool", "install", "--styio-bin", styio_bin],
        )

    def tool_use(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        compiler_version = body.get("compiler_version")
        if not isinstance(compiler_version, str) or not compiler_version:
            raise RuntimeError("tool use requires compiler_version")
        args = ["--json", "tool", "use", "--version", compiler_version]
        channel = body.get("channel")
        if isinstance(channel, str) and channel:
            args.extend(["--channel", channel])
        return self._run_spio_command(record, "tool use", args)

    def tool_pin(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        compiler_version = body.get("compiler_version")
        if not isinstance(compiler_version, str) or not compiler_version:
            raise RuntimeError("tool pin requires compiler_version")
        args = [
            "--json",
            "tool",
            "pin",
            "--manifest-path",
            str(record.manifest_path),
            "--version",
            compiler_version,
        ]
        channel = body.get("channel")
        if isinstance(channel, str) and channel:
            args.extend(["--channel", channel])
        return self._run_spio_command(record, "tool pin", args)

    def tool_clear_pin(self, workspace_id: str) -> dict[str, Any]:
        record = self.records[workspace_id]
        return self._run_spio_command(
            record,
            "tool pin",
            [
                "--json",
                "tool",
                "pin",
                "--manifest-path",
                str(record.manifest_path),
                "--clear",
            ],
        )

    def fetch_dependencies(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        args = ["--json", "fetch", "--manifest-path", str(record.manifest_path)]
        if body.get("locked") is True:
            args.append("--locked")
        if body.get("offline") is True:
            args.append("--offline")
        return self._run_spio_command(record, "fetch", args)

    def vendor_dependencies(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        args = ["--json", "vendor", "--manifest-path", str(record.manifest_path)]
        output_path = body.get("output_path")
        if isinstance(output_path, str) and output_path:
            args.extend(["--output", output_path])
        if body.get("locked") is True:
            args.append("--locked")
        if body.get("offline") is True:
            args.append("--offline")
        return self._run_spio_command(record, "vendor", args)

    def run_workflow(
        self,
        workspace_id: str,
        command: str,
        body: dict[str, Any],
    ) -> dict[str, Any]:
        record = self.records[workspace_id]
        working_root = record.workspace_root
        manifest_path = record.manifest_path
        cleanup_overlay: pathlib.Path | None = None

        active_file_path = body.get("active_file_path")
        document_text = body.get("document_text")
        if isinstance(active_file_path, str) and active_file_path and isinstance(document_text, str):
            cleanup_overlay = pathlib.Path(
                tempfile.mkdtemp(prefix=f"{workspace_id}-{command}-overlay-")
            )
            overlay_root = cleanup_overlay / "workspace"
            shutil.copytree(record.workspace_root, overlay_root)
            source_file = pathlib.Path(active_file_path)
            relative = (
                source_file.relative_to(record.workspace_root)
                if source_file.is_absolute()
                else pathlib.Path(active_file_path)
            )
            overlay_file = overlay_root / relative
            overlay_file.parent.mkdir(parents=True, exist_ok=True)
            overlay_file.write_text(document_text, encoding="utf-8")
            working_root = overlay_root
            manifest_path = overlay_root / record.manifest_path.relative_to(record.workspace_root)

        args = ["--json", command, "--manifest-path", str(manifest_path)]
        package_name = body.get("package_name")
        if isinstance(package_name, str) and package_name:
            args.extend(["--package", package_name])
        target_name = body.get("target_name")
        target_kind = body.get("target_kind")
        if target_kind == "lib":
            args.append("--lib")
        elif isinstance(target_name, str) and target_name:
            if target_kind == "test":
                args.extend(["--test", target_name])
            elif target_kind == "bin":
                args.extend(["--bin", target_name])
        response = self._run_spio_command(
            record,
            command,
            args,
            cwd=working_root,
            inline_workflow_artifacts=True,
        )
        if cleanup_overlay is not None:
            shutil.rmtree(cleanup_overlay, ignore_errors=True)
        return response

    def pack_project(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        args = ["--json", "pack", "--manifest-path", str(record.manifest_path)]
        package_name = body.get("package_name")
        if isinstance(package_name, str) and package_name:
            args.extend(["--package", package_name])
        output_path = body.get("output_path")
        if isinstance(output_path, str) and output_path:
            args.extend(["--output", output_path])
        return self._run_spio_command(record, "pack", args)

    def prepare_publish(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        args = ["--json", "publish", "--manifest-path", str(record.manifest_path), "--dry-run"]
        package_name = body.get("package_name")
        if isinstance(package_name, str) and package_name:
            args.extend(["--package", package_name])
        output_path = body.get("output_path")
        if isinstance(output_path, str) and output_path:
            args.extend(["--output", output_path])
        return self._run_spio_command(record, "publish", args)

    def publish_to_registry(self, workspace_id: str, body: dict[str, Any]) -> dict[str, Any]:
        record = self.records[workspace_id]
        args = ["--json", "publish", "--manifest-path", str(record.manifest_path)]
        package_name = body.get("package_name")
        if isinstance(package_name, str) and package_name:
            args.extend(["--package", package_name])
        output_path = body.get("output_path")
        if isinstance(output_path, str) and output_path:
            args.extend(["--output", output_path])
        registry_root = body.get("registry_root")
        if isinstance(registry_root, str) and registry_root:
            args.extend(["--registry", registry_root])
        else:
            raise RuntimeError("registry_root is required for hosted publish")
        return self._run_spio_command(record, "publish", args)

    def close_workspace(self, workspace_id: str) -> dict[str, Any]:
        record = self.records[workspace_id]
        record.close()
        return {"workspace": workspace_record_payload(record), "message": "workspace closed"}

    def export_workspace(self, workspace_id: str) -> dict[str, Any]:
        record = self.records[workspace_id]
        record.refresh_export()
        return {"workspace": workspace_record_payload(record), "message": "workspace export prepared"}

    def _run_spio_json(
        self,
        record: WorkspaceRecord,
        args: list[str],
        *,
        cwd: pathlib.Path | None = None,
    ) -> dict[str, Any]:
        result = subprocess.run(
            [str(self.spio_bin), *args],
            cwd=str(cwd or record.workspace_root),
            capture_output=True,
            text=True,
            env=self._workspace_env(record),
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"`{' '.join(args)}` failed with code {result.returncode}: "
                f"{(result.stderr or result.stdout).strip()}"
            )
        payload = decode_payload(result.stdout, result.stderr)
        if payload is None:
            raise RuntimeError(f"`{' '.join(args)}` did not emit JSON")
        return payload

    def _run_spio_command(
        self,
        record: WorkspaceRecord,
        command_name: str,
        args: list[str],
        *,
        cwd: pathlib.Path | None = None,
        inline_workflow_artifacts: bool = False,
    ) -> dict[str, Any]:
        record.touch()
        result = subprocess.run(
            [str(self.spio_bin), *args],
            cwd=str(cwd or record.workspace_root),
            capture_output=True,
            text=True,
            env=self._workspace_env(record),
        )
        payload = decode_payload(result.stdout, result.stderr)
        if inline_workflow_artifacts and payload is not None:
            payload = ensure_runtime_artifacts_inline(
                payload,
                workspace_root=cwd or record.workspace_root,
            )
        response: dict[str, Any] = {
            "workspace": workspace_record_payload(record),
            "command": command_name,
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
        }
        if result.returncode == 0:
            response["payload"] = payload
            response["message"] = (
                payload.get("message")
                if isinstance(payload, dict)
                else f"{command_name} completed."
            )
        else:
            response["error_payload"] = payload
            response["message"] = (
                payload.get("message")
                if isinstance(payload, dict)
                else f"{command_name} failed with code {result.returncode}."
            )
        return response

    def _workspace_env(self, record: WorkspaceRecord) -> dict[str, str]:
        env = dict(os.environ)
        env["SPIO_HOME"] = str(record.spio_home)
        return env

    def _workspace_id_from_path(self, path: str, *, suffix: str) -> str:
        prefix = f"{API_PREFIX}/workspaces/"
        if not path.startswith(prefix) or not path.endswith(suffix):
            raise RuntimeError(f"invalid hosted path: {path}")
        workspace_id = path[len(prefix) : -len(suffix)]
        if workspace_id not in self.records:
            raise KeyError(workspace_id)
        return workspace_id

    def _read_body_json(self, handler: BaseHTTPRequestHandler) -> dict[str, Any]:
        length = int(handler.headers.get("Content-Length", "0"))
        raw = handler.rfile.read(length) if length else b"{}"
        if not raw:
            return {}
        try:
            decoded = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError as err:
            raise RuntimeError("request body must be valid JSON") from err
        if not isinstance(decoded, dict):
            raise RuntimeError("request body must decode to a JSON object")
        return decoded

    def _write_json(
        self,
        handler: BaseHTTPRequestHandler,
        status: HTTPStatus,
        payload: dict[str, Any],
    ) -> None:
        encoded = json.dumps(payload).encode("utf-8")
        handler.send_response(status.value)
        handler.send_header("Content-Type", "application/json")
        handler.send_header("Content-Length", str(len(encoded)))
        handler.end_headers()
        handler.wfile.write(encoded)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Serve the repository-local Styio hosted control plane.",
    )
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument(
        "--spio-bin",
        default=str(DEFAULT_SPIO),
        help="spio entrypoint to delegate hosted actions to",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    spio_bin = pathlib.Path(args.spio_bin).resolve()
    if not spio_bin.exists():
        print(
            json.dumps({"message": f"spio binary not found: {spio_bin}"}),
            file=sys.stderr,
        )
        return 2
    server = HostedWorkspaceServer(
        spio_bin=spio_bin,
        bind=args.bind,
        port=args.port,
    )
    server.serve()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
