#!/usr/bin/env sh
set -eu

usage() {
  cat <<'USAGE'
Usage: install-spio.sh [options]

Install a prebuilt spio binary from an HTTP(S) directory. This script is safe
for a curl pipeline:

  curl -fsSL https://example.invalid/spio/install-spio.sh | sh -s -- --base-url https://example.invalid/spio

Options:
  --base-url <url>      Directory containing the spio binary.
  --binary-url <url>    Exact URL for the spio binary.
  --install-dir <dir>   Install directory (default: /usr/local/bin).
  --binary-name <name>  Installed executable name (default: spio).
  --no-styio-shim       Do not install the companion styio shim.
  -h, --help            Show this help.
USAGE
}

fail() {
  echo "install-spio: $*" >&2
  exit 1
}

BASE_URL="${SPIO_INSTALL_BASE_URL:-}"
BINARY_URL="${SPIO_INSTALL_BINARY_URL:-}"
INSTALL_DIR="${SPIO_INSTALL_DIR:-/usr/local/bin}"
BINARY_NAME="${SPIO_INSTALL_BINARY_NAME:-spio}"
INSTALL_STYIO_SHIM=1

while [ "$#" -gt 0 ]; do
  case "$1" in
    --base-url)
      [ "$#" -ge 2 ] || fail "--base-url requires a value"
      BASE_URL="$2"
      shift 2
      ;;
    --binary-url)
      [ "$#" -ge 2 ] || fail "--binary-url requires a value"
      BINARY_URL="$2"
      shift 2
      ;;
    --install-dir)
      [ "$#" -ge 2 ] || fail "--install-dir requires a value"
      INSTALL_DIR="$2"
      shift 2
      ;;
    --binary-name)
      [ "$#" -ge 2 ] || fail "--binary-name requires a value"
      BINARY_NAME="$2"
      shift 2
      ;;
    --no-styio-shim)
      INSTALL_STYIO_SHIM=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown option: $1"
      ;;
  esac
done

command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v install >/dev/null 2>&1 || fail "install is required"

if [ -z "$BINARY_URL" ]; then
  [ -n "$BASE_URL" ] || fail "pass --base-url or --binary-url"
  BASE_URL="${BASE_URL%/}"
  BINARY_URL="$BASE_URL/spio"
fi

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

TMP_BIN="$TMP_DIR/spio"
TMP_STYIO_SHIM="$TMP_DIR/styio"
curl -fsSL "$BINARY_URL" -o "$TMP_BIN"
chmod 0755 "$TMP_BIN"
cat >"$TMP_STYIO_SHIM" <<'EOF'
#!/usr/bin/env sh
set -eu
SPIO_HOME_DIR="${SPIO_HOME:-$HOME/.spio}"
STYIO_BIN="$SPIO_HOME_DIR/tools/styio/current/bin/styio"
if [ ! -x "$STYIO_BIN" ]; then
  echo "styio is not installed; run: spio install styio@latest" >&2
  exit 127
fi
exec "$STYIO_BIN" "$@"
EOF
chmod 0755 "$TMP_STYIO_SHIM"

if [ -w "$INSTALL_DIR" ]; then
  mkdir -p "$INSTALL_DIR"
  install -m 0755 "$TMP_BIN" "$INSTALL_DIR/$BINARY_NAME"
  if [ "$INSTALL_STYIO_SHIM" -eq 1 ]; then
    install -m 0755 "$TMP_STYIO_SHIM" "$INSTALL_DIR/styio"
  fi
elif command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
  sudo install -d -m 0755 "$INSTALL_DIR"
  sudo install -m 0755 "$TMP_BIN" "$INSTALL_DIR/$BINARY_NAME"
  if [ "$INSTALL_STYIO_SHIM" -eq 1 ]; then
    sudo install -m 0755 "$TMP_STYIO_SHIM" "$INSTALL_DIR/styio"
  fi
else
  fail "$INSTALL_DIR is not writable and passwordless sudo is unavailable; pass --install-dir \$HOME/.local/bin"
fi

if command -v "$BINARY_NAME" >/dev/null 2>&1; then
  "$BINARY_NAME" --version
else
  echo "installed $INSTALL_DIR/$BINARY_NAME"
  echo "add $INSTALL_DIR to PATH before running $BINARY_NAME"
fi
