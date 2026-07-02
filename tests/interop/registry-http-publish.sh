#!/usr/bin/env bash
set -euo pipefail

PAFIO_BIN="${1:?expected pafio binary path}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ROOT="$(mktemp -d)"
CONTROL_LOG="$ROOT/control-plane.log"
STATIC_LOG="$ROOT/static-server.log"
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
  rm -rf "$ROOT"
}
trap cleanup EXIT

export PAFIO_HOME="$ROOT/.pafio-home"
KEY_DIR="$ROOT/keys"
REGISTRY_ROOT="$ROOT/registry-v2"
mkdir -p "$REGISTRY_ROOT"

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

python3 "$REPO_ROOT/scripts/registry-v2-keygen.py" --output-dir "$KEY_DIR" >/dev/null

CONTROL_BASE="http://127.0.0.1:${CONTROL_PORT}/api/pafio-registry-control/v1"
REGISTRY_URL="http://127.0.0.1:${STATIC_PORT}"

python3 "$REPO_ROOT/scripts/registry-v2-control-plane-server.py" \
  --root "$REGISTRY_ROOT" \
  --key-dir "$KEY_DIR" \
  --pafio-bin "$PAFIO_BIN" \
  --read-root-url "$REGISTRY_URL" \
  --control-plane-base-url "$CONTROL_BASE" \
  --bind 127.0.0.1 \
  --port "$CONTROL_PORT" >"$CONTROL_LOG" 2>&1 &
CONTROL_PID="$!"

python3 -m http.server "$STATIC_PORT" --bind 127.0.0.1 --directory "$REGISTRY_ROOT" >"$STATIC_LOG" 2>&1 &
STATIC_PID="$!"

for _ in $(seq 1 30); do
  if curl -fsS "${CONTROL_BASE}/status" >/dev/null 2>&1 && curl -fsS "${REGISTRY_URL}/" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS "${CONTROL_BASE}/status" >/dev/null
curl -fsS "${REGISTRY_URL}/" >/dev/null

mkdir -p "$ROOT/publish/util/src"
cat >"$ROOT/publish/util/pafio.toml" <<'EOF'
[pafio]
manifest-version = 1

[package]
name = "acme/util"
version = "0.2.0"
edition = "2026"
publish = true

[toolchain]
channel = "nightly"
implicit-std = true

[lib]
path = "src/lib.styio"
EOF
cat >"$ROOT/publish/util/src/lib.styio" <<'EOF'
# util
EOF

PUBLISH_JSON="$("$PAFIO_BIN" --json publish --manifest-path "$ROOT/publish/util/pafio.toml" --registry "$CONTROL_BASE")"
python3 - "$PUBLISH_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "publish"
assert payload["mode"] == "publish"
assert payload["transport"] == "http"
assert payload["registry_protocol"] == "v2"
assert payload["package"] == "acme/util"
assert payload["control_plane_base_url"].endswith("/api/pafio-registry-control/v1")
assert payload["publish_endpoint"].endswith("/api/pafio-registry-control/v1/publish")
assert payload["registry_index_path"].endswith("index/acme/util.jsonl")
assert payload["registry_artifact_path"].endswith(".pafio.src.tar")
assert payload["registry_log_leaf_path"].endswith("log/leaves/000000000001.json")
assert payload["created_root"] is True
PY

curl -fsS "${REGISTRY_URL}/config.json" >/dev/null
curl -fsS "${REGISTRY_URL}/index/acme/util.jsonl" >/dev/null
"$PAFIO_BIN" --json registry trust import "${CONTROL_BASE}/descriptor" >/dev/null

cat >"$ROOT/pafio.toml" <<EOF
[pafio]
manifest-version = 1

[package]
name = "acme/app"
version = "0.1.0"
edition = "2026"

[toolchain]
channel = "nightly"
implicit-std = true

[[bin]]
name = "app"
path = "src/main.styio"

[dependencies]
util = { package = "acme/util", version = "0.2.0", registry = "${REGISTRY_URL}" }
EOF

FETCH_JSON="$("$PAFIO_BIN" --json fetch --manifest-path "$ROOT/pafio.toml")"
python3 - "$FETCH_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "fetch"
assert payload["registry_packages"] == 1
assert payload["packages"] == 2
PY

if "$PAFIO_BIN" publish --manifest-path "$ROOT/publish/util/pafio.toml" --registry "$CONTROL_BASE" >/dev/null 2>&1; then
  echo "duplicate remote publish unexpectedly succeeded" >&2
  exit 1
fi
