# ADR-0004: Phase-2 Canonical Manifest and Lockfile Write-Back

**Purpose:** Record the decision, context, alternatives, and consequences for deterministic manifest and lockfile write-back in the native phase-2 core.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

Phase 2 requires stable manifest and lockfile handling, but a read-only parser is not enough. Without canonical write-back, the repository cannot guarantee stable formatter behavior, deterministic lock emission, or reliable drift detection.

The project also already froze a subset of manifest and lockfile rules, so the native implementation needed a matching deterministic serialization policy.

## Decision

1. Add native canonical serializers for `spio.toml` and `spio.lock`.
2. Freeze deterministic section and field ordering for phase 2.
3. Sort:
   - `[[bin]]` entries by name
   - dependency aliases lexicographically
   - lock `[[package]]` entries by `id`
   - lock `dependencies` arrays lexicographically
4. Materialize `publish = false` in canonical package output when the package uses the phase-2 default.
5. Reuse the canonical manifest writer for generated project scaffolds.

## Alternatives

1. Keep write-back unspecified until the resolver phase.
   - Rejected because drift detection and fixture stability would stay weak.
2. Preserve source-file formatting exactly as written.
   - Rejected because semantic normalization needs a deterministic owner format.
3. Use an external TOML formatter as the canonical writer.
   - Rejected because the repo still needs schema-aware ordering and normalization rules.

## Consequences

Positive:

1. Manifest and lockfile output is now deterministic.
2. Fixture round-trip tests can verify real canonical behavior.
3. Future stale-lock checks have a stable output basis.

Negative:

1. Canonical output may rewrite user-authored formatting.
2. Serializer rules must be updated carefully whenever phase-2 schema rules expand.
