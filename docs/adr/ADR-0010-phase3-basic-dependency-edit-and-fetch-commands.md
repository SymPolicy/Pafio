# ADR-0010: Phase-3 Activates Basic Dependency Edit and Fetch Commands

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `add`, `remove`, and `fetch` commands.

**Last updated:** 2026-04-11

## Status

Accepted

## Context

Phase 3 now has a working resolver, lock generation, tree rendering, and hermetic git cache state. The remaining gap in the basic package-manager surface is that dependency manifests still have to be edited manually and cache warm-up still has to happen indirectly through other commands.

Without native dependency edit and fetch commands, `spio` still behaves more like a validator plus resolver library than a practical package manager.

## Decision

1. Activate `spio add`, `spio remove`, and `spio fetch` in phase 3.
2. Freeze the minimal public surfaces as:
   - `spio add <package-name> (--path <path> | --git <source> --rev <rev>) [--alias <name>] [--dev] [--manifest-path <path>]`
   - `spio remove <alias-or-package> [--dev] [--manifest-path <path>]`
   - `spio fetch [--manifest-path <path>]`
3. `add` and `remove` operate only on manifests that define `[package]`.
   - workspace-only manifests are valid overall, but they are not valid dependency-edit targets for this increment
4. `add` defaults the dependency alias to the package short name when `--alias` is omitted.
5. `add` always materializes `package = "<namespace/name>"` in the dependency entry.
6. `add` rejects duplicate dependency aliases and duplicate dependency package identities across both dependency sections.
7. `remove` matches by alias first.
   - when the target contains `/` and alias matching finds nothing, it may fall back to unique package-name matching
   - ambiguous matches remain an error
8. `--dev` selects `[dev-dependencies]`; otherwise `add` writes to `[dependencies]` and `remove` searches both sections.
9. Successful `add` and `remove` must:
   - rewrite the manifest canonically
   - refresh the adjacent `spio.lock` through the active phase-3 resolver
10. If the post-edit resolver step fails, `add` and `remove` must roll back both the manifest and the adjacent lockfile to their pre-command state.
11. `fetch` resolves the active graph and materializes dependency source state, especially pinned git caches under `SPIO_HOME`, but does not rewrite manifest or lock files.

## Alternatives

1. Keep dependency edits as manual TOML changes plus explicit `spio lock`.
   - Rejected because that leaves the package-manager core surface incomplete.
2. Make `fetch` read only from an existing `spio.lock`.
   - Rejected because phase 3 already treats the resolver graph as authoritative and fetch should not require a preexisting lockfile.
3. Let `add` and `remove` write the manifest even if lock refresh fails.
   - Rejected because that would leave the repository in a partially updated and harder-to-reason-about state.

## Consequences

Positive:

1. `spio` now has a usable core package-management loop: edit dependencies, fetch sources, inspect tree, and refresh lockfiles.
2. Dependency edits stay aligned with the resolver because `add` and `remove` refresh lockfiles through the same phase-3 graph path.
3. Rollback keeps manifest and lock state coherent when resolution fails.

Negative:

1. `add` and `remove` now own a small transactional write path for manifest and lock rollback.
2. Workspace-only manifests still need an explicit package target mechanism in a later phase if dependency editing should address members directly.
