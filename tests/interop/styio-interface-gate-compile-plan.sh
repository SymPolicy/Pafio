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
  printf '%s\n' '{"tool":"styio","compiler_version":"0.0.5","channel":"stable","supported_contracts":{"compile_plan":[1]},"capabilities":["machine_info_json","single_file_entry","jsonl_diagnostics"],"edition_max":"2026"}'
  exit 0
fi

if [ "${1:-}" = "--compile-plan" ] && [ -n "${2:-}" ]; then
  python3 - "$2" <<'PY'
import json
import pathlib
import sys

plan = json.loads(pathlib.Path(sys.argv[1]).read_text())
outputs = plan["outputs"]
for key in ("build_root", "artifact_dir", "diag_dir"):
    pathlib.Path(outputs[key]).mkdir(parents=True, exist_ok=True)
(pathlib.Path(outputs["artifact_dir"]) / "interop.artifact").write_text("ok\n")
(pathlib.Path(outputs["diag_dir"]) / "compile.jsonl").write_text("")
PY
  exit 0
fi

printf '%s\n' 'unexpected invocation' >&2
exit 64
EOF
chmod +x "$FAKE_STYIO"

export SPIO_HOME="$TMP/spio-home"
python3 "$ROOT/scripts/styio-interface-gate.py" --styio-bin "$FAKE_STYIO" --require-compile-plan --json >"$TMP/out.json"
python3 - "$TMP/out.json" <<'PY'
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text())
assert payload["ok"] is True, payload
assert payload["require_compile_plan"] is True, payload
step_names = [step["name"] for step in payload["steps"]]
assert "compile_plan_execute" in step_names, payload
PY
