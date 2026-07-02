#!/usr/bin/env bash
set -euo pipefail

PAFIO_BIN="${1:?expected pafio binary path}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TMP_ROOT="$(mktemp -d)"
CONTROL_LOG="$TMP_ROOT/control-plane.log"
STATIC_LOG="$TMP_ROOT/static-server.log"
CONTROL_PID=""
STATIC_PID=""

cleanup() {
  if [[ -n "$CONTROL_PID" ]]; then
    kill "$CONTROL_PID" >/dev/null 2>&1 || true
    wait "$CONTROL_PID" >/dev/null 2>&1 || true
  fi
  if [[ -n "$STATIC_PID" ]]; then
    kill "$STATIC_PID" >/dev/null 2>&1 || true
    wait "$STATIC_PID" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

CONTROL_PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

STATIC_PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

REGISTRY_ROOT="$TMP_ROOT/registry"
KEY_DIR="$TMP_ROOT/keys"
mkdir -p "$REGISTRY_ROOT"

python3 "$ROOT_DIR/scripts/registry-v2-keygen.py" --output-dir "$KEY_DIR" >/dev/null

python3 "$ROOT_DIR/scripts/registry-v2-control-plane-server.py" \
  --root "$REGISTRY_ROOT" \
  --key-dir "$KEY_DIR" \
  --pafio-bin "$PAFIO_BIN" \
  --bind 127.0.0.1 \
  --port "$CONTROL_PORT" >"$CONTROL_LOG" 2>&1 &
CONTROL_PID="$!"

python3 -m http.server "$STATIC_PORT" --bind 127.0.0.1 --directory "$REGISTRY_ROOT" >"$STATIC_LOG" 2>&1 &
STATIC_PID="$!"

CONTROL_BASE="http://127.0.0.1:${CONTROL_PORT}/api/pafio-registry-control/v1"
STATIC_ROOT="http://127.0.0.1:${STATIC_PORT}"
for _ in $(seq 1 30); do
  if curl -fsS "${CONTROL_BASE}/status" >/dev/null 2>&1 && curl -fsS "${STATIC_ROOT}/" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS "${CONTROL_BASE}/status" >/dev/null
curl -fsS "${STATIC_ROOT}/" >/dev/null

OUT_JSON="$TMP_ROOT/out.json"
python3 "$ROOT_DIR/scripts/registry-server-gate.py" \
  --publish-root "$CONTROL_BASE" \
  --fetch-root "$STATIC_ROOT" \
  --pafio-bin "$PAFIO_BIN" \
  --json >"$OUT_JSON"

python3 - "$OUT_JSON" <<'PY'
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
assert payload["ok"] is True
assert payload["publish_root"].endswith("/api/pafio-registry-control/v1")
assert payload["fetch_root"].startswith("http://127.0.0.1:")
step_names = [step["name"] for step in payload["steps"]]
assert step_names == ["publish", "republish_conflict", "fetch"]
assert payload["steps"][0]["ok"] is True
assert payload["steps"][1]["ok"] is False
assert payload["steps"][2]["ok"] is True
assert payload["validation_errors"] == []
PY
