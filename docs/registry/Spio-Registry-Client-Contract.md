# Spio Registry Client Contract

**Purpose:** Define the client-side rules for consuming packages from a `spio` registry without mixing them with server upload or deployment concerns.

**Last updated:** 2026-04-12

## 1. Scope

This document owns:

- supported registry root schemes for clients
- fetch/materialization behavior
- local cache behavior
- offline and lock-related client expectations
- the public read-side security hook boundary only at the level of interface

Shared repository layout remains owned by [../governance/Spio-Registry-Repository-Contract.md](../governance/Spio-Registry-Repository-Contract.md).

## 2. Supported Registry Roots

Registry clients may consume packages from:

- `file://<registry-root>`
- `http://<registry-root>`
- `https://<registry-root>`

The client consumes the same marker, version-entry, and blob paths regardless of transport.

Any environment-specific trust policy, credential injection, or allowlist enforcement belongs behind the private security boundary documented in [../security/Spio-Private-Security-Module-Contract.md](../security/Spio-Private-Security-Module-Contract.md).

## 3. Client Fetch Flow

For `package@version`, the client must:

1. normalize the registry root
2. load and validate `<registry-root>/spio-registry.json`
3. load `index/<namespace>/<name>/<version>.json`
4. read `sha256`, `size_bytes`, and `blob_path`
5. fetch or reuse the immutable blob
6. verify the blob digest against the version entry
7. extract the snapshot locally and resolve `spio.toml`

The client must not trust the transport alone; the blob digest check is mandatory before use.

## 4. Local Client State

Registry client state lives under `SPIO_HOME/registry/`:

- marker and entry cache: `registry/index/`
- immutable blob cache: `registry/blobs/`
- extracted snapshots: `registry/checkouts/`

Behavior rules:

- blobs are keyed by `sha256`
- cached blobs must be re-verified before reuse
- offline mode may use only cached metadata, cached blobs, extracted snapshots, vendored state, or `file://` registries

## 5. Client Failure Semantics

Client-side fetch must fail when:

- the marker is missing or invalid
- the version entry is missing or invalid
- the blob cannot be fetched
- the blob digest does not match the recorded `sha256`
- offline mode requires a network fetch

## 6. Non-Goals

This document does not define:

- server upload/auth behavior
- registry deployment topology
- account policy
- private registry allowlists or credentials
- signatures or transparency policy
