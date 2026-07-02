#!/usr/bin/env bash
set -euo pipefail

PAFIO_BIN="${1:?expected pafio binary path}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ROOT="$(mktemp -d)"
LOG_FILE="$ROOT/http-server.log"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
  rm -rf "$ROOT"
}
trap cleanup EXIT

export PAFIO_HOME="$ROOT/.pafio-home"
V2_ROOT="$ROOT/registry-v2"
KEY_DIR="$ROOT/keys"
PACKAGE_ROOT="$ROOT/publish/util"
mkdir -p "$PACKAGE_ROOT/src"

cat >"$PACKAGE_ROOT/pafio.toml" <<'EOF'
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

cat >"$PACKAGE_ROOT/src/lib.styio" <<'EOF'
# util
EOF

python3 "$REPO_ROOT/scripts/registry-v2-keygen.py" --output-dir "$KEY_DIR" >/dev/null
python3 "$REPO_ROOT/scripts/registry-v2-publish.py" \
  --root "$V2_ROOT" \
  --key-dir "$KEY_DIR" \
  --manifest-path "$PACKAGE_ROOT/pafio.toml" \
  --pafio-bin "$PAFIO_BIN" \
  --registry-name "http-read-registry" >/dev/null

PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$V2_ROOT" >"$LOG_FILE" 2>&1 &
SERVER_PID="$!"

REGISTRY_URL="http://127.0.0.1:${PORT}"
for _ in $(seq 1 30); do
  if curl -fsS "${REGISTRY_URL}/config.json" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

curl -fsS "${REGISTRY_URL}/config.json" >/dev/null
curl -fsS "${REGISTRY_URL}/trust/root.json" >/dev/null
curl -fsS "${REGISTRY_URL}/index/acme/util.jsonl" >/dev/null

VERIFY_JSON="$(
  python3 "$REPO_ROOT/scripts/registry-v2-verify.py" \
    --root "$REGISTRY_URL"
)"

python3 - "$VERIFY_JSON" <<'PY'
import json
import sys

verified = json.loads(sys.argv[1])
assert verified["ok"] is True
assert verified["namespaces"] == 1
assert verified["index_files"] == 1
assert verified["releases"] == 1
assert verified["tree_size"] == 1
assert verified["root"].startswith("http://127.0.0.1:")
PY
