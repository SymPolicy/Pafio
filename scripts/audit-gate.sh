#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUDITOR_ROOT="${GENERAL_AUDITOR_ROOT:-}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --audit-root) AUDITOR_ROOT="${2:?Missing auditor root}"; shift 2 ;;
    -h|--help) echo 'Usage: scripts/audit-gate.sh [--audit-root <trusted General-Auditor checkout>]'; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done
if [ -z "${AUDITOR_ROOT:-}" ]; then
  AUDITOR_ROOT="$(git -C "$ROOT" config --local --get generalAuditor.root || true)"
fi
case "$AUDITOR_ROOT" in
  /*) ;;
  *) echo 'General-Auditor requires an absolute trusted root; use GENERAL_AUDITOR_ROOT or local git config generalAuditor.root.' >&2; exit 2 ;;
esac
if [ ! -f "$AUDITOR_ROOT/action_entry.py" ] || [ ! -f "$AUDITOR_ROOT/profiles/SymPolicy/Pafio.json" ]; then
  echo 'General-Auditor root must contain action_entry.py and the exact repository profile.' >&2
  exit 2
fi
audit_command=scan
for ci_flag in "${CI:-}" "${GITHUB_ACTIONS:-}"; do
  case "$ci_flag" in
    ""|0|[Ff][Aa][Ll][Ss][Ee]|[Nn][Oo]) ;;
    *) audit_command=check ;;
  esac
done
audit_status=0
python3 -I "$AUDITOR_ROOT/action_entry.py" "$audit_command" --policy-root "$AUDITOR_ROOT" \
  --directory "$ROOT" --repository "SymPolicy/Pafio" --scope history || audit_status=$?
python3 -I "$AUDITOR_ROOT/action_entry.py" "$audit_command" --policy-root "$AUDITOR_ROOT" \
  --directory "$ROOT" --repository "SymPolicy/Pafio" --scope worktree || audit_status=$?
exit "$audit_status"
