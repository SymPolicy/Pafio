#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import tomllib
from urllib.parse import urlsplit


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SPIO = ROOT / "scripts" / "spio"


def run_step(
    name: str,
    command: list[str],
    *,
    cwd: pathlib.Path,
    env: dict[str, str] | None = None,
) -> dict:
    try:
        proc = subprocess.run(
            command,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            env=env,
        )
        return {
            "name": name,
            "command": command,
            "returncode": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "ok": proc.returncode == 0,
        }
    except OSError as err:
        return {
            "name": name,
            "command": command,
            "returncode": 127,
            "stdout": "",
            "stderr": str(err),
            "ok": False,
        }


def load_json(stdout_text: str, context: str) -> dict:
    try:
        payload = json.loads(stdout_text)
    except json.JSONDecodeError as err:
        raise RuntimeError(f"{context} did not emit valid JSON") from err
    if not isinstance(payload, dict):
        raise RuntimeError(f"{context} must emit a top-level JSON object")
    return payload


def normalize_registry_root(value: str) -> str:
    parsed = urlsplit(value)
    if parsed.scheme:
        normalized = value.rstrip("/")
        if normalized:
            return normalized
        return value
    return str(pathlib.Path(value).resolve())


def normalize_header(value: str) -> str:
    name, separator, header_value = value.partition(":")
    if not separator:
        raise RuntimeError(f"invalid registry publish header in policy file: {value}")
    name = name.strip()
    header_value = header_value.strip()
    if not name or not header_value:
        raise RuntimeError(f"invalid registry publish header in policy file: {value}")
    return f"{name}: {header_value}"


def load_policy_headers(policy_file: pathlib.Path, publish_root: str) -> list[str]:
    with policy_file.open("rb") as handle:
        payload = tomllib.load(handle)
    if payload.get("schema-version") != 1:
        raise RuntimeError("registry publish policy file must declare schema-version = 1")
    entries = payload.get("registry")
    if not isinstance(entries, list):
        raise RuntimeError("registry publish policy file must contain [[registry]] entries")

    normalized_root = normalize_registry_root(publish_root)
    matches: list[list[str]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise RuntimeError("registry publish policy file contains a non-table registry entry")
        if normalize_registry_root(str(entry.get("root", ""))) != normalized_root:
            continue
        headers = entry.get("headers", [])
        if not isinstance(headers, list):
            raise RuntimeError("registry publish policy file field 'headers' must be an array")
        matches.append([normalize_header(str(header)) for header in headers])

    if not matches:
        raise RuntimeError(f"registry publish policy file does not contain a matching root for {normalized_root}")
    if len(matches) != 1:
        raise RuntimeError(f"registry publish policy file contains multiple matching roots for {normalized_root}")
    return matches[0]


def expected_publish_transport(registry_root: str) -> str:
    parsed = urlsplit(registry_root)
    if parsed.scheme in ("http", "https"):
        return "http"
    return "filesystem"


def write_publishable_package(root: pathlib.Path) -> pathlib.Path:
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "lib.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/server-gate\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n"
        "publish = true\n\n"
        "[toolchain]\n"
        "channel = \"nightly\"\n"
        "implicit-std = true\n\n"
        "[lib]\n"
        "path = \"src/lib.styio\"\n",
        encoding="utf-8",
    )
    source_path.write_text("# server-gate\n", encoding="utf-8")
    return manifest_path


def write_consumer_package(root: pathlib.Path, fetch_root: str) -> pathlib.Path:
    manifest_path = root / "spio.toml"
    source_path = root / "src" / "main.styio"
    source_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        "[spio]\n"
        "manifest-version = 1\n\n"
        "[package]\n"
        "name = \"acme/server-gate-client\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2026\"\n\n"
        "[toolchain]\n"
        "channel = \"nightly\"\n"
        "implicit-std = true\n\n"
        "[[bin]]\n"
        "name = \"client\"\n"
        "path = \"src/main.styio\"\n\n"
        "[dependencies]\n"
        f"server_gate = {{ package = \"acme/server-gate\", version = \"0.1.0\", registry = \"{fetch_root}\" }}\n",
        encoding="utf-8",
    )
    source_path.write_text(">_(\"client\")\n", encoding="utf-8")
    return manifest_path


def write_publish_profile(spio_home: pathlib.Path, profile_name: str, publish_root: str, headers: list[str]) -> pathlib.Path:
    profile_root = spio_home / "server" / "registry" / "publish-profiles"
    profile_root.mkdir(parents=True, exist_ok=True)
    profile_path = profile_root / f"{profile_name}.toml"
    header_lines = ", ".join(json.dumps(header) for header in headers)
    profile_path.write_text(
        "schema-version = 1\n\n"
        "[[registry]]\n"
        f"root = {json.dumps(publish_root)}\n"
        f"headers = [{header_lines}]\n",
        encoding="utf-8",
    )
    return profile_path

def validate_publish_payload(
    payload: dict,
    publish_root: str,
    expected_headers: list[str],
    expected_policy_file: str | None,
    expected_profile_name: str | None,
) -> list[str]:
    errors: list[str] = []

    if payload.get("command") != "publish":
        errors.append("publish payload field 'command' must equal 'publish'")
    if payload.get("mode") != "publish":
        errors.append("publish payload field 'mode' must equal 'publish'")
    if payload.get("registry_protocol") != "v2":
        errors.append("publish payload field 'registry_protocol' must equal 'v2'")

    transport = payload.get("transport")
    expected_transport = expected_publish_transport(publish_root)
    if transport != expected_transport:
        errors.append(
            f"publish payload field 'transport' must equal '{expected_transport}' for registry root {publish_root}"
        )

    if payload.get("package") != "acme/server-gate":
        errors.append("publish payload field 'package' must equal 'acme/server-gate'")

    policy_headers: list[str] = []
    if expected_policy_file:
        try:
            policy_headers = load_policy_headers(pathlib.Path(expected_policy_file), publish_root)
        except RuntimeError as err:
            errors.append(str(err))

    combined_headers = [*policy_headers, *expected_headers]
    if payload.get("registry_header_count") != len(combined_headers):
        errors.append("publish payload field 'registry_header_count' did not match the effective header count")
    expected_security_mode = "anonymous"
    if expected_profile_name:
        expected_security_mode = "profile"
    elif expected_policy_file:
        expected_security_mode = "policy-file"
    elif expected_headers:
        expected_security_mode = "explicit-headers"
    if payload.get("registry_write_security_mode") != expected_security_mode:
        errors.append(
            f"publish payload field 'registry_write_security_mode' must equal '{expected_security_mode}'"
        )
    provider_name = payload.get("registry_security_provider")
    if not isinstance(provider_name, str) or not provider_name:
        errors.append("publish payload field 'registry_security_provider' must be a non-empty string")
    if expected_profile_name:
        if payload.get("registry_profile") != expected_profile_name:
            errors.append("publish payload field 'registry_profile' did not match the requested profile name")
    elif payload.get("registry_profile") not in ("", None):
        errors.append("publish payload field 'registry_profile' must be absent when no profile was requested")

    if expected_transport == "http":
        control_plane_base = publish_root.rstrip("/")
        if not control_plane_base.endswith("/api/spio-registry-control/v1"):
            control_plane_base = control_plane_base + "/api/spio-registry-control/v1"
        if payload.get("control_plane_base_url") != control_plane_base:
            errors.append("publish payload field 'control_plane_base_url' did not match the expected control-plane base")
        if payload.get("publish_endpoint") != control_plane_base + "/publish":
            errors.append("publish payload field 'publish_endpoint' did not match the expected control-plane publish route")
        for key, suffix in (
            ("registry_index_path", "index/acme/server-gate.jsonl"),
            ("registry_artifact_path", ".spio.src.tar"),
            ("registry_log_leaf_path", "log/leaves/000000000001.json"),
        ):
            value = payload.get(key)
            if not isinstance(value, str) or not value:
                errors.append(f"publish payload field '{key}' must be a non-empty string")
            elif not value.endswith(suffix):
                errors.append(f"publish payload field '{key}' must end with '{suffix}'")
    else:
        for key, suffix in (
            ("registry_config_path", "config.json"),
            ("registry_index_path", os.path.join("index", "acme", "server-gate.jsonl")),
            ("registry_artifact_path", ".spio.src.tar"),
            ("registry_log_leaf_path", os.path.join("log", "leaves", "000000000001.json")),
        ):
            value = payload.get(key)
            if not isinstance(value, str) or not value:
                errors.append(f"publish payload field '{key}' must be a non-empty string")
            elif suffix in (".spio.src.tar",):
                if not value.endswith(suffix):
                    errors.append(f"publish payload field '{key}' must end with '{suffix}'")
            else:
                normalized = value.replace("\\", "/")
                if not normalized.endswith(suffix.replace("\\", "/")):
                    errors.append(f"publish payload field '{key}' must end with '{suffix}'")

    if payload.get("created_root") not in (True, False):
        errors.append("publish payload field 'created_root' must be a boolean")
    if payload.get("sequence") != 1:
        errors.append("publish payload field 'sequence' must equal 1 for the first publish in the gate")

    return errors


def validate_fetch_payload(payload: dict) -> list[str]:
    errors: list[str] = []
    if payload.get("command") != "fetch":
        errors.append("fetch payload field 'command' must equal 'fetch'")
    if payload.get("registry_packages") != 1:
        errors.append("fetch payload field 'registry_packages' must equal 1")
    if payload.get("packages") != 2:
        errors.append("fetch payload field 'packages' must equal 2")
    return errors


def resolve_roots(args: argparse.Namespace) -> tuple[str, str]:
    if not args.registry_root and not args.publish_root and not args.fetch_root:
        raise RuntimeError("one of --registry-root or --publish-root/--fetch-root must be provided")

    fallback_root = args.registry_root
    publish_root = args.publish_root or fallback_root
    fetch_root = args.fetch_root or fallback_root
    if not publish_root or not fetch_root:
        raise RuntimeError("both publish and fetch roots must be resolvable from the provided arguments")

    return normalize_registry_root(publish_root), normalize_registry_root(fetch_root)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="registry-server-gate.py")
    parser.add_argument("--registry-root", help="single registry root used for both publish and fetch validation")
    parser.add_argument("--publish-root", help="registry write root used for publish validation")
    parser.add_argument("--fetch-root", help="registry read root used for fetch validation")
    parser.add_argument(
        "--publish-header",
        action="append",
        default=[],
        help="repeatable registry header in Name: Value form forwarded only to remote publish requests",
    )
    parser.add_argument(
        "--publish-policy-file",
        help="registry publish policy file forwarded only to remote publish requests",
    )
    parser.add_argument(
        "--publish-profile",
        help="registry publish profile name written into the isolated SPIO_HOME and used only for remote publish requests",
    )
    parser.add_argument("--sync-timeout-seconds", type=float, default=0.0, help="time budget for publish-to-fetch sync")
    parser.add_argument("--fetch-trust-descriptor", help="registry trust descriptor imported before remote fetch validation")
    parser.add_argument("--spio-bin", default=str(DEFAULT_SPIO), help="spio wrapper used for publish/fetch checks")
    parser.add_argument("--json", action="store_true", help="emit machine-readable summary")
    args = parser.parse_args(argv)

    try:
        publish_root, fetch_root = resolve_roots(args)
    except RuntimeError as err:
        parser.error(str(err))
    if args.publish_policy_file and args.publish_profile:
        parser.error("--publish-policy-file cannot be combined with --publish-profile")

    spio_bin = str(pathlib.Path(args.spio_bin).resolve())
    publish_header_args = sum((["--registry-header", header] for header in args.publish_header), [])
    publish_policy_args = ["--registry-policy-file", os.path.abspath(args.publish_policy_file)] if args.publish_policy_file else []
    publish_profile_args: list[str] = []

    steps: list[dict] = []
    validation_errors: list[str] = []

    duplicate_rejected = False
    trust_import_succeeded = not args.fetch_trust_descriptor
    fetch_succeeded = False

    with tempfile.TemporaryDirectory(prefix="spio-registry-server-gate-") as temp_dir:
        temp_root = pathlib.Path(temp_dir)
        isolated_env = dict(os.environ)
        spio_home = temp_root / ".spio-home"
        isolated_env["SPIO_HOME"] = str(spio_home)
        if args.publish_profile:
            write_publish_profile(spio_home, args.publish_profile, publish_root, args.publish_header)
            publish_profile_args = ["--registry-profile", args.publish_profile]
            publish_header_args = []

        publish_manifest = write_publishable_package(temp_root / "publish")
        publish_step = run_step(
            "publish",
            [
                spio_bin,
                "--json",
                "publish",
                "--manifest-path",
                str(publish_manifest),
                "--registry",
                publish_root,
                *publish_profile_args,
                *publish_policy_args,
                *publish_header_args,
            ],
            cwd=temp_root,
            env=isolated_env,
        )
        steps.append(publish_step)

        if publish_step["ok"]:
            try:
                publish_payload = load_json(publish_step["stdout"], "spio publish --json")
                validation_errors.extend(
                    validate_publish_payload(
                        publish_payload,
                        publish_root,
                        args.publish_header,
                        args.publish_policy_file,
                        args.publish_profile,
                    )
                )
            except RuntimeError as err:
                validation_errors.append(str(err))

            duplicate_step = run_step(
                "republish_conflict",
                [
                    spio_bin,
                    "--json",
                    "publish",
                    "--manifest-path",
                    str(publish_manifest),
                    "--registry",
                    publish_root,
                    *publish_profile_args,
                    *publish_policy_args,
                    *publish_header_args,
                ],
                cwd=temp_root,
                env=isolated_env,
            )
            steps.append(duplicate_step)
            if duplicate_step["ok"]:
                validation_errors.append("duplicate publish unexpectedly succeeded")
            else:
                duplicate_rejected = True
                try:
                    duplicate_payload = load_json(duplicate_step["stderr"], "duplicate publish failure")
                    if duplicate_payload.get("code") != 17:
                        validation_errors.append("duplicate publish did not fail with publish exit code 17")
                except RuntimeError as err:
                    validation_errors.append(str(err))

            consumer_manifest = write_consumer_package(temp_root / "consume", fetch_root)
            if args.fetch_trust_descriptor:
                trust_step = run_step(
                    "trust_import",
                    [
                        spio_bin,
                        "--json",
                        "registry",
                        "trust",
                        "import",
                        args.fetch_trust_descriptor,
                    ],
                    cwd=temp_root,
                    env=isolated_env,
                )
                steps.append(trust_step)
                trust_import_succeeded = trust_step["ok"]
                if not trust_step["ok"]:
                    validation_errors.append("fetch trust descriptor import failed")

            fetch_deadline = time.monotonic() + max(args.sync_timeout_seconds, 0.0)

            while True:
                fetch_step = run_step(
                    "fetch",
                    [
                        spio_bin,
                        "--json",
                        "fetch",
                        "--manifest-path",
                        str(consumer_manifest),
                    ],
                    cwd=temp_root,
                    env=isolated_env,
                )
                if fetch_step["ok"] or time.monotonic() >= fetch_deadline:
                    steps.append(fetch_step)
                    fetch_succeeded = fetch_step["ok"]
                    break
                time.sleep(0.5)

            if steps[-1]["ok"]:
                try:
                    fetch_payload = load_json(steps[-1]["stdout"], "spio fetch --json")
                    validation_errors.extend(validate_fetch_payload(fetch_payload))
                except RuntimeError as err:
                    validation_errors.append(str(err))

    ok = publish_step["ok"] and duplicate_rejected and trust_import_succeeded and fetch_succeeded and not validation_errors

    summary = {
        "ok": ok,
        "publish_root": publish_root,
        "fetch_root": fetch_root,
        "spio_bin": spio_bin,
        "publish_headers": args.publish_header,
        "publish_policy_file": os.path.abspath(args.publish_policy_file) if args.publish_policy_file else "",
        "publish_profile": args.publish_profile or "",
        "fetch_trust_descriptor": args.fetch_trust_descriptor or "",
        "sync_timeout_seconds": args.sync_timeout_seconds,
        "validation_errors": validation_errors,
        "steps": steps,
    }

    if args.json:
        sys.stdout.write(json.dumps(summary, sort_keys=True) + "\n")
    else:
        for step in steps:
            status = "OK" if step["ok"] else "FAIL"
            sys.stdout.write(f"[{status}] {step['name']}\n")
            if step["stdout"].strip():
                sys.stdout.write(step["stdout"])
                if not step["stdout"].endswith("\n"):
                    sys.stdout.write("\n")
            if step["stderr"].strip():
                sys.stderr.write(step["stderr"])
                if not step["stderr"].endswith("\n"):
                    sys.stderr.write("\n")
        if validation_errors:
            for error in validation_errors:
                sys.stderr.write(f"[VALIDATION] {error}\n")
        sys.stdout.write(
            f"registry server gate {'passed' if ok else 'failed'} for publish={publish_root} fetch={fetch_root}\n"
        )

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
