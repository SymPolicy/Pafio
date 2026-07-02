# Checkpoint Health

**Purpose:** Define the repository-wide build/test health entrypoint for `pafio` so CI and checkpoint delivery can call one script instead of wiring native checks inline.

**Last updated:** 2026-04-20

## Command

Default checkpoint health:

```bash
./scripts/checkpoint-health.sh
```

Checkpoint health against a published external compiler:

```bash
./scripts/checkpoint-health.sh --styio-bin /absolute/path/to/styio
```

## What It Runs

1. `native-check.sh`
2. `extractability-check.sh`
3. optional published-binary compatibility and interface probing through `preflight-readiness-check.py` when `--styio-bin` is provided

The repository keeps its native CMake/CTest tooling, but callers should continue to use this outer health entrypoint. Source-build mode is exercised through the public `pafio use build` and `pafio build minimal` command path rather than a separate checkpoint-health flag.
