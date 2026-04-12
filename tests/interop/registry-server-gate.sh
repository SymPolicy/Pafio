#!/usr/bin/env bash
set -euo pipefail

SPIO_BIN="${1:?expected spio binary path}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TMP_ROOT="$(mktemp -d)"
LOG_FILE="$TMP_ROOT/http-server.log"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

REGISTRY_ROOT="$TMP_ROOT/registry"
mkdir -p "$REGISTRY_ROOT"

python3 "$SCRIPT_DIR/registry-http-server.py" --root "$REGISTRY_ROOT" --bind 127.0.0.1 --port "$PORT" >"$LOG_FILE" 2>&1 &
SERVER_PID="$!"

REGISTRY_URL="http://127.0.0.1:${PORT}"
for _ in $(seq 1 30); do
  if curl -fsS "${REGISTRY_URL}/" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS "${REGISTRY_URL}/" >/dev/null

OUT_JSON="$TMP_ROOT/out.json"
python3 "$ROOT_DIR/scripts/registry-server-gate.py" \
  --publish-root "$REGISTRY_URL" \
  --fetch-root "$REGISTRY_URL" \
  --spio-bin "$SPIO_BIN" \
  --json >"$OUT_JSON"

python3 - "$OUT_JSON" <<'PY'
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
assert payload["ok"] is True
assert payload["publish_root"].startswith("http://127.0.0.1:")
assert payload["fetch_root"] == payload["publish_root"]
step_names = [step["name"] for step in payload["steps"]]
assert step_names == ["publish", "republish_conflict", "fetch"]
assert payload["steps"][0]["ok"] is True
assert payload["steps"][1]["ok"] is False
assert payload["steps"][2]["ok"] is True
assert payload["validation_errors"] == []
PY
