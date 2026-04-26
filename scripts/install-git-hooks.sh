#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOOKS_DIR="$ROOT/.git/hooks"
PRE_PUSH="$HOOKS_DIR/pre-push"

if [[ ! -d "$HOOKS_DIR" ]]; then
  echo "missing .git/hooks under: $ROOT" >&2
  exit 1
fi

cat >"$PRE_PUSH" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
python3 scripts/submit-gate.py --profile pre-push
EOF

chmod +x "$PRE_PUSH"
echo "installed git pre-push hook: $PRE_PUSH"
