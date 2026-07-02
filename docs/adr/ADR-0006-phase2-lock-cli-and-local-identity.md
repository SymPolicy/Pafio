# ADR-0006: Phase-2 `pafio lock` CLI Surface and Local Identity Rules

**Purpose:** Record the decision, context, alternatives, and consequences for the first public `pafio lock` command shape, stale-check behavior, and local lock package identity rules.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

`ADR-0005` freezes the phase-2 scope of `pafio lock` to local package, workspace, and recursive `path` graphs, with explicit rejection of `git` resolution. That leaves three public-contract questions that cannot stay implicit in code:

1. what argument surface phase-2 `pafio lock` exposes
2. how stale or missing lockfiles are reported
3. how local package identities are encoded without absolute paths

If these stay undocumented, the CLI surface, fixture expectations, and future resolver work will drift.

## Decision

1. Freeze the phase-2 public `pafio lock` surface as:
   - `pafio lock`
   - `pafio lock --manifest-path <path>`
   - `pafio lock --check`
   - `pafio lock --manifest-path <path> --check`
2. The lockfile path is not a free argument in phase 2.
   - The command always reads or writes the adjacent `pafio.lock` next to the selected manifest.
3. `--check` compares the generated canonical lockfile to the on-disk adjacent `pafio.lock`.
   - if the file is missing, return the lock exit code
   - if the file content differs, return the lock exit code
   - if the file matches canonical generated content, return success
4. Phase-2 local lock generation traverses both `[dependencies]` and `[dev-dependencies]`.
   - phase 2 captures the local graph shape rather than build-profile selection
5. Local lock package identifiers use deterministic source-kind/name/version encoding:
   - `workspace:<package-name>@<package-version>`
   - `path:<package-name>@<package-version>`
6. If two distinct local directories would produce the same phase-2 local package identifier, lock generation must fail with an explicit resolution error instead of silently merging them.
7. `git` dependencies remain valid manifest input in phase 2 but `pafio lock` must fail explicitly when it reaches them.

## Alternatives

1. Add `--lockfile-path` in phase 2.
   - Rejected because the phase-2 contract is intentionally narrow and already has a fixed adjacent-lock convention.
2. Encode relative paths directly into local lock package identifiers.
   - Rejected because it would produce noisier identities and make future resolver identity migration harder.
3. Traverse only `[dependencies]` and ignore `[dev-dependencies]`.
   - Rejected because the phase-2 lock command is graph capture, not profile-specific resolution.
4. Silently merge duplicate local package identities by first-seen path.
   - Rejected because it would hide ambiguous local graphs behind unstable lock output.

## Consequences

Positive:

1. `pafio lock` has a small, explicit, stable phase-2 surface.
2. Stale-lock behavior is deterministic and tied to the existing lock exit code contract.
3. Local identities stay readable and avoid absolute-path leakage.

Negative:

1. Phase-2 local graphs with duplicated `source-kind:name@version` identities are rejected even if a later resolver could model them more precisely.
2. Custom lockfile locations remain unavailable until a later phase explicitly expands the CLI contract.
