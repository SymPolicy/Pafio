#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/delivery-gate.sh [options]

Run the common Styio delivery floor by composing repository hygiene, the docs
gate, external audit, and checkpoint health into one entrypoint.

Options:
  --mode <checkpoint|push>  Delivery mode (default: checkpoint)
  --base <ref>              Base ref for team-docs-gate branch checks
  --range <rev-range>       Explicit revision range for repo-hygiene push mode
  --skip-health             Skip checkpoint-health (docs/process-only deliveries)
  --skip-audit              Skip external audit gate
  --audit-bin <path>        Explicit audit executable
  --styio-bin <path>        Optional external styio binary for checkpoint-health
  --build-dir <dir>         Forwarded to checkpoint-health
  -h, --help                Show this help
USAGE
}

log() {
  echo "[delivery-gate] $*"
}

run_cmd() {
  log "$*"
  "$@"
}

default_upstream_base() {
  git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null || true
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE="checkpoint"
BASE_REF=""
REV_RANGE=""
RUN_HEALTH=1
RUN_AUDIT=1
AUDIT_BIN=""
STYIO_BIN=""
BUILD_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      MODE="$2"
      shift 2
      ;;
    --base)
      BASE_REF="$2"
      shift 2
      ;;
    --range)
      REV_RANGE="$2"
      shift 2
      ;;
    --skip-health)
      RUN_HEALTH=0
      shift
      ;;
    --skip-audit)
      RUN_AUDIT=0
      shift
      ;;
    --audit-bin)
      AUDIT_BIN="$2"
      shift 2
      ;;
    --styio-bin)
      STYIO_BIN="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
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

REPO_CMD=(python3 scripts/repo-hygiene-gate.py)
DOCS_GATE_CMD=(./scripts/docs-gate.sh)
AUDIT_CMD=(./scripts/audit-gate.sh)
HEALTH_CMD=(./scripts/checkpoint-health.sh)

if [[ -n "$AUDIT_BIN" ]]; then
  AUDIT_CMD+=(--audit-bin "$AUDIT_BIN")
fi
if [[ -n "$BUILD_DIR" ]]; then
  HEALTH_CMD+=(--build-dir "$BUILD_DIR")
fi
if [[ -n "$STYIO_BIN" ]]; then
  HEALTH_CMD+=(--styio-bin "$STYIO_BIN")
fi

case "$MODE" in
  checkpoint)
    REPO_CMD+=(--mode staged)
    DOCS_GATE_CMD+=(--mode staged)
    ;;
  push)
    REPO_CMD+=(--mode push)
    if [[ -n "$REV_RANGE" ]]; then
      REPO_CMD+=(--range "$REV_RANGE")
    fi
    if [[ -z "$BASE_REF" ]]; then
      BASE_REF="$(default_upstream_base)"
    fi
    if [[ -z "$BASE_REF" ]]; then
      echo "push mode requires --base <ref> or a configured upstream branch" >&2
      exit 2
    fi
    DOCS_GATE_CMD+=(--mode push --base "$BASE_REF")
    ;;
  *)
    echo "Unsupported mode: $MODE" >&2
    usage >&2
    exit 2
    ;;
esac

run_cmd "${REPO_CMD[@]}"
run_cmd "${DOCS_GATE_CMD[@]}"

if [[ "$RUN_AUDIT" -eq 1 ]]; then
  run_cmd "${AUDIT_CMD[@]}"
else
  log "audit skipped"
fi

if [[ "$RUN_HEALTH" -eq 1 ]]; then
  run_cmd "${HEALTH_CMD[@]}"
else
  log "checkpoint-health skipped"
fi

log "all checks passed"
