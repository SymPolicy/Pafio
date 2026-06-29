#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-<spio-workspace>}"

mkdir -p "$TARGET"

EXCLUDE_ARGS=()
while IFS= read -r pattern; do
  [[ -z "$pattern" ]] && continue
  EXCLUDE_ARGS+=(--exclude "$pattern")
done < <(python3 "$ROOT/scripts/artifact-policy-rsync-excludes.py")

rsync -a \
  --delete \
  "${EXCLUDE_ARGS[@]}" \
  "$ROOT"/ "$TARGET"/

echo "copied spio subtree to: $TARGET"
