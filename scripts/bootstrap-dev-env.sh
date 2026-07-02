#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL_VENV="${PAFIO_TOOL_VENV:-$HOME/.local/venvs/pafio-tools}"
DEBIAN_STANDARD_VERSION="${STYIO_TOOLCHAIN_DEBIAN_STANDARD_VERSION:-13}"
LLVM_STANDARD_SERIES="${STYIO_TOOLCHAIN_LLVM_STANDARD_SERIES:-18.1.x}"
CMAKE_STANDARD_VERSION="${STYIO_TOOLCHAIN_CMAKE_STANDARD_VERSION:-3.31.6}"
PYTHON_STANDARD_VERSION="${STYIO_TOOLCHAIN_PYTHON_STANDARD_VERSION:-$(tr -d '[:space:]' < "$ROOT/.python-version")}"

usage() {
  cat <<EOF
Usage: $(basename "$0")

Install the Debian/Ubuntu packages required to build and test pafio on a fresh
Linux container or VM.

Optional environment:
  PAFIO_TOOL_VENV      Python virtualenv used for standardized cmake/ctest
                            Default: $TOOL_VENV

Standardized baseline shared with styio-nightly:
  Debian                  $DEBIAN_STANDARD_VERSION (trixie)
  LLVM / Clang / LLD      $LLVM_STANDARD_SERIES via clang-18 toolchain packages
  CMake / CTest           $CMAKE_STANDARD_VERSION (installed into the tool venv)
  Python                  $PYTHON_STANDARD_VERSION
EOF
}

log() {
  printf '[pafio env] %s\n' "$*"
}

fail() {
  printf '[pafio env] %s\n' "$*" >&2
  exit 1
}

as_root() {
  if [[ $EUID -eq 0 ]]; then
    "$@"
    return
  fi

  if command -v sudo >/dev/null 2>&1; then
    sudo "$@"
    return
  fi

  fail "sudo is required to install system packages"
}

ensure_debian_like() {
  if [[ ! -r /etc/os-release ]]; then
    fail "/etc/os-release is missing; only Debian/Ubuntu hosts are supported"
  fi

  # shellcheck disable=SC1091
  . /etc/os-release

  local family="${ID_LIKE:-}"
  if [[ "${ID:-}" != "debian" && "${ID:-}" != "ubuntu" && "${family}" != *debian* && "${family}" != *ubuntu* ]]; then
    fail "unsupported distribution: ${PRETTY_NAME:-unknown}. Expected Debian/Ubuntu."
  fi
}

report_standard_baseline() {
  # shellcheck disable=SC1091
  . /etc/os-release
  if [[ "${ID:-}" == "debian" && "${VERSION_ID:-}" == "$DEBIAN_STANDARD_VERSION" ]]; then
    log "host matches the standardized dev baseline: Debian $DEBIAN_STANDARD_VERSION"
    return
  fi

  log "host is ${PRETTY_NAME:-unknown}; standardized dev baseline is Debian $DEBIAN_STANDARD_VERSION (trixie). Continuing with the compatible Debian/Ubuntu bootstrap path."
}

install_system_packages() {
  local packages=(
    build-essential
    ca-certificates
    clang-18
    cmake
    curl
    git
    lld-18
    llvm-18-dev
    llvm-18-tools
    ninja-build
    pkg-config
    python3
    python3-pip
    python3-venv
  )

  log "installing system packages"
  as_root apt-get update
  as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"
}

install_python_tooling() {
  log "installing standardized CMake/CTest into $TOOL_VENV"
  python3 -m venv "$TOOL_VENV"
  "$TOOL_VENV/bin/python" -m pip install --upgrade pip
  "$TOOL_VENV/bin/python" -m pip install "cmake==$CMAKE_STANDARD_VERSION"
}

print_summary() {
  cat <<EOF

pafio bootstrap complete.

Standardized baseline:
  Debian:        $DEBIAN_STANDARD_VERSION (trixie)
  LLVM series:   $LLVM_STANDARD_SERIES
  CMake/CTest:   $CMAKE_STANDARD_VERSION
  Python:        $PYTHON_STANDARD_VERSION

Suggested shell exports:
  export CC=/usr/bin/clang-18
  export CXX=/usr/bin/clang++-18
  export LLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
  export PATH="$TOOL_VENV/bin:\$PATH"

Typical next steps:
  cmake -S "$ROOT" -B "$ROOT/build"
  cmake --build "$ROOT/build" -j"$(nproc)"
  ctest --test-dir "$ROOT/build"
EOF
}

main() {
  if [[ "${1:-}" == "--help" ]]; then
    usage
    exit 0
  fi

  ensure_debian_like
  report_standard_baseline
  install_system_packages
  install_python_tooling
  print_summary
}

main "$@"
