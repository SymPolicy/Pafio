# spio

**Purpose:** `spio` is the standalone package manager and project workflow tool for Styio. It is designed to remain movable as a self-contained subtree and later as an independent repository.

**Last updated:** 2026-04-09

## Scope

- `spio` manages package manifests, lockfiles, dependency resolution, cache layout, build orchestration, and project-level commands.
- `spio` does not parse Styio source semantics on its own.
- `spio` talks to the Styio compiler through a versioned machine contract and a process boundary.

## Independence Rules

- `spio` must not include or link against `styio` implementation headers or libraries.
- `spio` must not depend on files under `../src`, `../tests`, or any other compiler-internal path.
- `spio` may depend on an external `styio` executable only through `SPIO_STYIO_BIN`.
- `spio/contracts/` is the source of truth for package-manager-side machine contracts.

## Tree

```text
spio/
  src/
  tests/
    unit/
    integration/
  docs/
  contracts/
  scripts/
```

## Transitional Note

The current repository root still hosts the existing `styio` compiler project directly. This `spio/` subtree is being prepared so it can later be moved wholesale into `$HOME/DevSpace/Unka-Malloc/styio-spio` without dragging compiler source code along with it.

## Implementation Stack Note

The active implementation target is a native `C++20` + `CMake` codebase aligned with the operational toolchain used by `styio`.

The existing Python bootstrap remains in-tree only as a temporary migration reference while native phase-2 parity is being built for:

- CLI shape
- manifest and lockfile validation rules
- machine-facing contract boundaries

Python is not the intended long-term implementation path for `spio`.

## Developer Context Pack

Before moving this subtree into `$HOME/DevSpace/Unka-Malloc/styio-spio`, `spio` developers should read:

- `docs/styio/Styio-for-Spio-Developers.md`
- `docs/styio/Styio-Public-Interface-Roadmap.md`
- `docs/governance/Spio-Version-Decoupling-Constraints.md`

Those documents are the migration knowledge pack for working against `styio` without creating hidden source-level dependencies.

## Planning Entry Points

For the full implementation and migration plan, start with:

- `docs/planning/Spio-Master-Plan.md`
- `docs/planning/Spio-Workstreams-and-TODOs.md`
- `docs/operations/Spio-Verification-Matrix.md`
- `docs/operations/Spio-Repo-Split-Runbook.md`

Recommended preflight before moving this subtree:

```text
./scripts/preflight-readiness-check.py --styio-bin /absolute/path/to/styio
```
