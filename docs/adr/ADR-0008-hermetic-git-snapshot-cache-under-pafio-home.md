# ADR-0008: Hermetic Git Snapshot Cache Lives Under `PAFIO_HOME`

**Purpose:** Record the decision, context, alternatives, and consequences for the first phase-3 git source cache layout.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

Phase-3 pinned `git` resolution needs a reproducible place to store fetched source state. The project already committed to hermetic `PAFIO_HOME` usage and to avoiding dependence on developer-local mutable state, but it had not yet frozen the first concrete cache layout.

If git source material is fetched ad hoc into project directories or arbitrary temporary paths, both test isolation and extractability become fragile.

## Decision

1. The first phase-3 git source cache lives under `PAFIO_HOME`.
2. `PAFIO_HOME` resolves as:
   - the `PAFIO_HOME` environment variable when set
   - otherwise `~/.pafio`
3. The minimal git cache layout is:
   - `PAFIO_HOME/git/repos/<repo-hash>.git` for the mirrored repository cache
   - `PAFIO_HOME/git/checkouts/<repo-hash>/<rev>/` for the extracted revision snapshot
4. The repository cache key is the normalized git source string hash.
5. The extracted snapshot key is `<repo-hash> + <rev>`.
6. Resolver reads manifests from extracted snapshots, not from mutable clones in the project tree.
7. Tests and gates must continue to use a fresh temporary `PAFIO_HOME`.

## Alternatives

1. Fetch git sources directly into the project-local `.pafio/`.
   - Rejected because source cache and build output have different lifecycles and isolation requirements.
2. Use only temporary directories and never reuse git source state.
   - Rejected because pinned git resolution would become unnecessarily slow and less deterministic across repeated runs.
3. Reuse the developer's global git clone cache or local checkouts.
   - Rejected because it violates hermeticity and makes gate behavior machine-dependent.

## Consequences

Positive:

1. Pinned git resolution gets a deterministic, reusable cache location.
2. Extracted snapshots provide a stable filesystem base for recursive `path` resolution inside git packages.
3. Test and gate isolation rules remain compatible with the existing `PAFIO_HOME` discipline.

Negative:

1. Even the minimal resolver now owns cache invalidation and layout details.
2. Additional cache-key policy will still be needed later for build outputs and compile-plan artifacts.
