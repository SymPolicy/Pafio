#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/delivery-gate.sh [options]

Run the common Styio delivery floor by composing repository hygiene, the docs
gate, external audit, and checkpoint health into one entrypoint.

Options:
  --mode <auto|checkpoint|staged|push>
                            Delivery mode (default: auto)
  --base <ref>              Base ref for team-docs-gate branch checks
  --range <rev-range>       Explicit revision range for repo-hygiene push mode
  --skip-health             Skip checkpoint-health (docs/process-only deliveries)
  --skip-audit              Skip external General-Auditor gate
  --audit-root <path>        Trusted General-Auditor checkout
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

current_branch() {
  git branch --show-current 2>/dev/null || true
}

ref_exists() {
  git rev-parse --verify --quiet "$1^{commit}" >/dev/null
}

has_worktree_changes() {
  [[ -n "$(git status --porcelain=v1 --untracked-files=all)" ]]
}

rev_count() {
  git rev-list --count "$1" 2>/dev/null || echo 0
}

origin_repo_name() {
  local url path
  url="$(git remote get-url origin 2>/dev/null || true)"
  [[ -n "$url" ]] || return 1
  case "$url" in
    git@github.com:*)
      path="${url#git@github.com:}"
      ;;
    https://github.com/*)
      path="${url#https://github.com/}"
      ;;
    http://github.com/*)
      path="${url#http://github.com/}"
      ;;
    *)
      return 1
      ;;
  esac
  path="${path%.git}"
  [[ "$path" == */* ]] || return 1
  echo "$path"
}

github_parent_repo() {
  local repo parent
  command -v gh >/dev/null 2>&1 || return 1
  repo="$(origin_repo_name)" || return 1
  parent="$(
    gh repo view "$repo" --json parent --jq '.parent | if . == null then "" else .owner.login + "/" + .name end' 2>/dev/null || true
  )"
  [[ -n "$parent" ]] || return 1
  echo "$parent"
}

fetch_github_parent_base() {
  local branch parent ref
  branch="$1"
  [[ -n "$branch" ]] || return 1
  parent="$(github_parent_repo)" || return 1
  ref="refs/delivery-base/${parent/\//-}/${branch}"
  if git fetch --no-tags "https://github.com/${parent}.git" "+refs/heads/${branch}:${ref}" >/dev/null 2>&1; then
    echo "$ref"
    return 0
  fi
  return 1
}

resolve_delivery_base() {
  local branch configured parent_ref upstream_ref upstream_branch
  configured="${BASE_REF:-${STYIO_DELIVERY_BASE:-${STYIO_TEAM_DOC_GATE_BASE:-}}}"
  if [[ -n "$configured" ]]; then
    echo "$configured"
    return 0
  fi

  branch="$(current_branch)"
  if [[ -n "$branch" ]]; then
    upstream_ref="refs/remotes/upstream/${branch}"
    if ref_exists "$upstream_ref"; then
      echo "$upstream_ref"
      return 0
    fi
    parent_ref="$(fetch_github_parent_base "$branch" || true)"
    if [[ -n "$parent_ref" ]]; then
      echo "$parent_ref"
      return 0
    fi
  fi

  upstream_branch="$(default_upstream_base)"
  if [[ -n "$upstream_branch" ]]; then
    echo "$upstream_branch"
    return 0
  fi
  return 1
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE="auto"
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
    --audit-root)
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
  AUDIT_CMD+=(--audit-root "$AUDIT_BIN")
fi
if [[ -n "$BUILD_DIR" ]]; then
  HEALTH_CMD+=(--build-dir "$BUILD_DIR")
fi
if [[ -n "$STYIO_BIN" ]]; then
  HEALTH_CMD+=(--styio-bin "$STYIO_BIN")
fi

case "$MODE" in
  auto)
    if has_worktree_changes; then
      run_cmd python3 scripts/repo-hygiene-gate.py --mode worktree
      run_cmd ./scripts/docs-gate.sh --mode worktree
    else
      log "worktree: nothing to check"
    fi

    if [[ -z "$BASE_REF" ]]; then
      BASE_REF="$(resolve_delivery_base || true)"
    fi
    if [[ -z "$BASE_REF" ]]; then
      echo "auto mode could not infer a delivery base; set --base <ref> or STYIO_DELIVERY_BASE" >&2
      exit 2
    fi
    if [[ "$(rev_count "${BASE_REF}..HEAD")" -gt 0 ]]; then
      if [[ -z "$REV_RANGE" ]]; then
        REV_RANGE="${BASE_REF}..HEAD"
      fi
      run_cmd python3 scripts/repo-hygiene-gate.py --mode push --range "$REV_RANGE"
      run_cmd ./scripts/docs-gate.sh --mode push --base "$BASE_REF"
    else
      log "push range ${BASE_REF}..HEAD: nothing to check"
    fi
    ;;
  checkpoint)
    REPO_CMD+=(--mode worktree)
    DOCS_GATE_CMD+=(--mode worktree)
    run_cmd "${REPO_CMD[@]}"
    run_cmd "${DOCS_GATE_CMD[@]}"
    ;;
  staged)
    REPO_CMD+=(--mode staged)
    DOCS_GATE_CMD+=(--mode staged)
    run_cmd "${REPO_CMD[@]}"
    run_cmd "${DOCS_GATE_CMD[@]}"
    ;;
  push)
    REPO_CMD+=(--mode push)
    if [[ -z "$BASE_REF" ]]; then
      BASE_REF="$(resolve_delivery_base || true)"
    fi
    if [[ -z "$BASE_REF" ]]; then
      echo "push mode requires --base <ref> or a configured upstream branch" >&2
      exit 2
    fi
    if [[ -z "$REV_RANGE" ]]; then
      REV_RANGE="${BASE_REF}..HEAD"
    fi
    REPO_CMD+=(--range "$REV_RANGE")
    DOCS_GATE_CMD+=(--mode push --base "$BASE_REF")
    run_cmd "${REPO_CMD[@]}"
    run_cmd "${DOCS_GATE_CMD[@]}"
    ;;
  *)
    echo "Unsupported mode: $MODE" >&2
    usage >&2
    exit 2
    ;;
esac

if [[ "$RUN_AUDIT" -eq 1 ]]; then
  run_cmd "${AUDIT_CMD[@]}"
else
  log "General-Auditor skipped"
fi

if [[ "$RUN_HEALTH" -eq 1 ]]; then
  run_cmd "${HEALTH_CMD[@]}"
else
  log "checkpoint-health skipped"
fi

log "all checks passed"
