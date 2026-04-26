#!/usr/bin/env bash
set -euo pipefail

SPIO_BIN="${1:?expected spio binary path}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ROOT="$(mktemp -d)"

cleanup() {
  rm -rf "$ROOT"
}
trap cleanup EXIT

export SPIO_HOME="$ROOT/.spio-home"
KEY_DIR="$ROOT/keys"
V2_ROOT="$ROOT/registry-v2"
PACKAGE_ROOT="$ROOT/publish/util"
mkdir -p "$PACKAGE_ROOT/src"

cat >"$PACKAGE_ROOT/spio.toml" <<'EOF'
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

[dependencies]
base = { package = "acme/base", version = "1.0.0", registry = "https://packages.example.test" }
EOF

cat >"$PACKAGE_ROOT/src/lib.styio" <<'EOF'
# util
EOF

python3 "$REPO_ROOT/scripts/registry-v2-keygen.py" --output-dir "$KEY_DIR" >/dev/null

FIRST_JSON="$(
  python3 "$REPO_ROOT/scripts/registry-v2-publish.py" \
    --root "$V2_ROOT" \
    --key-dir "$KEY_DIR" \
    --manifest-path "$PACKAGE_ROOT/spio.toml" \
    --spio-bin "$SPIO_BIN" \
    --publisher-id "interop-test"
)"

python3 - "$FIRST_JSON" <<'PY'
import json
import sys

payload = json.loads(sys.argv[1])
assert payload["ok"] is True
assert payload["created_root"] is True
assert payload["package"] == "acme/util"
assert payload["version"] == "0.2.0"
assert payload["sequence"] == 1
assert payload["dependencies"] == 1
assert payload["dev_dependencies"] == 0
candidate = payload["candidate"]
assert candidate["command"] == "publish"
assert candidate["mode"] == "dry-run"
PY

python3 - "$PACKAGE_ROOT/spio.toml" <<'PY'
from pathlib import Path
path = Path(__import__("sys").argv[1])
text = path.read_text(encoding="utf-8")
path.write_text(text.replace('version = "0.2.0"', 'version = "0.3.0"'), encoding="utf-8")
PY

SECOND_JSON="$(
  python3 "$REPO_ROOT/scripts/registry-v2-publish.py" \
    --root "$V2_ROOT" \
    --key-dir "$KEY_DIR" \
    --manifest-path "$PACKAGE_ROOT/spio.toml" \
    --spio-bin "$SPIO_BIN" \
    --publisher-id "interop-test"
)"

python3 - "$SECOND_JSON" <<'PY'
import json
import sys

payload = json.loads(sys.argv[1])
assert payload["ok"] is True
assert payload["created_root"] is False
assert payload["version"] == "0.3.0"
assert payload["sequence"] == 2
assert payload["snapshot_version"] >= 2
PY

if python3 "$REPO_ROOT/scripts/registry-v2-publish.py" \
  --root "$V2_ROOT" \
  --key-dir "$KEY_DIR" \
  --manifest-path "$PACKAGE_ROOT/spio.toml" \
  --spio-bin "$SPIO_BIN" \
  --publisher-id "interop-test" >/dev/null 2>&1; then
  echo "duplicate registry v2 publish unexpectedly succeeded" >&2
  exit 1
fi

VERIFY_JSON="$(
  python3 "$REPO_ROOT/scripts/registry-v2-verify.py" \
    --root "$V2_ROOT"
)"

python3 - "$VERIFY_JSON" <<'PY'
import json
import sys

verified = json.loads(sys.argv[1])
assert verified["ok"] is True
assert verified["releases"] == 2
assert verified["tree_size"] == 2
PY

test -f "$V2_ROOT/index/acme/util.jsonl"
LINES="$(wc -l < "$V2_ROOT/index/acme/util.jsonl")"
test "$LINES" -eq 2
