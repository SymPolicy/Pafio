# ADR-0016: Phase-6 Activates `spio publish --dry-run` as Local Preflight Without Registry Transport

**Purpose:** Record the decision, context, alternatives, and consequences for the first native `publish` surface and the explicit boundary between local publish preparation and later registry transport.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`spio` now has a native local package archive command through `spio pack`, but `publish` is still stubbed. Leaving it fully stubbed would keep the package-manager surface incomplete even though the project can already validate package metadata and stage a source artifact locally.

At the same time, this repository still does not define a registry transport contract, authentication flow, upload protocol, or registry dependency model. Any attempt to make `publish` talk to a real remote would be guesswork.

## Decision

1. Activate `publish` in two layers:
   - `spio publish --dry-run` is active now
   - non-dry-run `spio publish` remains blocked until a real registry transport contract exists
2. Freeze the first public surface as:
   - `spio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] [--dry-run]`
3. `spio publish --dry-run` performs local publish preflight and stages a publish candidate archive.
   - it reuses the `spio pack` source archive path
   - default output is still `<package-root>/dist/<short-name>-<version>.tar`
4. `publish --dry-run` selects packages using the same root-package rules as `pack`.
5. `publish --dry-run` requires the selected package to opt in with `package.publish = true`.
6. `publish --dry-run` currently requires zero dependency entries in both `[dependencies]` and `[dev-dependencies]`.
   - current native dependency source kinds are only `path` and pinned `git`
   - registry-addressable dependency declarations are not implemented yet
   - therefore any dependency entry is treated as not publishable in the current phase
7. Reserve exit code `17` for publish-specific failures, including local publish preflight failure and the current no-transport gate.
8. Non-dry-run `publish` fails immediately with a publish-specific error until registry transport is defined.
   - current user guidance is to use `--dry-run`

## Alternatives

1. Keep `publish` fully stubbed until registry transport exists.
   - Rejected because the project can already provide useful local publish validation and candidate artifact staging.
2. Let `publish` upload to an ad hoc local or fake endpoint before the protocol is frozen.
   - Rejected because that would invent transport behavior before a contract exists.
3. Allow dependency-bearing packages to pass local publish preflight.
   - Rejected because current dependency declarations cannot represent publishable registry dependencies yet, so success would be misleading.

## Consequences

Positive:

1. `publish` is no longer a pure stub; it now validates real package metadata and produces a concrete local publish candidate.
2. Registry transport can be added later on top of a stable local preflight path.
3. `package.publish` now has an enforced user-facing workflow meaning instead of being only serialized metadata.

Negative:

1. `publish --dry-run` is intentionally restrictive and currently only accepts dependency-free packages.
2. Non-dry-run `publish` is still unavailable until registry transport work lands.
3. `publish` and `pack` now share archive behavior, so future archive-format changes must keep both commands aligned.
