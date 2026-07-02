# ADR-0009: Phase-3 `pafio tree` Renders the Resolver Graph Directly

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `pafio tree` command in phase 3.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

Phase 3 now has a real resolver for `workspace`, `path`, and pinned `git` under `single-version-v1`. The next missing user-facing capability from the phase-3 deliverables is dependency tree rendering.

If `pafio tree` stays stubbed, users cannot inspect the graph that `pafio lock` is actually resolving. If it reads only `pafio.lock`, then its behavior can drift from the active resolver whenever the adjacent lockfile is stale or absent.

## Decision

1. Activate `pafio tree` in phase 3 as a native read-only command.
2. Freeze the minimal public surface as:
   - `pafio tree`
   - `pafio tree --manifest-path <path>`
3. `pafio tree` resolves the graph directly from the selected manifest using the same resolver path as `pafio lock`.
   - it does not require an existing `pafio.lock`
   - it does not rewrite or validate `pafio.lock`
4. The command uses the same phase-3 source scope and conflict policy as `pafio lock`:
   - workspace packages
   - local `path` dependencies
   - pinned `git` dependencies with `rev`
   - `single-version-v1`
5. Human-readable tree output renders canonical lock package identifiers with ASCII connectors.
   - root package trees are emitted in sorted root-id order
   - child dependencies are emitted in sorted dependency-id order
6. Global `--json` success output returns the resolved graph as:
   - command metadata
   - sorted `root_ids`
   - package records with canonical ids and dependency ids
7. If dependency traversal encounters an ancestry cycle during rendering, the repeated package id is rendered once with a ` (cycle)` suffix and recursion stops at that edge.

## Alternatives

1. Keep `pafio tree` stubbed until build orchestration exists.
   - Rejected because phase 3 explicitly includes dependency tree rendering and the resolver now exists.
2. Render only from the adjacent `pafio.lock`.
   - Rejected because it would make `tree` depend on stale or missing lockfiles instead of the active resolver graph.
3. Invent a separate human label format unrelated to canonical lock ids.
   - Rejected because phase 3 already has deterministic package identities and the tree command should not create a second naming scheme.

## Consequences

Positive:

1. Users can inspect the exact graph that phase-3 resolution produces without first writing a lockfile.
2. `tree` and `lock` stay aligned because they share the same resolver result.
3. JSON output provides a machine-readable graph view without introducing a separate schema file yet.

Negative:

1. The resolver result now needs to expose root package identities in addition to lock package records.
2. The first text rendering format is intentionally minimal and may need extension later for filters or target-specific views.
