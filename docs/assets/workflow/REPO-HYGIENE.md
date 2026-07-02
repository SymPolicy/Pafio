# Repository Hygiene

**Purpose:** Define the repository hygiene entrypoint for `pafio` so contributors and CI use one script to reject generated artifacts, dependency payloads, and undocumented binary blobs.

**Last updated:** 2026-04-19

## Command

Tracked-tree hygiene:

```bash
python3 scripts/repo-hygiene-gate.py --mode tracked
```

Staged-only hygiene:

```bash
python3 scripts/repo-hygiene-gate.py --mode staged
```

Push-range hygiene:

```bash
python3 scripts/repo-hygiene-gate.py --mode push --range origin/main..HEAD
```

## Scope

This gate rejects generated build outputs, temporary package-manager state, stray binaries, shared `.gitignore` drift, and missing documentation references for the hygiene and delivery entrypoints, including `scripts/delivery-gate.sh`.
