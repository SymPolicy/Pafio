#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/spio"

rsync -a \
  --exclude '.git/' \
  --exclude '.build/' \
  --exclude 'build/' \
  --exclude 'build-codex/' \
  --exclude '.spio/' \
  --exclude '__pycache__/' \
  --exclude '.pytest_cache/' \
  --exclude 'Testing/' \
  --exclude 'src-private/' \
  --exclude 'tests-private/' \
  --exclude 'docs-private/' \
  --exclude 'scripts-private/' \
  "$ROOT"/ "$TMP_DIR/spio"/

(
  cd "$TMP_DIR/spio"
  ./scripts/native-check.sh
)

echo "spio extractability check passed: $TMP_DIR/spio"
