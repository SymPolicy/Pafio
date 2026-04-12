#!/usr/bin/env bash
set -euo pipefail

SPIO_BIN="${1:?expected spio binary path}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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

export SPIO_HOME="$ROOT/.spio-home"
REGISTRY_ROOT="$ROOT/registry"
mkdir -p "$REGISTRY_ROOT"

PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

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

mkdir -p "$ROOT/publish/util/src"
cat >"$ROOT/publish/util/spio.toml" <<'EOF'
[spio]
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

PUBLISH_JSON="$("$SPIO_BIN" --json publish --manifest-path "$ROOT/publish/util/spio.toml" --registry "$REGISTRY_URL")"
python3 - "$PUBLISH_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "publish"
assert payload["mode"] == "publish"
assert payload["transport"] == "http"
assert payload["package"] == "acme/util"
assert payload["registry_entry_url"].endswith("/index/acme/util/0.2.0.json")
assert payload["registry_blob_url"].endswith(".tar")
assert payload["registry_marker_url"].endswith("/spio-registry.json")
PY

curl -fsS "${REGISTRY_URL}/spio-registry.json" >/dev/null
curl -fsS "${REGISTRY_URL}/index/acme/util/0.2.0.json" >/dev/null

cat >"$ROOT/spio.toml" <<EOF
[spio]
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

FETCH_JSON="$("$SPIO_BIN" --json fetch --manifest-path "$ROOT/spio.toml")"
python3 - "$FETCH_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "fetch"
assert payload["registry_packages"] == 1
assert payload["packages"] == 2
PY

if "$SPIO_BIN" publish --manifest-path "$ROOT/publish/util/spio.toml" --registry "$REGISTRY_URL" >/dev/null 2>&1; then
  echo "duplicate remote publish unexpectedly succeeded" >&2
  exit 1
fi
