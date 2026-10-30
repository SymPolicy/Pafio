#!/usr/bin/env python3
"""Fail-closed repository policy check.

Covers the policy domains the retired centralized audit enforced that the
other repository-local gates do not already own:

- CI authority: every third-party action is pinned to a reviewed full commit
  SHA; checkouts that do not need credentials set ``persist-credentials:
  false``; no ``pull_request_target`` trigger runs repository code.
- Authorization defaults: every workflow declares an explicit top-level
  ``permissions:`` block, and no permissions block at any level grants
  write or admin scopes.
- Transport: operational surfaces (workflows, scripts, CMake inputs) fetch
  over HTTPS or SSH only — no plaintext ``http://`` endpoints outside the
  configured exact-host allowlist.
- Container runtime: tracked container recipes must pin base images by
  digest; absence of container recipes passes.
- Release-artifact controls: the artifact policy manifest exists, parses to
  an object, and declares the required integer ``version``; the
  binary-artifact checker is present.

Tunable policy values live in ``scripts/policy-check.json`` so policy review
stays configuration-as-data; this file holds only enforcement mechanics.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OPERATIONAL_SUFFIXES = {".yml", ".yaml", ".sh", ".py", ".cmake"}
OPERATIONAL_NAMES = {"CMakeLists.txt"}
USES_RE = re.compile(r"^\s*-?\s*uses:\s*([^\s#]+)")
FLOW_STEP_RE = re.compile(r"^\s*-?\s*\{(.*)\}\s*$")
SHA_PIN_RE = re.compile(r"^[A-Za-z0-9_./-]+@[0-9a-f]{40}$")
# Immutable ceilings: scripts/policy-check.json may only ever tighten these
# sets — a value outside the ceiling is a policy violation, never an
# expansion of what the gate accepts.
CEILING_PERMISSION_SCOPES = {"read", "none"}
CEILING_PLAINTEXT_HOSTS = {
    "localhost", "127.0.0.1", "0.0.0.0", "[::1]", "github.com",
    "www.apache.org", "example.com", "example.org", "example.net",
}
CEILING_PLAINTEXT_SUFFIXES = {".invalid", ".test", ".example", ".local"}
CEILING_PLAINTEXT_TEMPLATES = {"{host}"}
PLAINTEXT_RE = re.compile(r"http://(\{[^{}\s]*\}|[A-Za-z0-9][A-Za-z0-9._~-]*)(?::\d+)?(?:[/?#]|$)")
CONTAINER_RE = re.compile(r"^(Dockerfile.*|.*\.dockerfile|docker-compose.*\.(yml|yaml))$")
FROM_RE = re.compile(r"^FROM\s+(\S+)", re.M)
DIGEST_PIN_RE = re.compile(r"@sha256:[0-9a-f]{64}$")


def load_policy(root: pathlib.Path) -> tuple[dict, list[str]]:
    path = root / "scripts" / "policy-check.json"
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError:
        return {}, ["scripts/policy-check.json is missing — policy values are not reviewable"]
    except json.JSONDecodeError as exc:
        return {}, [f"scripts/policy-check.json does not parse: {exc}"]
    if not isinstance(payload, dict):
        return {}, ["scripts/policy-check.json root must be an object"]
    errors: list[str] = []
    if not isinstance(payload.get("allowed_plaintext_hosts"), list) or not all(
        isinstance(host, str) for host in payload["allowed_plaintext_hosts"]
    ):
        errors.append("scripts/policy-check.json: allowed_plaintext_hosts must be a list of host strings")
    if not isinstance(payload.get("allowed_plaintext_suffixes"), list) or not all(
        isinstance(suffix, str) for suffix in payload["allowed_plaintext_suffixes"]
    ):
        errors.append("scripts/policy-check.json: allowed_plaintext_suffixes must be a list of strings")
    if type(payload.get("artifact_policy_version")) is not int:
        errors.append("scripts/policy-check.json: artifact_policy_version must be an integer")
    if not isinstance(payload.get("allowed_permission_scopes"), list) or not all(
        isinstance(scope, str) for scope in payload["allowed_permission_scopes"]
    ):
        errors.append("scripts/policy-check.json: allowed_permission_scopes must be a list of strings")
    if not isinstance(payload.get("allowed_plaintext_templates"), list) or not all(
        isinstance(template, str) for template in payload["allowed_plaintext_templates"]
    ):
        errors.append("scripts/policy-check.json: allowed_plaintext_templates must be a list of strings")
    ceilings = (
        ("allowed_plaintext_hosts", CEILING_PLAINTEXT_HOSTS),
        ("allowed_plaintext_suffixes", CEILING_PLAINTEXT_SUFFIXES),
        ("allowed_plaintext_templates", CEILING_PLAINTEXT_TEMPLATES),
        ("allowed_permission_scopes", CEILING_PERMISSION_SCOPES),
    )
    for name, ceiling in ceilings:
        values = payload.get(name)
        if not isinstance(values, list):
            continue
        for value in values:
            if isinstance(value, str) and value not in ceiling:
                errors.append(
                    f"scripts/policy-check.json: {name} entry {value!r} exceeds the immutable policy ceiling"
                )
    return payload, errors


def strip_yaml_comment(text: str) -> str:
    """Remove a ``#`` comment; a hash inside quotes or glued to a token stays."""
    quote = ""
    for index, char in enumerate(text):
        if quote:
            if char == quote:
                quote = ""
        elif char in "'\"":
            quote = char
        elif char == "#" and (index == 0 or text[index - 1] in " \t"):
            return text[:index]
    return text


def unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
        return value[1:-1]
    return value


def split_flow(inner: str) -> list[str]:
    """Split a flow ``[a, b]``/``{k: v}`` body on top-level commas."""
    items, depth, quote, start = [], 0, "", 0
    for index, char in enumerate(inner):
        if quote:
            if char == quote:
                quote = ""
        elif char in "'\"":
            quote = char
        elif char in "[{":
            depth += 1
        elif char in "]}":
            depth -= 1
        elif char == "," and depth == 0:
            items.append(inner[start:index])
            start = index + 1
    items.append(inner[start:])
    return [item.strip() for item in items if item.strip()]


def flow_pairs(body: str) -> list[tuple[str, str]]:
    """Parse ``{k: v, ...}`` into pairs; fail closed on malformed items."""
    pairs = []
    for item in split_flow(body):
        key, sep, value = item.partition(":")
        if not sep:
            raise ValueError(f"flow mapping item has no colon: {item!r}")
        pairs.append((unquote(key), unquote(value)))
    return pairs


def key_lines(lines: list[str], key: str) -> list[tuple[int, str, int]]:
    """``(indent, inline-value, line)`` for every ``key:`` at any depth."""
    found = []
    for index, raw in enumerate(lines):
        match = re.match(r"^(\s*)(?:-\s*)?([A-Za-z_\"'][^:#]*?)\s*:\s*(.*)$", raw)
        if not match or unquote(match.group(2)) != key:
            continue
        found.append((len(match.group(1)), strip_yaml_comment(match.group(3)).strip(), index + 1))
    return found


def block_values(lines: list[str], start: int, indent: int) -> tuple[list[str], str | None]:
    """Map values directly under an empty ``key:`` at ``indent``.

    Nested blocks contribute their top-level map values only; returns
    ``(values, error)`` — the error is set when the block's shape is not a
    mapping this checker can prove correct about.
    """
    values: list[str] = []
    first_indent: int | None = None
    for cursor in range(start, len(lines)):
        raw = lines[cursor]
        if not raw.strip() or raw.strip().startswith("#"):
            continue
        line_indent = len(raw) - len(raw.lstrip())
        if line_indent <= indent:
            break
        if first_indent is None:
            if raw.lstrip().startswith("- "):
                return values, f"line {cursor + 1}: sequence under a key that expects a mapping"
            first_indent = line_indent
        if line_indent != first_indent:
            continue
        body = strip_yaml_comment(raw.strip())
        if body.lstrip().startswith("- "):
            return values, f"line {cursor + 1}: sequence under a key that expects a mapping"
        _, sep, value = body.partition(":")
        if not sep:
            return values, f"line {cursor + 1}: unparseable mapping entry {body!r}"
        value = value.strip()
        if value.startswith(("{", "[")):
            try:
                values.extend(v for _, v in flow_pairs(value.strip("{}[] ")) if value.startswith("{"))
            except ValueError as exc:
                return values, f"line {cursor + 1}: {exc}"
        elif value:
            values.append(unquote(value))
    return values, None


def trigger_names(lines: list[str], start: int, indent: int, inline: str) -> tuple[list[str], str | None]:
    """Trigger names declared by an ``on:`` key."""
    if inline:
        if inline.startswith("["):
            if not inline.endswith("]"):
                return [], "on: flow sequence is not closed"
            return [unquote(item) for item in split_flow(inline[1:-1])], None
        if inline.startswith("{"):
            try:
                return [key for key, _ in flow_pairs(inline[1:-1])], None
            except ValueError as exc:
                return [], f"on: {exc}"
        if inline.startswith(("|", ">", "&", "*", "!")):
            return [], f"on: unsupported YAML form {inline!r}"
        return [unquote(inline)], None
    names: list[str] = []
    first_indent: int | None = None
    for cursor in range(start, len(lines)):
        raw = lines[cursor]
        if not raw.strip() or raw.strip().startswith("#"):
            continue
        line_indent = len(raw) - len(raw.lstrip())
        if line_indent <= indent:
            break
        if first_indent is None:
            first_indent = line_indent
        if line_indent != first_indent:
            continue
        body = strip_yaml_comment(raw.strip()).lstrip("- ").strip()
        name, sep, _ = body.partition(":")
        names.append(unquote(name if sep else body))
    return names, None


def workflow_triggers(text: str) -> tuple[list[str], list[str]]:
    """``(names, errors)`` for the workflow's ``on`` declaration."""
    lines = text.splitlines()
    top = [entry for entry in key_lines(lines, "on") if entry[0] == 0]
    if not top:
        return [], ["no top-level on: trigger declaration"]
    names: list[str] = []
    errors: list[str] = []
    for indent, inline, line_no in top:
        found, err = trigger_names(lines, line_no, indent, inline)
        if err:
            errors.append(err)
        names.extend(found)
    return names, errors


def step_range(lines: list[str], uses_line: int, uses_indent: int, dashed: bool) -> tuple[int, int]:
    """Line range ``[start, end)`` the step owning ``uses_line`` occupies.

    ``uses_indent`` is the column of the step's keys. A dashed ``- uses:``
    line opens the step itself; otherwise the step opens at the nearest
    ``- `` line above at ``uses_indent - 2``. The step ends at the next
    ``- `` item at that dash indent or at a dedent below it.
    """
    dash_indent = uses_indent if dashed else uses_indent - 2
    start = uses_line - 1
    if not dashed:
        for cursor in range(uses_line - 2, -1, -1):
            raw = lines[cursor]
            if not raw.strip():
                continue
            indent = len(raw) - len(raw.lstrip())
            if indent == dash_indent and raw.lstrip().startswith("- "):
                start = cursor
                break
            if indent <= dash_indent:
                break
    end = len(lines)
    for cursor in range(uses_line, len(lines)):
        raw = lines[cursor]
        if not raw.strip() or raw.strip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip())
        if indent < dash_indent or (indent == dash_indent and raw.lstrip().startswith("- ")):
            end = cursor
            break
    return start, end


def step_persist_ok(lines: list[str], start: int, end: int) -> bool:
    found = False
    for index in range(start, end):
        body = strip_yaml_comment(lines[index]).strip()
        key, sep, value = body.partition(":")
        if sep and unquote(key) == "persist-credentials":
            found = True
            if unquote(value).lower() != "false":
                return False
    return found


def check_ci_authority(workflows: pathlib.Path, root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    for workflow in sorted(workflows.glob("*.yml")) + sorted(workflows.glob("*.yaml")):
        text = workflow.read_text(encoding="utf-8")
        rel = workflow.relative_to(root).as_posix()
        names, trigger_errors = workflow_triggers(text)
        errors.extend(f"{rel}: {err}" for err in trigger_errors)
        if "pull_request_target" in names:
            errors.append(f"{rel}: pull_request_target trigger is not allowed on repository workflows")
        lines = text.splitlines()
        steps: list[tuple[str, int, int, bool, str | None]] = []
        for indent, inline, line_no in key_lines(lines, "uses"):
            raw = lines[line_no - 1]
            dashed = raw.lstrip().startswith("- ")
            steps.append((unquote(inline), line_no, indent, dashed, None))
        for index, raw in enumerate(lines):
            flow = FLOW_STEP_RE.match(raw)
            if not flow:
                continue
            try:
                pairs = flow_pairs(flow.group(1))
            except ValueError:
                continue
            with_value = next((v for k, v in pairs if k == "with"), None)
            for key, value in pairs:
                if key == "uses":
                    steps.append((
                        value, index + 1,
                        len(raw) - len(raw.lstrip()), True, with_value,
                    ))
        for action, line_no, indent, dashed, with_value in steps:
            if action.startswith("./"):
                continue
            if not SHA_PIN_RE.fullmatch(action):
                errors.append(f"{rel}:{line_no}: action '{action}' is not pinned to a full commit SHA")
                continue
            if action.startswith("actions/checkout@"):
                if with_value is not None:
                    ok = False
                    if with_value.startswith("{") and with_value.endswith("}"):
                        try:
                            ok = any(
                                key == "persist-credentials" and value.lower() == "false"
                                for key, value in flow_pairs(with_value[1:-1])
                            )
                        except ValueError:
                            ok = False
                else:
                    start, end = step_range(lines, line_no, indent, dashed)
                    ok = step_persist_ok(lines, start, end)
                if not ok:
                    errors.append(
                        f"{rel}:{line_no}: actions/checkout step omits persist-credentials: false"
                    )
    return errors


def permission_values(text: str) -> tuple[list[tuple[str, int]], bool, list[str]]:
    """``(values, has_top_level, errors)`` — every declared permission scope.

    Values carry their line for diagnostics. Inline scalars, flow mappings
    and block mappings are normalized; forms this subset parser cannot prove
    (anchors, block scalars, sequences) fail closed.
    """
    lines = text.splitlines()
    values: list[tuple[str, int]] = []
    errors: list[str] = []
    has_top = False
    for indent, inline, line_no in key_lines(lines, "permissions"):
        has_top = has_top or indent == 0
        if inline:
            if inline.startswith("{"):
                if not inline.endswith("}"):
                    errors.append(f"line {line_no}: permissions flow mapping is not closed")
                    continue
                try:
                    values.extend((value, line_no) for _, value in flow_pairs(inline[1:-1]))
                except ValueError as exc:
                    errors.append(f"line {line_no}: {exc}")
            elif inline.startswith(("[", "|", ">", "&", "*", "!")):
                errors.append(f"line {line_no}: unsupported permissions YAML form {inline!r}")
            else:
                values.append((unquote(inline), line_no))
            continue
        found, err = block_values(lines, line_no, indent)
        if err:
            errors.append(err)
        values.extend((value, line_no) for value in found)
    return values, has_top, errors


def check_authorization_defaults(workflows: pathlib.Path, root: pathlib.Path,
                                 payload: dict) -> list[str]:
    errors: list[str] = []
    allowed = {scope.lower() for scope in payload.get("allowed_permission_scopes") or []}
    for workflow in sorted(workflows.glob("*.yml")) + sorted(workflows.glob("*.yaml")):
        text = workflow.read_text(encoding="utf-8")
        rel = workflow.relative_to(root).as_posix()
        values, has_top, parse_errors = permission_values(text)
        errors.extend(f"{rel}: {err}" for err in parse_errors)
        if not has_top:
            errors.append(f"{rel}: no top-level permissions block declares the token scope")
        for value, line_no in values:
            if value.lower() not in allowed:
                errors.append(
                    f"{rel}:{line_no}: permissions grant a non-allowed scope ('{value}')"
                )
    return errors


def host_allowed(host: str, allowed_hosts: set[str], allowed_suffixes: tuple[str, ...],
                 allowed_templates: set[str]) -> bool:
    if host.startswith("{"):
        return host in allowed_templates
    if host in allowed_hosts:
        return True
    return any(host.endswith(suffix) for suffix in allowed_suffixes)


def check_transport(root: pathlib.Path, payload: dict) -> list[str]:
    errors: list[str] = []
    allowed_hosts = set(payload.get("allowed_plaintext_hosts") or [])
    allowed_suffixes = tuple(payload.get("allowed_plaintext_suffixes") or ())
    allowed_templates = set(payload.get("allowed_plaintext_templates") or [])
    proc = subprocess.run(
        ["git", "ls-files", "-z"], cwd=root, capture_output=True
    )
    if proc.returncode != 0:
        return ["git ls-files failed while scanning transport surfaces"]
    tracked = [p for p in proc.stdout.decode().split("\0") if p]
    for rel in tracked:
        path = root / rel
        if path.suffix not in OPERATIONAL_SUFFIXES and path.name not in OPERATIONAL_NAMES:
            continue
        if not path.is_file():
            continue
        for line_no, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            for host in PLAINTEXT_RE.findall(line):
                if not host_allowed(host, allowed_hosts, allowed_suffixes, allowed_templates):
                    errors.append(f"{rel}:{line_no}: plaintext endpoint in operational surface: http://{host}")
    return errors


def check_container_runtime(root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    for path in sorted(root.rglob("*")):
        rel = path.relative_to(root)
        if any(part in {".git", "build-codex", "_deps"} or part.startswith("build") for part in rel.parts):
            continue
        if path.is_file() and CONTAINER_RE.match(path.name):
            for line_no, image in enumerate(FROM_RE.findall(path.read_text(encoding="utf-8")), start=1):
                if not DIGEST_PIN_RE.search(image):
                    errors.append(f"{rel.as_posix()}:{line_no}: container base image '{image}' is not digest-pinned")
    return errors


def check_release_artifact_controls(root: pathlib.Path, payload: dict) -> list[str]:
    errors: list[str] = []
    required_version = payload.get("artifact_policy_version")
    policy = root / "scripts" / "artifact-policy.json"
    if not policy.is_file():
        errors.append("scripts/artifact-policy.json is missing")
    else:
        try:
            artifact = json.loads(policy.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"artifact policy does not parse: {exc}")
        else:
            if not isinstance(artifact, dict):
                errors.append("artifact policy root must be an object")
            else:
                if type(artifact.get("version")) is not int or artifact["version"] != required_version:
                    errors.append(
                        f"artifact policy version is {artifact.get('version')!r}; "
                        f"expected {required_version!r}"
                    )
                if not isinstance(artifact.get("forbidden_dirs"), list) or not artifact["forbidden_dirs"]:
                    errors.append("artifact policy does not declare forbidden_dirs")
    if not (root / "scripts" / "check_no_binaries.py").is_file():
        errors.append("scripts/check_no_binaries.py is missing — binary-artifact control absent")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    workflows = root / ".github" / "workflows"

    payload, errors = load_policy(root)
    errors.extend(check_ci_authority(workflows, root))
    errors.extend(check_authorization_defaults(workflows, root, payload))
    errors.extend(check_transport(root, payload))
    errors.extend(check_container_runtime(root))
    errors.extend(check_release_artifact_controls(root, payload))

    for error in errors:
        sys.stderr.write(f"policy-check: {error}\n")
    if errors:
        return 1
    sys.stdout.write("repository policy check passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
