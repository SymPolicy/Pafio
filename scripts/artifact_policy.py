#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_POLICY_PATH = ROOT / "scripts" / "artifact-policy.json"


@dataclass(frozen=True)
class ArtifactPolicy:
    forbidden_dirs: tuple[str, ...]
    forbidden_file_names: tuple[str, ...]
    forbidden_file_globs: tuple[str, ...]
    tracked_binary_allow_globs: tuple[str, ...]
    required_gitignore_patterns: tuple[str, ...]
    required_delivery_paths: tuple[str, ...]
    export_excludes: tuple[str, ...]


def _read_str_list(payload: dict, key: str) -> tuple[str, ...]:
    value = payload.get(key, [])
    if not isinstance(value, list):
        raise ValueError(f"artifact policy field '{key}' must be a JSON array")
    strings: list[str] = []
    for entry in value:
        if not isinstance(entry, str):
            raise ValueError(f"artifact policy field '{key}' entries must be strings")
        strings.append(entry)
    return tuple(strings)


def load_policy(policy_path: pathlib.Path | None = None) -> ArtifactPolicy:
    path = (policy_path or DEFAULT_POLICY_PATH).resolve()
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"artifact policy must be a JSON object: {path}")
    return ArtifactPolicy(
        forbidden_dirs=_read_str_list(payload, "forbidden_dirs"),
        forbidden_file_names=_read_str_list(payload, "forbidden_file_names"),
        forbidden_file_globs=_read_str_list(payload, "forbidden_file_globs"),
        tracked_binary_allow_globs=_read_str_list(payload, "tracked_binary_allow_globs"),
        required_gitignore_patterns=_read_str_list(payload, "required_gitignore_patterns"),
        required_delivery_paths=_read_str_list(payload, "required_delivery_paths"),
        export_excludes=_read_str_list(payload, "export_excludes"),
    )
