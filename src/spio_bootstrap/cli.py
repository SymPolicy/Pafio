from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
import tomllib

from . import __version__
from .compat import CompatibilityError, CompilerProbeError, check_compiler_compatibility, resolve_styio_binary
from .project import InitOptions, infer_local_package_name, initialize_project
from .validation import ValidationError, validate_lock_document, validate_manifest_document


EXIT_SUCCESS = 0
EXIT_USAGE = 2
EXIT_MANIFEST = 10
EXIT_LOCK = 11
EXIT_WORKSPACE = 12
EXIT_RESOLVE = 13
EXIT_FETCH = 14
EXIT_CACHE = 15
EXIT_PLAN = 20
EXIT_CONTRACT = 21
EXIT_COMPILER_SPAWN = 22
EXIT_COMPILER = 23
EXIT_RUN = 24
EXIT_TEST = 25
EXIT_INTERNAL = 30
EXIT_NOT_IMPLEMENTED = 31


@dataclass(frozen=True)
class CommandError(Exception):
    category: str
    code: int
    message: str
    command: str


def _emit_error(err: CommandError, as_json: bool) -> int:
    payload = {
        "category": err.category,
        "code": err.code,
        "message": err.message,
        "command": err.command,
    }
    if as_json:
        sys.stderr.write(json.dumps(payload, sort_keys=True) + "\n")
    else:
        sys.stderr.write(f"[{err.category}:{err.code}] {err.command}: {err.message}\n")
    return err.code


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="spio")
    parser.add_argument("--json", action="store_true", help="emit machine-readable diagnostics")
    parser.add_argument("--version", action="store_true", help="show version and exit")
    subparsers = parser.add_subparsers(dest="command")

    machine = subparsers.add_parser("machine-info")
    machine.add_argument("--json", action="store_true", help="emit machine-readable info")

    new_cmd = subparsers.add_parser("new")
    new_cmd.add_argument("name")
    new_cmd.add_argument("directory", nargs="?")
    kind_group = new_cmd.add_mutually_exclusive_group()
    kind_group.add_argument("--lib", action="store_true")
    kind_group.add_argument("--bin", action="store_true")

    init_cmd = subparsers.add_parser("init")
    init_cmd.add_argument("--name")
    init_kind = init_cmd.add_mutually_exclusive_group()
    init_kind.add_argument("--lib", action="store_true")
    init_kind.add_argument("--bin", action="store_true")

    check_cmd = subparsers.add_parser("check")
    check_cmd.add_argument("--manifest-path", default="spio.toml")
    check_cmd.add_argument("--styio-bin")

    for command in ("add", "remove", "fetch", "lock", "build", "run", "test", "tree", "pack", "publish"):
        subparsers.add_parser(command)

    tool = subparsers.add_parser("tool")
    tool_sub = tool.add_subparsers(dest="tool_command")
    tool_sub.add_parser("install")

    return parser


def _machine_info_payload() -> dict:
    return {
        "tool": "spio",
        "version": __version__,
        "bootstrap": True,
        "supported_manifests": [1],
        "supported_lockfiles": [1],
        "supported_contracts": {
            "compile_plan": [1],
        },
        "notes": [
            "bootstrap implementation",
            "commands other than machine-info/version are stubs",
        ],
    }


def _emit_success(payload: dict, as_json: bool) -> int:
    if as_json:
        sys.stdout.write(json.dumps(payload, sort_keys=True) + "\n")
    else:
        message = payload.get("message")
        if message:
            sys.stdout.write(f"{message}\n")
    return EXIT_SUCCESS


def _bootstrap_not_implemented(command: str, as_json: bool) -> int:
    return _emit_error(
        CommandError(
            category="BootstrapNotImplemented",
            code=EXIT_NOT_IMPLEMENTED,
            message="command is recognized but not implemented in the bootstrap scaffold",
            command=command,
        ),
        as_json,
    )


def _kind_from_args(args: argparse.Namespace) -> str:
    if getattr(args, "lib", False):
        return "lib"
    return "bin"


def _handle_new(args: argparse.Namespace, as_json: bool) -> int:
    root = Path(args.directory or args.name.split("/", 1)[1])
    try:
        initialize_project(
            InitOptions(
                package_name=args.name,
                root=root,
                kind=_kind_from_args(args),
            )
        )
    except FileExistsError as err:
        return _emit_error(CommandError("UsageError", EXIT_USAGE, str(err), "new"), as_json)

    return _emit_success(
        {
            "command": "new",
            "message": f"initialized project at {root}",
            "root": str(root.resolve()),
        },
        as_json,
    )


def _handle_init(args: argparse.Namespace, as_json: bool) -> int:
    root = Path.cwd()
    package_name = args.name or infer_local_package_name(root)
    try:
        initialize_project(
            InitOptions(
                package_name=package_name,
                root=root,
                kind=_kind_from_args(args),
            )
        )
    except FileExistsError as err:
        return _emit_error(CommandError("UsageError", EXIT_USAGE, str(err), "init"), as_json)

    return _emit_success(
        {
            "command": "init",
            "message": f"initialized project in {root}",
            "root": str(root.resolve()),
            "package": package_name,
        },
        as_json,
    )


def _handle_check(args: argparse.Namespace, as_json: bool) -> int:
    manifest_path = Path(args.manifest_path)
    if not manifest_path.exists():
        return _emit_error(
            CommandError("ManifestError", EXIT_MANIFEST, f"manifest not found: {manifest_path}", "check"),
            as_json,
        )

    try:
        manifest = tomllib.loads(manifest_path.read_text())
        validate_manifest_document(manifest)
    except (tomllib.TOMLDecodeError, ValidationError) as err:
        return _emit_error(CommandError("ManifestError", EXIT_MANIFEST, str(err), "check"), as_json)

    lock_path = manifest_path.with_name("spio.lock")
    if lock_path.exists():
        try:
            lock_doc = tomllib.loads(lock_path.read_text())
            validate_lock_document(lock_doc)
        except (tomllib.TOMLDecodeError, ValidationError) as err:
            return _emit_error(CommandError("LockfileError", EXIT_LOCK, str(err), "check"), as_json)

    compatibility_payload = None
    styio_binary = resolve_styio_binary(args.styio_bin)
    if styio_binary is not None:
        try:
            compatibility_payload = check_compiler_compatibility(styio_binary).to_dict()
        except CompilerProbeError as err:
            return _emit_error(CommandError("CompilerSpawnError", EXIT_COMPILER_SPAWN, str(err), "check"), as_json)
        except CompatibilityError as err:
            return _emit_error(CommandError("ContractError", EXIT_CONTRACT, str(err), "check"), as_json)

    return _emit_success(
        {
            "command": "check",
            "message": f"manifest and lockfile look valid: {manifest_path}",
            "manifest_path": str(manifest_path.resolve()),
            "lockfile_present": lock_path.exists(),
            "compiler_checked": compatibility_payload is not None,
            "styio": compatibility_payload,
        },
        as_json,
    )


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if args.version:
        sys.stdout.write(f"spio {__version__}\n")
        return EXIT_SUCCESS

    if args.command == "machine-info":
        sys.stdout.write(json.dumps(_machine_info_payload(), sort_keys=True) + "\n")
        return EXIT_SUCCESS

    if args.command is None:
        parser.print_help()
        return EXIT_SUCCESS

    as_json = bool(getattr(args, "json", False))

    if args.command == "new":
        return _handle_new(args, as_json)
    if args.command == "init":
        return _handle_init(args, as_json)
    if args.command == "check":
        return _handle_check(args, as_json)
    if args.command == "tool":
        return _bootstrap_not_implemented("tool install", as_json)
    return _bootstrap_not_implemented(args.command, as_json)
