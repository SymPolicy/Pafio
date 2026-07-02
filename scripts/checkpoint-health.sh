#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/checkpoint-health.sh [options]

Run the repository-wide checkpoint health gate for pafio.

Options:
  --build-dir <dir>      Native build directory forwarded through PAFIO_BUILD_DIR
  --styio-bin <path>     Optional external styio binary for compatibility probing
  -h, --help             Show this help
USAGE
}

log() {
  echo "[checkpoint-health] $*"
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${PAFIO_BUILD_DIR:-$ROOT/build-codex}"
STYIO_BIN=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --styio-bin)
      STYIO_BIN="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -n "$STYIO_BIN" ]]; then
  log "preflight with external styio"
  PAFIO_BUILD_DIR="$BUILD_DIR" ./scripts/preflight-readiness-check.py --styio-bin "$STYIO_BIN"
else
  log "native check"
  PAFIO_BUILD_DIR="$BUILD_DIR" ./scripts/native-check.sh
  log "extractability check"
  ./scripts/extractability-check.sh
fi

log "all checks passed"
