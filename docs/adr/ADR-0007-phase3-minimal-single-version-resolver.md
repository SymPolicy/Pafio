# ADR-0007: Phase-3 Minimal Resolver Uses `single-version-v1` Across Workspace, Path, and Pinned Git

**Purpose:** Record the decision, context, alternatives, and consequences for the first native phase-3 resolver increment.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

Phase 2 established a native manifest/lock core plus canonical write-back, and `ADR-0005` intentionally kept `spio lock` limited to local graphs. The next critical-path step is phase 3: a real resolver that can cover `workspace`, `path`, and pinned `git` sources under the intentionally conservative `single-version-v1` policy.

Without freezing the first resolver boundary, `git` support would drift between CLI behavior, lockfile output, and cache implementation details.

## Decision

1. Replace the phase-2 local-only lock generation path with a resolver-backed phase-3 path for `spio lock`.
2. The first native phase-3 resolver must support:
   - workspace packages
   - local `path` dependencies
   - pinned `git` dependencies declared with `git` and `rev`
3. The pinned `git` dependency manifest at the resolved revision is authoritative for:
   - package name
   - package version
   - transitive dependencies
4. The resolver policy is `single-version-v1`:
   - one package name may resolve to only one effective package version
   - one package name may resolve to only one effective source fingerprint
5. A source fingerprint mismatch is a resolution error even when the package version matches.
   - example: the same package name at the same version but from two different pinned git revisions
   - example: the same package name at the same version but from two different unrelated local directories
6. Resolved lock output must include transitive dependencies across all supported source kinds instead of treating pinned `git` as a leaf placeholder.
7. Git-backed lock entries must record both `git` source and pinned `rev`.
8. `path` dependencies discovered inside a pinned git snapshot must stay within that snapshot and must not escape onto the host filesystem.
9. Registry resolution, semver range solving, and multi-version graphs remain out of scope for this increment.

## Alternatives

1. Keep `git` as a leaf-only placeholder in lockfiles.
   - Rejected because that is not a real resolver and would hide transitive graph shape.
2. Allow same-name same-version packages from different source fingerprints under `single-version-v1`.
   - Rejected because it makes source provenance ambiguous and weakens reproducibility.
3. Skip phase 3 and jump straight to registry-oriented solving.
   - Rejected because the project still needs local and pinned-git workflows first.

## Consequences

Positive:

1. `spio lock` becomes a real resolver-backed command instead of a local graph expander.
2. Pinned `git` dependencies participate in the same graph and conflict rules as workspace and path packages.
3. The single-version policy stays explicit and deterministic.

Negative:

1. Some graphs that other ecosystems accept will now fail early because `single-version-v1` is intentionally strict.
2. Resolver behavior now depends on a minimal git source cache, which adds operational complexity before build orchestration exists.
