#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PAFIO_BUILD_DIR:-$ROOT/build-codex}"

cmake -S "$ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --parallel 4
ctest --test-dir "$BUILD_DIR" --output-on-failure
