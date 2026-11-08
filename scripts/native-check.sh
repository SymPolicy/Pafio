#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PAFIO_BUILD_DIR:-$ROOT/build-codex}"

run_native_command() {
  local name="$1" rc=0
  shift
  if [[ -z "${PAFIO_GATE_LOG_DIR:-}" ]]; then
    "$@"
    return
  fi
  printf '%q ' "$@" > "$NATIVE_LOG_DIR/$name.command"
  printf '\n' >> "$NATIVE_LOG_DIR/$name.command"
  "$@" > "$NATIVE_LOG_DIR/$name.stdout.log" 2> "$NATIVE_LOG_DIR/$name.stderr.log" || rc=$?
  printf '%s\n' "$rc" > "$NATIVE_LOG_DIR/$name.exit"
  return "$rc"
}

if [[ -n "${PAFIO_GATE_LOG_DIR:-}" ]]; then
  mkdir -p "$PAFIO_GATE_LOG_DIR"
  NATIVE_LOG_DIR="$(mktemp -d "$PAFIO_GATE_LOG_DIR/native-XXXXXXXX")"
  retain_native_records() {
    local rc=$? copy_rc=0
    trap - EXIT
    # Preserve the command failure even when retaining its records also fails.
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
      cp "$BUILD_DIR/CMakeCache.txt" "$NATIVE_LOG_DIR/" || copy_rc=$?
    fi
    if [[ -d "$BUILD_DIR/Testing" && -f "$NATIVE_LOG_DIR/ctest.command" ]]; then
      cp -R "$BUILD_DIR/Testing" "$NATIVE_LOG_DIR/" || copy_rc=$?
    fi
    printf '%s\n' "$copy_rc" > "$NATIVE_LOG_DIR/retention.exit"
    if [[ "$rc" -eq 0 ]]; then rc="$copy_rc"; fi
    printf '%s\n' "$rc" > "$NATIVE_LOG_DIR/check.exit"
    exit "$rc"
  }
  trap retain_native_records EXIT
fi

run_native_command configure cmake -S "$ROOT" -B "$BUILD_DIR"
run_native_command build cmake --build "$BUILD_DIR" --parallel 4
if [[ -n "${PAFIO_GATE_LOG_DIR:-}" ]]; then
  run_native_command ctest ctest --test-dir "$BUILD_DIR" --output-on-failure \
    --verbose --output-junit "$NATIVE_LOG_DIR/ctest.xml"
else
  run_native_command ctest ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
