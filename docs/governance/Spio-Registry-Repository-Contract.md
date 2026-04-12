# Spio Registry Repository Contract

**Purpose:** Define the repository layout and client behavior for `spio` package registries that can be hosted either locally or through cloud/static infrastructure.

**Last updated:** 2026-04-12

## 1. Scope

See also:

- [../registry/Spio-Registry-Client-Contract.md](../registry/Spio-Registry-Client-Contract.md)
- [../registry/Spio-Registry-Server-Contract.md](../registry/Spio-Registry-Server-Contract.md)
- [../registry/Spio-Registry-Deployment-Baseline.md](../registry/Spio-Registry-Deployment-Baseline.md)

This document owns the external repository contract for package distribution. It defines:

- repository root layout
- version-entry JSON shape
- supported registry URL schemes
- client-side fetch, verification, and cache behavior
- writer-side HTTP upload behavior
- lockfile and manifest rules tied to registry sources

It does not define:

- authentication or account systems
- compiler-facing `styio` interfaces

## 2. Repository Root Layout

A registry root uses these paths:

- marker:
  - `spio-registry.json`
- immutable blobs:
  - `blobs/sha256/<xx>/<yy>/<sha256>.tar`
- version entries:
  - `index/<namespace>/<name>/<version>.json`

This layout is intentionally static-host-friendly:

- local tests and development may use `file://...`
- deployed registries may expose the same files over `http://...` or `https://...`
- clients perform direct reads or HTTP GETs; no custom RPC is required
- remote writers upload to the same paths through HTTP `PUT`; no separate write RPC is required in the current phase
- write roots and read roots may be separated operationally as long as the read root is eventually populated with the same canonical objects

## 3. Marker Contract

The marker file lives at `<registry-root>/spio-registry.json`.

Current required JSON shape:

```json
{
  "kind": "filesystem-local",
  "schema_version": 1
}
```

Notes:

- the marker value `filesystem-local` is retained for compatibility with the first published layout
- the same marker remains valid when the layout is hosted remotely over HTTP(S)

## 4. Version Entry Contract

Each package version entry lives at:

`index/<namespace>/<name>/<version>.json`

Current required fields:

- `schema_version`
- `package`
- `version`
- `sha256`
- `size_bytes`
- `blob_path`
- `published_at`
- `dependencies`
- `dev_dependencies`

Example shape:

```json
{
  "schema_version": 1,
  "package": "acme/feed",
  "version": "1.2.0",
  "sha256": "<64-lowercase-hex>",
  "size_bytes": 1234,
  "blob_path": "blobs/sha256/ab/cd/abcdef....tar",
  "published_at": "2026-04-12T00:00:00Z",
  "dependencies": [
    {
      "alias": "util",
      "package": "acme/util",
      "version": "0.2.0",
      "registry": "https://packages.example.test"
    }
  ],
  "dev_dependencies": []
}
```

Dependency metadata rules:

- entries are informational metadata for published dependency intent
- current published dependencies must be registry-addressable
- each dependency record uses:
  - `alias`
  - `package`
  - `version`
  - `registry`

## 5. Manifest Rules for Registry Dependencies

Registry dependencies in `spio.toml` use inline tables:

```toml
[dependencies]
feed = { package = "acme/feed", version = "1.2.0", registry = "https://packages.example.test" }
```

Rules:

- `package` is required and must match `namespace/name`
- `version` is required and must be strict semver `x.y.z`
- `registry` is required
- `registry` must use `file://`, `http://`, or `https://`
- registry dependencies are now part of the active native contract

## 6. Client Fetch and Materialization

For a registry dependency, the client must:

1. normalize the registry root URL
2. validate the registry marker
3. load the version entry for `package@version`
4. fetch or reuse the blob identified by `sha256`
5. verify the blob digest before use
6. extract the blob into a local checkout under `SPIO_HOME`
7. resolve `spio.toml` from the extracted snapshot

Supported extracted archive shapes:

- `spio.toml` directly at the extraction root
- a single top-level directory containing `spio.toml`

This accommodates deterministic package archives that are rooted at `<short-name>-<version>/`.

## 7. Client Cache Layout

Registry client cache state lives under `SPIO_HOME/registry/`:

- entry and marker cache:
  - `registry/index/`
- blob cache:
  - `registry/blobs/`
- extracted snapshots:
  - `registry/checkouts/`

Behavior rules:

- blobs are keyed by `sha256`
- cached blobs must be re-verified before use
- offline mode may use only cached marker metadata, cached entries, cached blobs, or local/file registries

## 8. Lockfile Rules

Registry lock entries use:

- `source-kind = "registry"`
- `registry = "<normalized-url>"`
- `sha256 = "<64-lowercase-hex>"`

Canonical package id form:

- `registry:<package-name>@<package-version>#<sha256>`

Current resolver discipline:

- `single-version-v1` still allows only one effective version and one effective source fingerprint per package name
- registry source fingerprints include the normalized registry root plus immutable blob digest

## 9. Publish Rules

Published packages must satisfy:

- `package.publish = true`
- dependencies, when present, must themselves be registry-addressable
- `path` and `git` dependencies are not publishable into the shared registry contract

Writer targets may use:

- a plain local filesystem path
- `file://<registry-root>`
- `http://<registry-root>`
- `https://<registry-root>`

Current write behavior:

- local paths and `file://...` publish directly into the filesystem registry root
- `http://...` and `https://...` publish through anonymous HTTP `PUT`
- remote writers probe existing marker/blob/entry objects through HTTP `GET` or `HEAD`
- remote writers upload:
  - marker: `PUT <registry-root>/spio-registry.json`
  - immutable blob: `PUT <registry-root>/blobs/sha256/<xx>/<yy>/<sha256>.tar`
  - version entry: `PUT <registry-root>/index/<namespace>/<name>/<version>.json`

Remote origin requirements:

- must preserve immutable blob and version-entry paths
- should reject overwrite attempts for existing immutable objects with `409 Conflict`
- must serve the same paths back over ordinary `GET`

Current write status:

- remote write transport is active for anonymous HTTP `PUT`
- auth and account policy are still deferred

## 10. Trust Policy

Current trust model:

- content identity is anchored by `sha256`
- clients verify downloaded blobs against the version entry before extraction
- version-entry and blob paths are treated as immutable object identities

Current limitations:

- there are no signatures
- there is no delegated trust root beyond the selected registry origin
- remote overwrite prevention relies on origin behavior plus client-side preflight, not on cryptographic server-side enforcement
- the current remote publish path assumes a single-writer or otherwise immutable-origin discipline

## 11. Cloud Deployment Guidance

The registry can be deployed as a static repository:

- object storage bucket
- CDN-backed origin
- read-only web root on an HTTP server

Operationally, the server side only needs to preserve:

- immutable blob paths
- immutable version-entry paths
- registry marker availability

If remote publication is required in the same deployment, the write-facing origin or upload proxy must additionally provide:

- HTTP `PUT` for marker/blob/entry paths
- `GET` and `HEAD` for existence checks and client reads
- overwrite rejection for immutable paths

Write/read separation guidance:

- package publication may target an internal write root that is not exposed to clients
- client fetches may target a separate read root or CDN
- promotion or replication from write root to read root is operational infrastructure, not a client-visible registry protocol change

The client side owns:

- URL fetching
- checksum verification
- local cache and extraction
- resolver integration
