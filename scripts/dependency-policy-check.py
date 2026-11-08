#!/usr/bin/env python3
"""Fail-closed license and dependency boundary check.

Verifies the surfaces the retired centralized audit covered locally:

- the required license and dependency-evidence files exist and carry the
  declared Apache-2.0 boundary;
- every CMake ``FetchContent`` dependency is on the reviewed allowlist in
  ``scripts/dependency-policy.json`` (approved source prefix, scope, and
  pinned SHA-256), declares a matching ``URL_HASH``, and is named in
  ``DEPENDENCY-USAGE.md`` per that document's own dependency policy;
- test-scoped dependencies stay under ``tests/``;
- the component inventory and usage-boundary records are present.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys

FETCH_DECLARE = re.compile(r"FetchContent_Declare\s*\(", re.IGNORECASE)
FETCH_MAKE_AVAILABLE = re.compile(r"FetchContent_MakeAvailable\s*\(", re.IGNORECASE)
FIRST_ARGUMENT = re.compile(
    r'^\s*(?:"(?P<quoted>[^"]*)"|(?P<bracket>\[(=*)\[(?P<bracket_body>.*?)\]\3\])|(?P<bare>[^\s()"#]+))',
    re.DOTALL,
)
NAME_RE = re.compile(r"^[A-Za-z0-9_+.-]+$")
KEYWORD_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")

# Permitted FetchContent_Declare keywords with their value arity. Anything
# else — custom DOWNLOAD_COMMAND, PATCH_COMMAND, UPDATE_COMMAND, VCS options
# like GIT_REPOSITORY, or unrecognized uppercase options — is a closed-gate
# error because it can redirect or mutate the pinned artifact.
DECLARE_FLAGS = {"DOWNLOAD_NO_EXTRACT", "DOWNLOAD_NO_PROGRESS", "USES_TERMINAL_DOWNLOAD"}
DECLARE_OPTIONS = {
    "URL": 1,
    "URL_HASH": 1,
    "DOWNLOAD_EXTRACT_TIMESTAMP": 1,
    "DOWNLOAD_NAME": 1,
    "EXPECTED_HASH": 1,
    "EXPECTED_MD5": 1,
    "TIMEOUT": 1,
    "INACTIVITY_TIMEOUT": 1,
    "TLS_VERIFY": 1,
    "TLS_CAINFO": 1,
    "NETRC": 1,
    "NETRC_FILE": 1,
    "BINARY_DIR": 1,
    "SUBBUILD_DIR": 1,
    "PREFIX": 1,
    "SOURCE_SUBDIR": 1,
}

DEFAULT_POLICY = pathlib.Path(__file__).resolve().parent / "dependency-policy.json"

REQUIRED_FILES = (
    "LICENSE",
    "LICENSE-POLICY.md",
    "DEPENDENCY-USAGE.md",
    "docs/specs/TECHNOLOGY-COMPONENT-INVENTORY.md",
)

SPDX_ID_RE = re.compile(r"^[A-Za-z0-9-.+]+$")
TEST_SCOPE = {"tests", "test"}


def load_policy(path: pathlib.Path) -> dict[str, dict[str, str]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    project_license = payload.get("project_license")
    if not isinstance(project_license, str) or not SPDX_ID_RE.fullmatch(project_license):
        raise ValueError("dependency policy must declare a valid SPDX project_license")
    approved = payload.get("approved_dependencies")
    if not isinstance(approved, dict):
        raise ValueError("dependency policy must define an approved_dependencies object")
    for name, entry in approved.items():
        for field in ("source_prefix", "scope", "sha256", "license"):
            if not isinstance(entry.get(field), str) or not entry[field]:
                raise ValueError(f"dependency policy entry '{name}' is missing '{field}'")
        if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ValueError(f"dependency policy entry '{name}' sha256 must be 64 lowercase hex")
        if not SPDX_ID_RE.fullmatch(entry["license"]):
            raise ValueError(f"dependency policy entry '{name}' license is not an SPDX identifier")
        if entry["scope"] not in {"build", "test"}:
            raise ValueError(f"dependency policy entry '{name}' scope must be build or test")
    return payload


def tracked_cmake_files(root: pathlib.Path) -> list[pathlib.Path]:
    """Every tracked ``CMakeLists.txt`` and ``*.cmake`` module is active input,
    wherever it sits in the tree; untracked and generated files are outside the
    policy surface."""
    proc = subprocess.run(
        [
            "git", "ls-files", "-z", "--",
            "CMakeLists.txt", "**/CMakeLists.txt", "*.cmake", "**/*.cmake",
        ],
        cwd=root,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode().strip() or "git ls-files failed")
    return [
        root / rel
        for rel in proc.stdout.decode().split("\0")
        if rel and (root / rel).is_file()
    ]


def strip_cmake_comments(text: str) -> str:
    """Drop ``#`` line comments while preserving ``#`` inside quoted strings,
    so commented-out keywords cannot masquerade as active arguments."""
    lines = []
    for line in text.splitlines():
        in_quote = False
        for index, char in enumerate(line):
            if char == '"':
                in_quote = not in_quote
            elif char == "#" and not in_quote:
                line = line[:index]
                break
        lines.append(line)
    return "\n".join(lines)


def _command_body(text: str, open_paren: int) -> str | None:
    """Slice the argument body of a CMake command invocation, honoring quoted
    and bracket arguments; ``None`` when the invocation never closes."""
    depth = 1
    index = open_paren
    while index < len(text):
        char = text[index]
        if char == '"':
            end = text.find('"', index + 1)
            if end < 0:
                return None
            index = end + 1
            continue
        if char == "[":
            bracket = re.match(r"\[(=*)\[", text[index:])
            if bracket:
                close = "]" + bracket.group(1) + "]"
                end = text.find(close, index + len(bracket.group(0)))
                if end < 0:
                    return None
                index = end + len(close)
                continue
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren:index]
        index += 1
    return None


def _tokens(body: str) -> list[str] | None:
    tokens: list[str] = []
    rest = body
    while rest.strip():
        match = FIRST_ARGUMENT.match(rest)
        if not match:
            return None
        tokens.append(match.group("quoted") or match.group("bracket_body") or match.group("bare"))
        rest = rest[match.end():]
    return tokens


def _parse_declare_body(body: str) -> tuple[str, str, str, str | None]:
    """Return ``(name, url, digest, error)``: the first argument is the
    dependency name, the remainder must be allowlisted keywords; an
    unparseable or unsupported form is a closed-gate error."""
    tokens = _tokens(body)
    if not tokens or not NAME_RE.fullmatch(tokens[0]):
        return "", "", "", "unparseable FetchContent_Declare name"
    url = digest = ""
    index = 1
    while index < len(tokens):
        keyword = tokens[index]
        if keyword in DECLARE_FLAGS:
            index += 1
            continue
        if keyword not in DECLARE_OPTIONS:
            if KEYWORD_RE.fullmatch(keyword):
                return "", "", "", f"unsupported FetchContent_Declare option '{keyword}'"
            return "", "", "", f"unparseable FetchContent_Declare argument '{keyword}'"
        arity = DECLARE_OPTIONS[keyword]
        values = tokens[index + 1 : index + 1 + arity]
        if len(values) != arity:
            return "", "", "", f"FetchContent_Declare option '{keyword}' is missing a value"
        if keyword == "URL":
            url = values[0]
        elif keyword == "URL_HASH":
            match = re.fullmatch(r"SHA256=([0-9a-fA-F]{64})", values[0])
            if not match:
                return "", "", "", "FetchContent_Declare URL_HASH is not 'SHA256=<64 hex>'"
            digest = match.group(1)
        index += 1 + arity
    return tokens[0], url, digest, None


def _arguments(body: str) -> list[str] | None:
    names: list[str] = []
    rest = body
    while rest.strip():
        match = FIRST_ARGUMENT.match(rest)
        if not match:
            return None
        name = match.group("quoted") or match.group("bracket_body") or match.group("bare")
        if not name or not NAME_RE.fullmatch(name):
            return None
        names.append(name)
        rest = rest[match.end():]
    return names


def cmake_dependencies(
    root: pathlib.Path,
) -> tuple[list[tuple[str, str, str, pathlib.Path]], list[str]]:
    """Parse every tracked CMake input; any ``FetchContent`` invocation whose
    arguments cannot be tokenized is a closed-gate error, never a skip."""
    found: list[tuple[str, str, str, pathlib.Path]] = []
    errors: list[str] = []
    for path in tracked_cmake_files(root):
        rel = path.relative_to(root)
        text = strip_cmake_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in FETCH_DECLARE.finditer(text):
            body = _command_body(text, match.end())
            if body is None:
                errors.append(f"{rel}: unclosed FetchContent_Declare invocation")
                continue
            name, url, digest, error = _parse_declare_body(body)
            if error:
                errors.append(f"{rel}: {error}")
                continue
            found.append((name, url, digest, rel, "declare"))
        for match in FETCH_MAKE_AVAILABLE.finditer(text):
            body = _command_body(text, match.end())
            if body is None:
                errors.append(f"{rel}: unclosed FetchContent_MakeAvailable invocation")
                continue
            names = _arguments(body)
            if not names:
                errors.append(f"{rel}: unparseable FetchContent_MakeAvailable arguments")
                continue
            for name in names:
                found.append((name, "", "", rel, "make_available"))
    return found, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--policy", type=pathlib.Path, default=DEFAULT_POLICY)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    errors: list[str] = []

    try:
        policy = load_policy(args.policy.resolve())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        sys.stderr.write(f"dependency-policy: {exc}\n")
        return 1
    approved_deps = policy["approved_dependencies"]
    project_license = policy["project_license"]

    texts: dict[str, str] = {}
    for rel in REQUIRED_FILES:
        path = root / rel
        if not path.is_file() or not (text := path.read_text(encoding="utf-8", errors="replace").strip()):
            errors.append(f"required policy file missing or empty: {rel}")
            continue
        texts[rel] = text

    if license_text := texts.get("LICENSE"):
        if not re.search(rf"Apache License[,\s]+Version 2\.0", license_text):
            errors.append(f"LICENSE is not the declared {project_license} license text")
        if project_license == "Apache-2.0" and not re.search(
            r"www\.apache\.org/licenses/LICENSE-2\.0", license_text
        ):
            errors.append("LICENSE does not carry the Apache-2.0 license URL")
    if (policy_text := texts.get("LICENSE-POLICY.md")) and project_license not in policy_text:
        errors.append(f"LICENSE-POLICY.md does not declare the {project_license} SPDX identifier")

    usage = texts.get("DEPENDENCY-USAGE.md", "")
    if usage and "Dependency policy" not in usage:
        errors.append("DEPENDENCY-USAGE.md no longer carries the dependency policy section")

    dependencies, parse_errors = cmake_dependencies(root)
    errors.extend(parse_errors)
    for name, url, digest, rel, kind in dependencies:
        approved = approved_deps.get(name)
        if approved is None:
            errors.append(f"{rel}: FetchContent dependency '{name}' is not on the approved-source allowlist")
            continue
        if kind == "declare" and not url.startswith(approved["source_prefix"]):
            errors.append(f"{rel}: dependency '{name}' uses an unapproved source: {url or '<no URL>'}")
        if kind != "declare":
            continue
        if not digest:
            errors.append(f"{rel}: dependency '{name}' declares no URL_HASH SHA256")
        elif digest.lower() != approved["sha256"]:
            errors.append(f"{rel}: dependency '{name}' SHA256 does not match the reviewed pin")
        if approved["scope"] == "test" and rel.parts[0] not in TEST_SCOPE:
            errors.append(f"{rel}: test-scoped dependency '{name}' is declared outside the test scope")
        if usage:
            entry_line = next(
                (line for line in usage.splitlines() if name in line), None
            )
            if entry_line is None:
                errors.append(f"{rel}: dependency '{name}' is not recorded in DEPENDENCY-USAGE.md")
            elif approved["license"] not in entry_line:
                errors.append(
                    f"{rel}: dependency '{name}' license '{approved['license']}' is not recorded in DEPENDENCY-USAGE.md"
                )
        inventory = texts.get("docs/specs/TECHNOLOGY-COMPONENT-INVENTORY.md", "")
        if inventory and name not in inventory:
            errors.append(f"{rel}: dependency '{name}' is not reconciled in the component inventory")

    for error in errors:
        sys.stderr.write(f"dependency-policy: {error}\n")
    if errors:
        return 1
    sys.stdout.write("dependency policy check passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
