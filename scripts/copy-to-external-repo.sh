#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-$HOME/DevSpace/Unka-Malloc/styio-spio}"

mkdir -p "$TARGET"

rsync -a \
  --delete \
  --exclude '.git/' \
  --exclude '.build/' \
  --exclude 'build/' \
  --exclude 'build-codex/' \
  --exclude '.spio/' \
  --exclude '__pycache__/' \
  --exclude '.pytest_cache/' \
  --exclude 'Testing/' \
  "$ROOT"/ "$TARGET"/

echo "copied spio subtree to: $TARGET"
