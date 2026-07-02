#!/usr/bin/env bash
set -euo pipefail

PAFIO_BIN="${1:?expected pafio binary path}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TMP_ROOT="$(mktemp -d)"
WRITE_SERVER_PID=""
READ_SERVER_PID=""
WRITE_LOG="$TMP_ROOT/write-server.log"
READ_LOG="$TMP_ROOT/read-server.log"

cleanup() {
  if [[ -n "$WRITE_SERVER_PID" ]]; then
    kill "$WRITE_SERVER_PID" >/dev/null 2>&1 || true
    wait "$WRITE_SERVER_PID" >/dev/null 2>&1 || true
  fi
  if [[ -n "$READ_SERVER_PID" ]]; then
    kill "$READ_SERVER_PID" >/dev/null 2>&1 || true
    wait "$READ_SERVER_PID" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

export PAFIO_HOME="$TMP_ROOT/.pafio-home"
KEY_DIR="$TMP_ROOT/keys"
WRITE_ROOT="$TMP_ROOT/upload-root"
READ_ROOT="$TMP_ROOT/read-root"
mkdir -p "$WRITE_ROOT" "$READ_ROOT" "$TMP_ROOT/publish/util/src"

write_port="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

read_port="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

python3 "$ROOT_DIR/scripts/registry-v2-keygen.py" --output-dir "$KEY_DIR" >/dev/null

python3 "$ROOT_DIR/scripts/registry-v2-control-plane-server.py" \
  --root "$WRITE_ROOT" \
  --key-dir "$KEY_DIR" \
  --pafio-bin "$PAFIO_BIN" \
  --bind 127.0.0.1 \
  --port "$write_port" >"$WRITE_LOG" 2>&1 &
WRITE_SERVER_PID="$!"

python3 -m http.server "$read_port" --bind 127.0.0.1 --directory "$READ_ROOT" >"$READ_LOG" 2>&1 &
READ_SERVER_PID="$!"

WRITE_URL="http://127.0.0.1:${write_port}/api/pafio-registry-control/v1"
READ_URL="http://127.0.0.1:${read_port}"

for _ in $(seq 1 30); do
  if curl -fsS "${WRITE_URL}/status" >/dev/null 2>&1 && curl -fsS "${READ_URL}/" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS "${WRITE_URL}/status" >/dev/null
curl -fsS "${READ_URL}/" >/dev/null

cat >"$TMP_ROOT/publish/util/pafio.toml" <<'EOF'
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

cat >"$TMP_ROOT/publish/util/src/lib.styio" <<'EOF'
# util
EOF

PUBLISH_JSON="$("$PAFIO_BIN" --json publish --manifest-path "$TMP_ROOT/publish/util/pafio.toml" --registry "$WRITE_URL")"
python3 - "$PUBLISH_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "publish"
assert payload["mode"] == "publish"
assert payload["transport"] == "http"
assert payload["registry_protocol"] == "v2"
assert payload["package"] == "acme/util"
assert payload["registry_index_path"].endswith("index/acme/util.jsonl")
PY

cat >"$TMP_ROOT/pafio.toml" <<EOF
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
util = { package = "acme/util", version = "0.2.0", registry = "${READ_URL}" }
EOF

if "$PAFIO_BIN" fetch --manifest-path "$TMP_ROOT/pafio.toml" >/dev/null 2>&1; then
  echo "fetch unexpectedly succeeded from read origin before promotion" >&2
  exit 1
fi

PROMOTE_JSON="$(
  python3 "$ROOT_DIR/scripts/registry-promote.py" \
    --source-root "$WRITE_ROOT" \
    --dest-root "$READ_ROOT" \
    --package "acme/util" \
    --version "0.2.0" \
    --json
)"
python3 - "$PROMOTE_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["ok"] is True
assert payload["packages"] == ["acme/util@0.2.0"]
assert payload["files_total"] >= 1
assert payload["verified"]["ok"] is True
PY

FETCH_JSON="$("$PAFIO_BIN" --json fetch --manifest-path "$TMP_ROOT/pafio.toml")"
python3 - "$FETCH_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "fetch"
assert payload["registry_packages"] == 1
assert payload["packages"] == 2
PY

if "$PAFIO_BIN" publish --manifest-path "$TMP_ROOT/publish/util/pafio.toml" --registry "$WRITE_URL" >/dev/null 2>&1; then
  echo "duplicate publish to write origin unexpectedly succeeded" >&2
  exit 1
fi
