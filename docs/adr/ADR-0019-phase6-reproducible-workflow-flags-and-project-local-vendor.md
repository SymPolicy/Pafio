# ADR-0019: Phase-6 Activates Reproducibility Flags and Project-Local Vendor State

**Purpose:** Record the decision, context, alternatives, and consequences for introducing `--locked`, `--offline`, `--frozen`, and a project-local `pafio vendor` workflow in the native core.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`pafio` now has a real manifest core, a real resolver, local lock generation, dry-run compile-plan emission, deterministic local packaging, and managed compiler installation. That is enough to use the tool locally, but not enough to make it reproducible in CI or resilient to disconnected environments.

Two gaps were still obvious:

1. workflow commands could succeed without an adjacent lockfile, which makes repeatability weaker than it should be
2. pinned git dependencies depended on `PAFIO_HOME` cache state, but the project itself had no local vendored snapshot workflow

The current repository also already uses ordinary `vendor/` directories as local path dependency roots in tests and fixtures. Reusing plain `vendor/` as a reserved `pafio` area would create immediate collisions.

## Decision

1. Activate three reproducibility workflow flags on resolver-backed workflow commands:
   - `--locked`
   - `--offline`
   - `--frozen`
2. In the current native core, these flags apply to:
   - `pafio check`
   - `pafio fetch`
   - `pafio vendor`
   - `pafio build`
   - `pafio run`
   - `pafio test`
3. `--locked` requires an adjacent `pafio.lock` to exist and to match the active resolver graph.
4. `--offline` forbids network fetches for pinned git sources.
   - offline resolution may still use already cached git mirrors and snapshots under `PAFIO_HOME`
   - offline resolution may also use project-local vendored snapshots
5. `--frozen` is defined as `--locked` plus `--offline`.
6. Activate a new native command:
   - `pafio vendor [--manifest-path <path>] [--output <path>] [--locked|--offline|--frozen]`
7. The default project-local vendor root is `.pafio/vendor/` next to the selected manifest.
   - this intentionally avoids colliding with existing local path dependency trees such as `vendor/<pkg>`
8. Vendored git snapshots are stored under:
   - `.pafio/vendor/git/<repo-hash>/<rev>/`
9. Vendor metadata is written to:
   - `.pafio/vendor/pafio-vendor.json`
10. Resolver-backed commands may prefer project-local vendored snapshots before falling back to `PAFIO_HOME`.
11. `pafio lock` gains `--offline`, but not `--locked` or `--frozen`.
   - `pafio lock --check` remains the explicit lock-validation surface
12. Vendor failures use a dedicated exit code family:
   - exit `19`

## Alternatives

1. Reuse plain `vendor/` as the native vendor root.
   - Rejected because the current repository already uses plain `vendor/` paths for normal local path dependencies.
2. Make offline mode depend only on `PAFIO_HOME`.
   - Rejected because that makes project-level reproducibility weaker and harder to move between machines and CI jobs.
3. Add `--locked` only to build-like commands and leave `check` and `fetch` loose.
   - Rejected because validation and source preparation are also part of reproducible workflow enforcement.
4. Add vendor mode only after registry transport exists.
   - Rejected because vendoring pinned git sources is already valuable before registry work starts.

## Consequences

Positive:

1. `pafio` now has a real reproducibility contract for core workflow commands instead of relying only on convention.
2. Pinned git dependencies can be materialized into project-local state and reused offline.
3. The implementation stays compatible with the current decoupling rules because vendoring still works on resolved snapshots, not compiler internals.

Negative:

1. Workflow command syntax becomes larger because reproducibility flags are now part of the public surface.
2. The first vendor implementation is project-local state under `.pafio/vendor/`, not yet a polished committed vendor directory workflow.
3. Offline mode still depends on the current source model of workspace, path, and pinned git; it is not yet a full registry-era offline story.
