# ADR-0024: Phase-6 Activates Anonymous HTTP Remote Registry Publish

**Last updated:** 2026-04-17  
**Status:** Accepted  
**Date:** 2026-04-12  
**Purpose:** Record the first remote registry write contract, keep it aligned with the existing static blob-and-index layout, and defer auth without blocking cloud-hosted package publication.

## Context

`spio` already had:

- deterministic package archives
- local publish preflight
- a filesystem registry publish transport
- registry consumption through `file://`, `http://`, and `https://`

That made the repository layout cloud-deployable for reads, but not cloud-usable for writes. The next step needs to enable publication to a remote registry without inventing a second repository format or a custom RPC layer.

At the same time, auth is still unsettled. Blocking remote writes on auth would delay the first usable cloud package flow even though the static repository layout is already sufficient for immutable object publication.

## Decision

1. Activate remote non-dry-run `publish` for `http://` and `https://` registry roots.
   - `spio publish --registry https://packages.example.test`
   - the existing `--registry` flag now accepts either a local path, `file://...`, or `http(s)://...`

2. Keep the repository layout identical to the existing local/filesystem registry contract.
   - marker:
     - `spio-registry.json`
   - immutable blob:
     - `blobs/sha256/<xx>/<yy>/<sha256>.tar`
   - version entry:
     - `index/<namespace>/<name>/<version>.json`

3. Use anonymous HTTP `PUT` as the first remote write protocol.
   - no custom JSON upload endpoint
   - no multipart API
   - no separate sync protocol
   - the client writes directly to the canonical marker/blob/entry object paths

4. Require remote origins used for publication to support:
   - `GET`
   - `HEAD`
   - `PUT`

5. Keep immutable publish semantics.
   - digest-addressed blobs may be reused when already present
   - package version entries remain immutable
   - overwrite attempts for immutable objects should be rejected with `409 Conflict`

6. Keep auth explicitly out of scope for this phase.
   - anonymous writes are acceptable for the current contract
   - auth, account policy, and delegated trust are follow-on work

## Alternatives Considered

1. Wait for a full authenticated registry API before enabling remote writes.
   - Rejected because the static repository layout already supports immutable object publication and the project needs a usable cloud path now.

2. Invent a separate remote write API that later translates into the static layout.
   - Rejected because it would create two registry contracts at once and make publish/consume harder to reason about.

3. Continue to support only local filesystem publication.
   - Rejected because that leaves cloud-hosted registries read-only from the perspective of `spio`.

## Consequences

Positive:

- remote publication now uses the same object layout that clients already consume
- the first cloud write path stays operationally simple
- deployment can start with an upload-capable HTTP origin or proxy instead of a bespoke registry server

Negative:

- there is still no auth or identity layer
- remote overwrite prevention depends on origin behavior plus client preflight
- the current trust model remains checksum-based rather than signature-based

## Follow-On Work

1. Add auth and account policy for remote writes.
2. Add stronger trust material such as signatures or transparency-style append-only guarantees.
3. Add registry synchronization and mirroring flows for multi-origin deployment.
4. Add remote publish CI and operational runbooks for object storage/CDN environments.
