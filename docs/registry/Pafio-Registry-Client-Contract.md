# Pafio Registry Client Contract

**Purpose:** Define the client-side rules for consuming packages from a `pafio` registry without mixing them with server upload or deployment concerns.

**Last updated:** 2026-04-24

## 1. Scope

This document owns:

- supported registry root schemes for clients
- fetch/materialization behavior
- local cache behavior
- offline and lock-related client expectations
- the public read-side security hook boundary only at the level of interface

Shared `v2` static read-plane layout remains owned by [./Pafio-Registry-V2-Protocol.md](./Pafio-Registry-V2-Protocol.md).

## 2. Supported Registry Roots

Registry clients may consume packages from:

- `file://<registry-root>`
- `http://<registry-root>`
- `https://<registry-root>`

The client consumes the same `v2` config, namespace-targets, append-only index, and source-artifact paths regardless of transport.
HTTP roots may be platform-hosted mirrors or direct registry read origins, but
the client treats them as read roots only. Mirror freshness, replay, and
regional routing are platform responsibilities; local cache and offline
behavior remain valid when no mirror is reachable.

Any environment-specific trust policy, credential injection, or allowlist enforcement belongs behind the private security boundary documented in [../security/Pafio-Private-Security-Module-Contract.md](../security/Pafio-Private-Security-Module-Contract.md).

## 3. Client Fetch Flow

For `package@version`, the client must:

1. normalize the registry root
2. load and validate `<registry-root>/config.json`
3. load `trust/targets/<namespace>.json` and resolve the package `index_path`
4. load `index/<namespace>/<name>.jsonl`
5. select the exact record for `package@version`
6. read `source_artifact.sha256`, `size_bytes`, and `path`
7. fetch or reuse the immutable source artifact
8. verify the artifact digest against the selected index record
9. extract the snapshot locally and resolve `pafio.toml`

The client must not trust the transport alone; the source-artifact digest check is mandatory before use.

## 4. Local Client State

Registry client state lives under `PAFIO_HOME/registry/`:

- config / targets / index cache: `registry/index/`
- immutable artifact cache: `registry/blobs/`
- extracted snapshots: `registry/checkouts/`

Behavior rules:

- artifacts are keyed by `sha256`
- cached artifacts must be re-verified before reuse
- offline mode may use only cached metadata, cached blobs, extracted snapshots, vendored state, or `file://` registries
- cached state must not silently relax digest checks, even when the package was originally fetched from a trusted platform mirror

## 5. Client Failure Semantics

Client-side fetch must fail when:

- the registry `config.json` is missing or invalid
- namespace targets metadata is missing the requested package/version
- the append-only package index is missing or invalid
- the source artifact cannot be fetched
- the source-artifact digest does not match the selected record
- offline mode requires a network fetch

## 6. Non-Goals

This document does not define:

- server upload/auth behavior
- registry deployment topology
- account policy
- private registry allowlists or credentials
- full trust-metadata signature verification policy
