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
  printf '%s\n' '{"tool":"styio","compiler_version":"0.0.5","channel":"stable","active_integration_phase":"compile-plan-live","supported_contracts":{"machine_info":[1],"jsonl_diagnostics":[1],"compile_plan":[1],"runtime_events":[]},"supported_contract_versions":{"machine_info":[1],"jsonl_diagnostics":[1],"compile_plan":[1],"runtime_events":[]},"supported_adapter_modes":["cli"],"feature_flags":{"single_file_entry":true,"jsonl_diagnostics":true,"compile_plan_consumer":true,"project_execution_via_compile_plan":true,"runtime_event_stream":false},"capabilities":["machine_info_json","single_file_entry","jsonl_diagnostics"],"edition_max":"2026"}'
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
assert payload["machine_info"]["supported_contracts"]["compile_plan"] == [1], payload
assert payload["require_compile_plan"] is False, payload
spio_check = next(step for step in payload["steps"] if step["name"] == "spio_check")
spio_payload = json.loads(spio_check["stdout"])
assert spio_payload["styio"]["integration_phase"] == "compile-plan-live", spio_payload
PY
