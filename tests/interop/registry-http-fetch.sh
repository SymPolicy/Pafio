#!/usr/bin/env bash
set -euo pipefail

SPIO_BIN="${1:?expected spio binary path}"
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

"$SPIO_BIN" publish --manifest-path "$ROOT/publish/util/spio.toml" --registry "$REGISTRY_ROOT" >/dev/null

PORT="$(
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$REGISTRY_ROOT" >"$LOG_FILE" 2>&1 &
SERVER_PID="$!"

REGISTRY_URL="http://127.0.0.1:${PORT}"
for _ in $(seq 1 30); do
  if curl -fsS "${REGISTRY_URL}/config.json" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
curl -fsS "${REGISTRY_URL}/config.json" >/dev/null

python3 - "$REGISTRY_URL" "$REGISTRY_ROOT/trust/root.json" "$ROOT/registry-descriptor.json" <<'PY'
import hashlib
import json
import pathlib
import sys

registry_url = sys.argv[1]
root_metadata = pathlib.Path(sys.argv[2]).read_bytes()
descriptor_path = pathlib.Path(sys.argv[3])
descriptor_path.write_text(
    json.dumps(
        {
            "schema_version": 1,
            "registry_name": "interop-registry",
            "registry_root": registry_url,
            "control_plane_base_url": "interop-local-fixture",
            "root_sha256": hashlib.sha256(root_metadata).hexdigest(),
            "issued_at": "2026-05-02T00:00:00Z",
            "expires": "2026-06-02T00:00:00Z",
        },
        sort_keys=True,
    )
    + "\n",
    encoding="utf-8",
)
PY
"$SPIO_BIN" --json registry trust import "$ROOT/registry-descriptor.json" >/dev/null

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

ONLINE_JSON="$("$SPIO_BIN" --json fetch --manifest-path "$ROOT/spio.toml")"
python3 - "$ONLINE_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "fetch"
assert payload["registry_packages"] == 1
assert payload["packages"] == 2
PY

kill "$SERVER_PID" >/dev/null 2>&1 || true
wait "$SERVER_PID" >/dev/null 2>&1 || true
SERVER_PID=""

OFFLINE_JSON="$("$SPIO_BIN" --json fetch --manifest-path "$ROOT/spio.toml" --offline)"
python3 - "$OFFLINE_JSON" <<'PY'
import json
import sys
payload = json.loads(sys.argv[1])
assert payload["command"] == "fetch"
assert payload["registry_packages"] == 1
assert payload["offline"] is True
PY
