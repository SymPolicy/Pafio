#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
import sys

if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from spio_registry_v2 import publish_to_registry_v2, verify_registry_root  # noqa: E402
from spio_registry_v2.common import RegistryV2Error  # noqa: E402


BASE_PATH = "/api/spio-registry-control/v1"
MAX_REQUEST_BYTES = 1 * 1024 * 1024
REQUEST_TIMEOUT_SECONDS = 10.0


def registry_error_status(error: RegistryV2Error, *, operation: str) -> int:
    detail = str(error)
    if "already published" in detail or "already exists" in detail:
        return 409
    if operation == "publish":
        return 422
    if operation == "verify":
        return 422
    return 500


def load_json_request(handler: BaseHTTPRequestHandler) -> dict[str, Any]:
    content_length = handler.headers.get("Content-Length")
    if content_length is None:
        return {}
    try:
        length = int(content_length)
    except ValueError as err:
        raise ValueError("request body content-length must be an integer") from err
    if length < 0:
        raise ValueError("request body content-length must be non-negative")
    if length > MAX_REQUEST_BYTES:
        raise ValueError(f"request body exceeds {MAX_REQUEST_BYTES} bytes")
    try:
        body = handler.rfile.read(length)
    except TimeoutError as err:
        raise ValueError(f"request body read timed out after {REQUEST_TIMEOUT_SECONDS} seconds") from err
    if not body:
        return {}
    if len(body) != length:
        raise ValueError("request body was truncated")
    try:
        payload = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as err:
        raise ValueError("request body must be valid UTF-8 JSON") from err
    if not isinstance(payload, dict):
        raise ValueError("request body must be a JSON object")
    return payload


def success_envelope(message: str, payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "returncode": 0,
        "message": message,
        "stdout": "",
        "stderr": "",
        "payload": payload,
    }


def failure_envelope(message: str, detail: str, *, category: str, returncode: int = 17) -> dict[str, Any]:
    return {
        "returncode": returncode,
        "message": message,
        "stdout": "",
        "stderr": detail,
        "error_payload": {
            "category": category,
            "detail": detail,
        },
    }


class RegistryControlPlaneHandler(BaseHTTPRequestHandler):
    registry_root: str
    key_dir: str
    registry_name: str
    spio_bin: str

    def setup(self) -> None:
        super().setup()
        self.connection.settimeout(REQUEST_TIMEOUT_SECONDS)

    def _send_json(self, status_code: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: Any) -> None:
        return

    def do_GET(self) -> None:
        if self.path != f"{BASE_PATH}/status":
            self.send_error(404, "not found")
            return
        root_path = Path(self.registry_root)
        payload = {
            "registry_root": "<redacted>",
            "key_dir": "<redacted>",
            "registry_name": self.registry_name,
            "root_initialized": (root_path / "config.json").exists() and (root_path / "trust" / "root.json").exists(),
            "config_present": (root_path / "config.json").exists(),
            "root_metadata_present": (root_path / "trust" / "root.json").exists(),
            "publish_endpoint": f"{BASE_PATH}/publish",
            "verify_endpoint": f"{BASE_PATH}/verify",
        }
        self._send_json(200, success_envelope("registry control plane is ready", payload))

    def do_POST(self) -> None:
        if self.path == f"{BASE_PATH}/publish":
            self._handle_publish()
            return
        if self.path == f"{BASE_PATH}/verify":
            self._handle_verify()
            return
        self.send_error(404, "not found")

    def _handle_publish(self) -> None:
        try:
            request = load_json_request(self)
        except ValueError as err:
            self._send_json(400, failure_envelope("malformed registry publish request", str(err), category="UsageError", returncode=2))
            return
        try:
            payload = publish_to_registry_v2(
                self.registry_root,
                self.key_dir,
                archive_path_value=request.get("archive_path"),
                manifest_path_value=request.get("manifest_path"),
                spio_bin_value=self.spio_bin,
                package_name=request.get("package"),
                output_path_value=request.get("output_path"),
                registry_name=self.registry_name,
                publisher_id=request.get("publisher_id") or "control-plane",
            )
        except RegistryV2Error as err:
            self._send_json(
                registry_error_status(err, operation="publish"),
                failure_envelope("registry publish failed", str(err), category="PublishError"),
            )
            return
        self._send_json(200, success_envelope("published registry v2 release", payload))

    def _handle_verify(self) -> None:
        try:
            request = load_json_request(self)
        except ValueError as err:
            self._send_json(400, failure_envelope("malformed registry verify request", str(err), category="UsageError", returncode=2))
            return
        if request not in ({},):
            self._send_json(
                400,
                failure_envelope(
                    "malformed registry verify request",
                    "verify request must be an empty JSON object",
                    category="UsageError",
                    returncode=2,
                ),
            )
            return
        try:
            payload = verify_registry_root(self.registry_root)
        except RegistryV2Error as err:
            self._send_json(
                registry_error_status(err, operation="verify"),
                failure_envelope("registry verification failed", str(err), category="VerifyError"),
            )
            return
        self._send_json(200, success_envelope("verified registry v2 root", payload))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the local HTTP control plane for a spio registry v2 root.")
    parser.add_argument("--root", required=True, help="Local directory bound as the registry v2 static root.")
    parser.add_argument("--key-dir", required=True, help="Directory containing the registry v2 role keys.")
    parser.add_argument("--registry-name", default="spio-registry-v2", help="Registry name used when the root is initialized.")
    parser.add_argument("--spio-bin", default=str(ROOT / "scripts" / "spio"), help="spio executable used for dry-run publish preparation.")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    RegistryControlPlaneHandler.registry_root = str(Path(args.root).resolve())
    RegistryControlPlaneHandler.key_dir = str(Path(args.key_dir).resolve())
    RegistryControlPlaneHandler.registry_name = args.registry_name
    RegistryControlPlaneHandler.spio_bin = str(Path(args.spio_bin).resolve())
    server = ThreadingHTTPServer((args.bind, args.port), RegistryControlPlaneHandler)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
