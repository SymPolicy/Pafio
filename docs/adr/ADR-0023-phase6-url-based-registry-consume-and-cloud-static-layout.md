# ADR-0023: Phase-6 Activates URL-Based Registry Consumption and a Cloud-Deployable Static Repository Layout

**Last updated:** 2026-04-17  
**Status:** Accepted  
**Date:** 2026-04-12  
**Purpose:** Record the decision to make the existing filesystem-registry layout the canonical package-repository contract for both local and cloud deployment, and to activate client-side registry dependency resolution against URL-addressable registry roots.

## Context

`spio` already had deterministic source packaging and a local filesystem-registry publish transport:

- immutable tar blobs under `blobs/sha256/`
- per-version metadata under `index/<namespace>/<name>/<version>.json`
- a marker file at the registry root

That was enough to publish packages, but not enough to form a usable package ecosystem. Clients still could not declare registry dependencies, resolve them, download them, verify them, or cache them locally.

At the same time, a real cloud deployment should not require a bespoke server API if the registry can be hosted safely on static infrastructure such as:

- object storage
- CDN-backed static hosting
- Nginx/Apache serving immutable files

The next useful step is therefore not a custom network protocol. It is to treat the existing blob-and-index layout as the canonical repository format and let clients consume it through stable URLs.

## Decision

1. Registry dependencies are now a first-class manifest source kind:
   - dependency inline tables use:
     - `package = "namespace/name"`
     - `version = "x.y.z"`
     - `registry = "<url>"`
   - registry roots must use `file://`, `http://`, or `https://`

2. The existing filesystem-registry layout becomes the canonical repository layout for both local and cloud deployment.
   - local development and tests may use `file://...`
   - deployed registries may use `http://...` or `https://...`
   - clients fetch the same marker, index entries, and blobs regardless of transport

3. Registry clients resolve packages by:
   - validating the registry marker
   - loading the version entry for `package@version`
   - downloading or reusing the immutable blob identified by `sha256`
   - verifying the blob digest before extraction
   - materializing an extracted snapshot under `SPIO_HOME`

4. Registry client cache state lives under `SPIO_HOME/registry/`:
   - cached marker and entry metadata under `registry/index/`
   - blob cache under `registry/blobs/`
   - extracted snapshots under `registry/checkouts/`

5. Registry-resolved lock entries use:
   - `source-kind = "registry"`
   - `registry = "<normalized-url>"`
   - `sha256 = "<digest>"`
   - lock id:
     - `registry:<package-name>@<package-version>#<sha256>`

6. Published packages may contain dependencies only when those dependencies are themselves registry-addressable.
   - `path` and `git` dependencies remain invalid for published packages
   - version entries now record dependency metadata for both `[dependencies]` and `[dev-dependencies]`

7. Offline registry consumption must rely on cached marker metadata, cached version entries, cached blobs, or vendored/local file registries; it must not silently reach the network.

## Alternatives Considered

1. Invent a separate remote registry API unrelated to the existing filesystem layout.
   - Rejected because it would force two repository models at once and delay usable cloud deployment.

2. Allow manifest-side registry roots to be arbitrary local filesystem paths.
   - Rejected because lockfiles and manifests would then encode machine-local paths instead of deployable repository identities.

3. Keep publish restricted to zero-dependency packages even after registry consumption exists.
   - Rejected because that would block the first meaningful package graph even though the repository contract can now represent registry-addressable dependencies safely.

4. Make registry identity depend only on `package@version`.
   - Rejected because immutable blob digests are the real content identity and need to participate in lock/package ids.

## Consequences

Positive:

- `spio` can now consume packages from a single shared package repository.
- the same repository can be hosted locally or deployed behind plain HTTP(S) without inventing custom RPC.
- client integrity improves because fetched blobs are verified against recorded `sha256` digests before use.
- publish and consume now share one concrete repository contract.

Negative:

- the marker name still says `filesystem-local`, which is historically awkward once the same layout is hosted remotely.
- remote auth, trust policy, and registry writes beyond local filesystem publication are still separate follow-on work.
- registry dependency declarations now need stronger documentation discipline because they are part of the public contract.

## Follow-On Work

1. Add remote registry publication and synchronization on top of the same static layout.
2. Add checksum/trust hardening beyond raw `sha256` verification.
3. Add project-local vendoring for registry packages.
4. Add registry-aware workspace filtering and lock/update workflows.
