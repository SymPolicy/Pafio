#!/usr/bin/env python3
import argparse
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


def resolve_request_path(root: Path, request_path: str) -> Path:
    relative = Path(unquote(urlsplit(request_path).path).lstrip("/"))
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root)
    except ValueError as exc:
        raise PermissionError("request path escapes registry root") from exc
    return candidate


class ImmutableRegistryHandler(SimpleHTTPRequestHandler):
    registry_root: Path
    required_headers: list[tuple[str, str]]

    def translate_path(self, path: str) -> str:
        try:
            return str(resolve_request_path(self.registry_root, path))
        except PermissionError:
            return str(self.registry_root / "__forbidden__")

    def require_configured_headers(self) -> bool:
        for name, expected_value in self.required_headers:
            actual_value = self.headers.get(name)
            if actual_value != expected_value:
                self.send_error(403, f"missing or invalid required header: {name}")
                return False
        return True

    def do_GET(self) -> None:
        if not self.require_configured_headers():
            return
        super().do_GET()

    def do_HEAD(self) -> None:
        if not self.require_configured_headers():
            return
        super().do_HEAD()

    def do_PUT(self) -> None:
        if not self.require_configured_headers():
            return
        try:
            target = resolve_request_path(self.registry_root, self.path)
        except PermissionError:
            self.send_error(403, "path escapes registry root")
            return

        if target.exists():
            self.send_error(409, "immutable object already exists")
            return

        content_length = self.headers.get("Content-Length")
        if content_length is None:
            self.send_error(411, "missing Content-Length")
            return

        remaining = int(content_length)
        temp_path = target.with_suffix(target.suffix + ".tmp")
        target.parent.mkdir(parents=True, exist_ok=True)

        with open(temp_path, "wb") as out:
            while remaining > 0:
                chunk = self.rfile.read(min(remaining, 1024 * 1024))
                if not chunk:
                    temp_path.unlink(missing_ok=True)
                    self.send_error(400, "request body ended early")
                    return
                out.write(chunk)
                remaining -= len(chunk)

        os.replace(temp_path, target)
        self.send_response(201)
        self.end_headers()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument(
        "--require-header",
        action="append",
        default=[],
        help="repeatable required header in Name: Value form; applied to GET/HEAD/PUT",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    ImmutableRegistryHandler.registry_root = root
    ImmutableRegistryHandler.required_headers = []
    for header in args.require_header:
        name, separator, value = header.partition(":")
        name = name.strip()
        value = value.strip()
        if not separator or not name or not value:
            raise SystemExit(f"invalid --require-header value: {header}")
        ImmutableRegistryHandler.required_headers.append((name, value))

    server = ThreadingHTTPServer((args.bind, args.port), ImmutableRegistryHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()
