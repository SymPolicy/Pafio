# ADR-0021: Phase-6 Activates Filesystem Registry Publish Transport

**Purpose:** Record the decision, context, alternatives, and consequences for the first non-dry-run `publish` transport in the native core.

**Last updated:** 2026-04-12

## Status

Accepted

## Context

`pafio pack` and `pafio publish --dry-run` already established deterministic source archives and local publish preflight. That made the front half of publishing real, but the back half was still missing.

At the same time, this repository still does not define:

- remote authentication
- network transport
- registry dependency declarations
- install-from-registry workflow
- checksum trust delegation beyond the registry root itself

That means the next useful step is not a full remote registry. It is the smallest immutable local registry contract that can exercise archive publication, index materialization, and checksum recording without inventing network behavior.

## Decision

1. Activate non-dry-run `publish` only for an explicit local filesystem registry root:
   - `pafio publish [--manifest-path <path>] [--package <package-name>] [--output <path>] --registry <path>`
2. Keep `publish --dry-run` unchanged as the local preflight path.
3. Continue to require:
   - `package.publish = true`
   - zero dependency entries in `[dependencies]` and `[dev-dependencies]`
4. Filesystem registry publish writes these paths under `<registry-root>`:
   - registry marker:
     - `pafio-registry.json`
   - immutable archive blob:
     - `blobs/sha256/<xx>/<yy>/<sha256>.tar`
   - version entry:
     - `index/<namespace>/<name>/<version>.json`
5. The registry marker format is:
   - `kind = "filesystem-local"`
   - `schema_version = 1`
6. The version entry records:
   - package name
   - version
   - archive `sha256`
   - archive size in bytes
   - relative blob path
   - publish timestamp in UTC
   - dependency arrays
7. Archive blobs are immutable and addressed by digest.
   - if the digest-named blob already exists, publication may reuse it
8. Package versions are immutable.
   - if the version entry already exists, publication fails explicitly
9. The first live transport remains local-only.
   - no network upload
   - no auth
   - no remote trust policy

## Alternatives

1. Keep non-dry-run `publish` blocked until a remote registry exists.
   - Rejected because the project can already validate immutable archive publication and checksum-backed index entry creation locally.
2. Publish directly into package/version-named archive paths without a separate blob store.
   - Rejected because digest-addressed blobs preserve immutable content identity and separate archive storage from index metadata.
3. Allow republishing the same package version if the bytes match.
   - Rejected because immutable version semantics are easier to reason about and align better with future registry behavior.
4. Start with network transport and ad hoc auth.
   - Rejected because no published protocol or trust model exists yet.

## Consequences

Positive:

1. `publish` is no longer only a preflight surface; it now exercises a real immutable archive publication path.
2. Checksums become part of the live workflow before any remote transport is introduced.
3. Later remote registry work can layer on top of a concrete blob-and-index model instead of inventing archive identity and upload behavior at the same time.

Negative:

1. The first live registry transport is intentionally narrow and local-only.
2. Registry dependency metadata is still not implemented, so publishable packages remain dependency-free in the current phase.
3. There is still no install-from-registry or remote synchronization story.
