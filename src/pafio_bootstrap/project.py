from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


DEFAULT_EDITION = "2026"


@dataclass(frozen=True)
class InitOptions:
    package_name: str
    root: Path
    kind: str


def package_short_name(package_name: str) -> str:
    return package_name.split("/", 1)[1]


def infer_local_package_name(root: Path) -> str:
    return f"local/{root.resolve().name}"


def render_manifest(package_name: str, kind: str) -> str:
    lines = [
        "[pafio]",
        "manifest-version = 1",
        "",
        "[package]",
        f'name = "{package_name}"',
        'version = "0.1.0"',
        f'edition = "{DEFAULT_EDITION}"',
        "publish = false",
        "",
        "[toolchain]",
        'channel = "nightly"',
        "implicit-std = true",
        "",
    ]

    if kind == "lib":
        lines.extend([
            "[lib]",
            'path = "src/lib.styio"',
            "",
        ])
    else:
        lines.extend([
            "[[bin]]",
            f'name = "{package_short_name(package_name)}"',
            'path = "src/main.styio"',
            "",
        ])

    return "\n".join(lines)


def render_lib_source() -> str:
    return "# add := (a: i32, b: i32) => a + b\n"


def render_main_source() -> str:
    return '>_("hello from pafio bootstrap")\n'


def initialize_project(options: InitOptions) -> None:
    options.root.mkdir(parents=True, exist_ok=True)

    manifest_path = options.root / "pafio.toml"
    src_dir = options.root / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    if manifest_path.exists():
        raise FileExistsError(f"manifest already exists: {manifest_path}")

    manifest_path.write_text(render_manifest(options.package_name, options.kind))

    if options.kind == "lib":
        source_path = src_dir / "lib.styio"
        source_path.write_text(render_lib_source())
    else:
        source_path = src_dir / "main.styio"
        source_path.write_text(render_main_source())
