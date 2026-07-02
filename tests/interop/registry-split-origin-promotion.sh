#!/usr/bin/env bash
set -euo pipefail

PAFIO_BIN="${1:?expected pafio binary path}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_ROOT="$(mktemp -d)"
SERVER_PID=""
LOG_FILE="$TMP_ROOT/http-server.log"

cleanup() {
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

export PAFIO_HOME="$TMP_ROOT/.pafio-home"
WRITE_ROOT="$TMP_ROOT/upload-root"
READ_ROOT="$TMP_ROOT/read-root"
mkdir -p "$WRITE_ROOT" "$READ_ROOT" "$TMP_ROOT/publish/util/src"

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

"$PAFIO_BIN" publish --manifest-path "$TMP_ROOT/publish/util/pafio.toml" --registry "$WRITE_ROOT" >/dev/null

PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$READ_ROOT" >"$LOG_FILE" 2>&1 &
SERVER_PID="$!"

REGISTRY_URL="http://127.0.0.1:${PORT}"
for _ in $(seq 1 30); do
  if curl -fsS "${REGISTRY_URL}/" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS "${REGISTRY_URL}/" >/dev/null

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
util = { package = "acme/util", version = "0.2.0", registry = "${REGISTRY_URL}" }
EOF

if "$PAFIO_BIN" fetch --manifest-path "$TMP_ROOT/pafio.toml" >/dev/null 2>&1; then
  echo "fetch unexpectedly succeeded before promotion" >&2
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
assert payload["files_copied"] >= 1
assert payload["verified"]["ok"] is True
PY

PROMOTE_AGAIN_JSON="$(
  python3 "$ROOT_DIR/scripts/registry-promote.py" \
    --source-root "$WRITE_ROOT" \
    --dest-root "$READ_ROOT" \
    --package "acme/util" \
    --version "0.2.0" \
    --json
)"
python3 - "$PROMOTE_AGAIN_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["ok"] is True
assert payload["packages"] == ["acme/util@0.2.0"]
assert payload["files_total"] >= 1
assert payload["files_reused"] >= 1
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
