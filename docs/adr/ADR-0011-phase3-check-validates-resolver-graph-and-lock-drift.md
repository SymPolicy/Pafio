# ADR-0011: Phase-3 `pafio check` Validates the Resolver Graph and Lock Drift

**Purpose:** Record the decision, context, alternatives, and consequences for upgrading `pafio check` from syntax-only validation to graph-aware validation.

**Last updated:** 2026-04-11

## Status

Accepted

## Context

`pafio` now has a native phase-3 resolver, lock generation, dependency tree rendering, and dependency editing commands. Leaving `pafio check` as a manifest parser plus optional lockfile parser would make it the weakest validation command in the tool, even though users reasonably expect it to answer whether the current package state is actually coherent.

That gap is especially visible when a project has:

- a broken `path` dependency and no lockfile
- a stale `pafio.lock` that still parses but no longer matches the active graph

## Decision

1. Upgrade `pafio check` in phase 3 to validate the active resolver graph after manifest parsing succeeds.
2. `pafio check` must use the same `single-version-v1` resolver path as `pafio fetch`, `pafio lock`, and `pafio tree`.
3. If an adjacent `pafio.lock` exists, `pafio check` must:
   - validate that the lockfile parses and matches schema rules
   - compare the on-disk file to the canonical lockfile generated from the active graph
4. A parse-valid but content-stale adjacent lockfile is still a lock failure.
5. If no adjacent `pafio.lock` exists, `pafio check` may still succeed once manifest validation, graph resolution, and optional compiler handshake all succeed.
6. Resolver-backed `check` runs before the optional external compiler handshake.
   - invalid dependency graphs must fail before `styio` probing starts
7. Resolver-backed `check` may materialize source cache state under `PAFIO_HOME` when pinned git dependencies are present.

## Alternatives

1. Keep `pafio check` as syntax-only validation and require `pafio lock --check` for drift.
   - Rejected because it splits basic project validation across two commands in a surprising way.
2. Compare lockfiles semantically after parsing instead of using canonical text.
   - Rejected because canonical write-back is already a project invariant and drift should include non-canonical lock output.
3. Run the optional compiler handshake before resolver validation.
   - Rejected because compiler probing is irrelevant when the package graph is already invalid.

## Consequences

Positive:

1. `pafio check` becomes a real project-state validation command instead of a syntax gate.
2. Users get stale-lock detection without having to remember a separate `lock --check` call.
3. The basic package-management workflow stays aligned around one resolver path.

Negative:

1. `pafio check` may now touch `PAFIO_HOME` for pinned git projects.
2. Validation cost increases because check now resolves the graph instead of only parsing files.
