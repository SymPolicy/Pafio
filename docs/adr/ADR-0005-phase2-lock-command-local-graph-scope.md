# ADR-0005: Phase-2 `spio lock` Uses Only Local Workspace and Path Graphs

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `spio lock` implementation scope.

**Last updated:** 2026-04-10

## Status

Superseded by ADR-0007 for current resolver-backed `spio lock` behavior

## Context

The project has advanced to native manifest parsing, lockfile parsing, and canonical write-back, but it still does not have the full phase-3 resolver. The next useful phase-2 step is a minimal `spio lock` command, but implementing full pinned-`git` resolution now would prematurely drag resolver and fetch behavior into phase 2.

At the same time, phase-2 manifests already admit `git` dependencies for validation, so the lock command needs an explicit boundary rather than silently inventing pseudo-resolution behavior.

## Decision

1. Implement the first native `spio lock` over local graphs only:
   - local package manifests
   - workspace member packages
   - recursively discovered `path` dependencies
2. Emit deterministic local package identities without absolute paths.
3. Treat workspace-local packages as `source-kind = "workspace"`.
4. Treat non-workspace local dependencies as `source-kind = "path"`.
5. Reject `git` dependency locking in phase 2 with an explicit resolution error instead of inventing unresolved placeholder versions.
6. Support a `--check` mode that compares the generated canonical lockfile to the on-disk file and reports stale or missing lockfiles without rewriting them.

## Alternatives

1. Keep `spio lock` completely stubbed until the full resolver phase.
   - Rejected because phase 2 needs practical progress beyond parsing and write-back.
2. Invent placeholder versions for unresolved `git` dependencies.
   - Rejected because it would create lockfiles with fabricated semantic data.
3. Implement full `git` fetch and pinned revision resolution now.
   - Rejected because it prematurely collapses phase 2 and phase 3.

## Consequences

Positive:

1. `spio lock` becomes useful for local package and workspace graphs during phase 2.
2. Lockfile generation can progress without breaking the published delivery order.
3. Unsupported `git` locking fails explicitly instead of silently producing misleading lockfiles.

Negative:

1. Manifests containing `git` dependencies still validate but cannot yet be locked.
2. Another behavior line must be expanded later when the resolver phase lands.
