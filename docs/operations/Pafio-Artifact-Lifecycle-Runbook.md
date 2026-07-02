# Pafio Artifact Lifecycle Runbook

**Purpose:** Define the enforceable lifecycle for generated files, artifact policy inputs, and delivery-tree exclusions so temporary outputs never leak into tracked sources or exported packages.

**Last updated:** 2026-04-15  
**Owner:** repository maintainers (`scripts/submit-gate.py` gate owners)

## Purpose

Define a single, enforceable lifecycle for all generated files so local caches/build outputs never leak into GitHub or delivery artifacts.

## Single Source of Truth

- Artifact policy file: `scripts/artifact-policy.json`
- Policy loader: `scripts/artifact_policy.py`
- Export exclude projection: `scripts/artifact-policy-rsync-excludes.py`

All generated-file rules must be changed in `artifact-policy.json` first, then propagated by running gates.

## Lifecycle Classes

1. Local ephemeral artifacts (must never be tracked):
- Build/test/cache dirs: `build/`, `build-codex/`, `.build/`, `.pytest_cache/`, `.cache/`, `.mypy_cache/`, `.ruff_cache/`, `.tox/`, `.nox/`
- Tool/runtime temp dirs: `.pafio/`, `tmp/`, `Testing/`, `.venv/`, `venv/`, `node_modules/`
- Editor/OS files: `.DS_Store`, `.idea/`, `.vscode/`, `__pycache__/`

2. Generated package/bundle artifacts (must never be tracked):
- `*.tar`, `*.tar.gz`, `*.tgz`, `*.zip`, `*.7z`, `*.gz`, `*.bz2`, `*.xz`
- `*.log`, `*.tmp`

3. Private extension trees (must never ship in open-source delivery):
- `src-private/`, `tests-private/`, `docs-private/`, `scripts-private/`

4. Delivery-required source trees (must exist in export package):
- `src/`, `tests/`, `docs/`, `contracts/`, `scripts/`, `.github/workflows/`

5. Intentional binary exceptions (default empty allowlist):
- configured by `tracked_binary_allow_globs` in `scripts/artifact-policy.json`
- only for test fixtures, mandatory static assets, or byte-level golden compatibility samples
- patterns must be narrow and path-scoped (for example `tests/fixtures/binary/*.zip`)
- broad patterns (`*`, `**`, root-wide globs) are disallowed by review policy

## Enforcement Chain

1. Local pre-push:
- `python3 scripts/submit-gate.py --profile pre-push`

2. CI:
- `python3 scripts/submit-gate.py --profile ci --json`

3. Quality gates:
- `scripts/check_no_binaries.py` rejects tracked binary content.
- `scripts/repo-hygiene-check.py` checks forbidden paths and `.gitignore` coverage.

4. Delivery gate:
- `scripts/delivery-gate.py` exports a temp delivery tree and re-runs hygiene checks inside export.

5. Export filter:
- `scripts/copy-to-external-repo.sh` consumes excludes generated from `artifact-policy.json`.

## Change Procedure for New Generated Files

When introducing a new tool/build step that creates files:

1. Add new patterns to `scripts/artifact-policy.json`.
2. Add matching ignore entries to `.gitignore` (validated by `repo-hygiene-check.py`).
3. If delivery behavior changes, verify `scripts/delivery-gate.py` still passes.
4. Update gate docs if command behavior changes:
- `docs/operations/Pafio-Verification-Matrix.md`
- `docs/governance/Pafio-Entry-Argument-Index.md`
5. Run local acceptance:
- `python3 scripts/repo-hygiene-check.py`
- `python3 scripts/delivery-gate.py`
- `python3 scripts/submit-gate.py --profile pre-push`

## Cleanup Routine

Before release tagging:

1. Run `python3 scripts/submit-gate.py --profile pre-push`.
2. If gate reports forbidden artifacts, remove local generated data and rerun.
3. Never force-add generated/private files with `git add -f`.
