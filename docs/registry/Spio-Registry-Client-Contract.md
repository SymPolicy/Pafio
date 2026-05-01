# Spio Registry Client Contract

**Purpose:** Define the client-side rules for consuming packages from a `spio` registry without mixing them with server upload or deployment concerns.

**Last updated:** 2026-05-02

## 1. Scope

This document owns:

- supported registry root schemes for clients
- fetch/materialization behavior
- local cache behavior
- offline and lock-related client expectations
- the public read-side security hook boundary only at the level of interface

Shared `v2` static read-plane layout remains owned by [./Spio-Registry-V2-Protocol.md](./Spio-Registry-V2-Protocol.md).

## 2. Supported Registry Roots

Registry clients may consume packages from:

- `file://<registry-root>`
- `http://<registry-root>`
- `https://<registry-root>`

The client consumes the same `v2` config, namespace-targets, append-only index, and source-artifact paths regardless of transport.
HTTP roots may be platform-hosted mirrors or direct registry read origins, but
the client treats them as read roots only. Before consuming a remote HTTP root,
the client must import a platform registry descriptor with
`spio registry trust import <descriptor-url|descriptor-file>`. The descriptor
pins the expected `trust/root.json` SHA-256 outside the registry read root.
Mirror freshness, replay, and regional routing are platform responsibilities;
local cache and offline behavior remain valid when no mirror is reachable.

Any environment-specific trust policy, credential injection, or allowlist enforcement belongs behind the private security boundary documented in [../security/Spio-Private-Security-Module-Contract.md](../security/Spio-Private-Security-Module-Contract.md).

## 3. Client Fetch Flow

For `package@version`, the client must:

1. normalize the registry root
2. load and validate `<registry-root>/config.json`
3. for remote HTTP roots, resolve a previously imported trust descriptor and
   verify `trust/root.json` against the descriptor `root_sha256`
4. load `trust/targets/<namespace>.json` and resolve the package `index_path`
5. load `index/<namespace>/<name>.jsonl`
6. select the exact record for `package@version`
7. read `source_artifact.sha256`, `size_bytes`, and `path`
8. fetch or reuse the immutable source artifact
9. verify the artifact digest against the selected index record
10. extract the snapshot locally and resolve `spio.toml`

The client must not trust the transport alone; the source-artifact digest check is mandatory before use.

## 4. Local Client State

Registry client state lives under `SPIO_HOME/registry/`:

- config / targets / index cache: `registry/index/`
- immutable artifact cache: `registry/blobs/`
- extracted snapshots: `registry/checkouts/`
- imported remote registry trust pins: `registry/trust/registry-trust.json`

Behavior rules:

- artifacts are keyed by `sha256`
- cached artifacts must be re-verified before reuse
- offline mode may use only cached metadata, cached blobs, extracted snapshots, vendored state, or `file://` registries
- cached state must not silently relax digest checks, even when the package was originally fetched from a trusted platform mirror
- remote HTTP roots must fail closed when no matching imported descriptor pin
  exists or when the fetched `trust/root.json` digest differs from that pin

## 5. Client Failure Semantics

Client-side fetch must fail when:

- the registry `config.json` is missing or invalid
- namespace targets metadata is missing the requested package/version
- the append-only package index is missing or invalid
- the source artifact cannot be fetched
- the source-artifact digest does not match the selected record
- a remote HTTP registry has no imported descriptor pin
- the remote `trust/root.json` digest does not match the imported descriptor
- offline mode requires a network fetch

## 6. Non-Goals

This document does not define:

- server upload/auth behavior
- registry deployment topology
- account policy
- private registry allowlists or credentials
- full trust-metadata signature verification policy
