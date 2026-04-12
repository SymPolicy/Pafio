#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

FAKE_STYIO="$TMP/fake-styio"
cat >"$FAKE_STYIO" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "--machine-info=json" ]; then
  printf '%s\n' '{"tool":"styio","compiler_version":"0.0.5","channel":"stable","supported_contracts":{"compile_plan":[]},"capabilities":["machine_info_json","single_file_entry","jsonl_diagnostics"],"edition_max":"2026"}'
  exit 0
fi

printf '%s\n' 'unexpected invocation' >&2
exit 64
EOF
chmod +x "$FAKE_STYIO"

export SPIO_HOME="$TMP/spio-home"
python3 "$ROOT/scripts/styio-interface-gate.py" --styio-bin "$FAKE_STYIO" --json >"$TMP/out.json"
python3 - "$TMP/out.json" <<'PY'
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text())
assert payload["ok"] is True, payload
assert payload["machine_info"]["tool"] == "styio", payload
assert payload["require_compile_plan"] is False, payload
PY
