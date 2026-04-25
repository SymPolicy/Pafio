#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
DEPS_CACHE_DIR="$TMP_DIR/deps"

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

mkdir -p "$DEPS_CACHE_DIR"
if [ -d "$ROOT/build-codex/_deps/tomlplusplus-src" ]; then
  rsync -a "$ROOT/build-codex/_deps/tomlplusplus-src/" "$DEPS_CACHE_DIR/tomlplusplus-src/"
fi
if [ -d "$ROOT/build-codex/_deps/nlohmann_json-src" ]; then
  rsync -a "$ROOT/build-codex/_deps/nlohmann_json-src/" "$DEPS_CACHE_DIR/nlohmann_json-src/"
fi

(
  cd "$TMP_DIR/spio"
  env \
    FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="${DEPS_CACHE_DIR}/tomlplusplus-src" \
    FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="${DEPS_CACHE_DIR}/nlohmann_json-src" \
    ./scripts/native-check.sh
)

echo "spio extractability check passed: $TMP_DIR/spio"
