#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$#" -ne 1 ]]; then
  echo "usage: $0 <target-dir>" >&2
  exit 64
fi
TARGET="$1"

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

echo "copied pafio subtree to: $TARGET"
