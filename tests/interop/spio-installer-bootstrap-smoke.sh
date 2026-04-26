#!/usr/bin/env bash
set -euo pipefail

die() {
  echo "spio-installer-bootstrap-smoke: $*" >&2
  exit 1
}

if [ "$#" -ne 1 ]; then
  die "usage: $0 <spio-binary>"
fi

spio_binary=$1
if [ ! -x "$spio_binary" ]; then
  die "spio binary is not executable: $spio_binary"
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/spio-installer-bootstrap.XXXXXX")
server_pid=""

cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$work_dir"
}
trap cleanup EXIT

remote_dir="$work_dir/remote"
install_dir="$work_dir/bin"
source_repo="$work_dir/styio-source"
spio_home="$work_dir/spio-home"
mkdir -p "$remote_dir" "$install_dir" "$source_repo" "$spio_home"

cp "$spio_binary" "$remote_dir/spio"
cp "$repo_root/scripts/install-spio.sh" "$remote_dir/install-spio.sh"
chmod +x "$remote_dir/spio" "$remote_dir/install-spio.sh"

cat >"$source_repo/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(fake_styio LANGUAGES NONE)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
configure_file("${CMAKE_SOURCE_DIR}/styio.sh.in" "${CMAKE_BINARY_DIR}/bin/styio" @ONLY NEWLINE_STYLE UNIX)
file(CHMOD "${CMAKE_BINARY_DIR}/bin/styio" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
add_custom_target(styio ALL DEPENDS "${CMAKE_BINARY_DIR}/bin/styio")
CMAKE

cat >"$source_repo/styio.sh.in" <<'SH'
#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "--machine-info=json" ]; then
  printf '%s\n' '{"tool":"styio","compiler_version":"0.0.5","channel":"stable","supported_contracts":{"compile_plan":[1]},"capabilities":["machine_info_json","single_file_entry","jsonl_diagnostics"],"edition_max":"2026"}'
  exit 0
fi

if [ "${1:-}" = "--file" ] && [ -n "${2:-}" ]; then
  grep -q 'hello, styio!' "$2" || {
    echo "unexpected styio input" >&2
    exit 65
  }
  printf '%s\n' 'hello, styio!'
  exit 0
fi

echo "unexpected styio invocation: $*" >&2
exit 64
SH

git -C "$source_repo" init -q
git -C "$source_repo" checkout -q -b main
git -C "$source_repo" add CMakeLists.txt styio.sh.in
git -C "$source_repo" -c user.name=spio-test -c user.email=spio-test@example.invalid commit -qm "seed fake styio source"

port_file="$work_dir/http-port"
python3 - "$remote_dir" "$port_file" <<'PY' &
import functools
import http.server
import socketserver
import sys

remote_dir = sys.argv[1]
port_file = sys.argv[2]

class Server(socketserver.TCPServer):
    allow_reuse_address = True

handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=remote_dir)
with Server(("127.0.0.1", 0), handler) as httpd:
    with open(port_file, "w", encoding="utf-8") as fh:
        fh.write(str(httpd.server_address[1]))
        fh.write("\n")
    httpd.serve_forever()
PY
server_pid=$!

for _ in $(seq 1 100); do
  [ -s "$port_file" ] && break
  sleep 0.05
done
[ -s "$port_file" ] || die "local installer HTTP server did not start"

base_url="http://127.0.0.1:$(cat "$port_file")"
curl -fsSL "$base_url/install-spio.sh" | sh -s -- --base-url "$base_url" --install-dir "$install_dir"

export PATH="$install_dir:$PATH"
export SPIO_HOME="$spio_home"
export SPIO_STYIO_SOURCE_ORIGIN="file://$source_repo"
export SPIO_STYIO_SOURCE_REF="main"

[ "$(command -v spio)" = "$install_dir/spio" ] || die "installed spio was not first in PATH"
[ "$(command -v styio)" = "$install_dir/styio" ] || die "installed styio shim was not first in PATH"

spio install styio@latest >/dev/null
[ -x "$SPIO_HOME/tools/styio/current/bin/styio" ] || die "managed styio binary was not installed"

hello_file="$work_dir/hello.styio"
printf '%s\n' '>_("hello, styio!")' >"$hello_file"
hello_output=$(styio --file "$hello_file")
[ "$hello_output" = "hello, styio!" ] || die "unexpected hello output: $hello_output"

echo "spio installer bootstrap smoke passed"
