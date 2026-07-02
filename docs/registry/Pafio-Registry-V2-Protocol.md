# Pafio Registry V2 Protocol

**Purpose:** Define the production-oriented `pafio` package registry `v2` static read-plane protocol so package clients, mirrors, and backend services can develop independently against the same immutable distribution surface.

**Last updated:** 2026-04-21

## 1. Positioning

`pafio` registry `v2` is the active package distribution contract.

`v2` keeps the static-hosting strengths required by language package ecosystems:

- local filesystem roots
- object storage
- CDN-backed read origins
- offline mirroring

Its production model is:

- public clients consume only immutable static objects
- publication is no longer anonymous `PUT` to the read root
- trust is anchored by signed metadata plus a transparency log
- package state is append-only at the package-index layer

All active clients, CLI publish paths, control-plane routes, and verification gates must target `v2`.

## 2. Read-Plane Object Families

The registry root exposes these public objects:

- `config.json`
- `trust/root.json`
- `trust/timestamp.json`
- `trust/snapshot.json`
- `trust/targets/<namespace>.json`
- `index/<namespace>/<name>.jsonl`
- `artifacts/source/sha256/<xx>/<yy>/<sha256>.pafio.src.tar`
- `artifacts/binary/<target-triple>/sha256/<xx>/<yy>/<sha256>.pafio.bin.tar.zst`
- `log/checkpoint.json`
- `log/leaves/<sequence>.json`

Rules:

- object paths are immutable once published
- object paths are canonical POSIX-relative paths under the registry root; absolute paths, empty segments, `.`, `..`, and backslash separators are invalid
- package index files are append-only JSON Lines streams
- artifacts are addressed by digest, not by display name
- read clients do not call a mutation RPC to install packages

## 3. Dual-Plane Model

`v2` is explicitly split into two planes.

Static read plane:

- package discovery
- signed metadata refresh
- package index download
- source or binary artifact download
- log checkpoint and leaf verification

Publish control plane:

- publisher authentication
- namespace ownership enforcement
- upload admission
- metadata signing
- transparency-log append
- yank and deprecate actions

The static read plane must remain valid on plain object storage or a static HTTP server. It must not depend on dynamic publish APIs to be consumable.

## 4. Package Identity

Canonical package identity is:

- `package = namespace/name`
- `version = semantic release version`
- `release_revision = append-only registry revision for the release state`

Rules:

- package display identity does not depend on artifact filenames
- release revisions may grow without mutating old artifacts
- yanked and deprecated states are metadata states, not artifact rewrites

## 5. Package Index Shape

Each package owns one sparse index file:

- `index/<namespace>/<name>.jsonl`

Each line is one immutable release record. Required high-level fields:

- package identity and publish metadata
- one source artifact descriptor
- zero or more binary artifact descriptors
- runtime and development dependency intent
- feature declarations
- manifest digest
- original metadata digest

Design consequences:

- the registry does not store solver outputs
- release records stay immutable
- yanks or deprecations append new state records instead of rewriting history

## 6. Artifact Model

Every release must expose a source artifact.

Source artifact responsibilities:

- portable fallback path
- long-term reproducibility anchor
- manifest extraction source

Binary artifacts are optional acceleration objects. Their selection keys include:

- `target_triple`
- `abi`
- `libc`
- `build_profile`
- `features_fingerprint`

Clients may prefer a matching binary artifact, but the source artifact remains authoritative and must be lockfile-addressable.

## 7. Trust Metadata

`v2` uses TUF-shaped signed metadata envelopes:

- root
- timestamp
- snapshot
- namespace targets
- transparency checkpoint

Envelope shape:

```json
{
  "signed": { "...": "..." },
  "signatures": [
    {
      "keyid": "<sha256 of role public key>",
      "sig": "<base64 signature>"
    }
  ]
}
```

Current tracked implementation uses:

- Ed25519 role keys
- canonical JSON serialization
- one-key-per-role threshold policy
- namespace-target metadata signed by the shared `targets` role

## 8. Transparency Log

Every published release emits one leaf object under `log/leaves/`.

Current tracked open-source model uses an append-only signed hash chain:

- each leaf hash is the SHA-256 of the canonical leaf JSON
- the checkpoint root hash folds leaf hashes sequentially
- `log/checkpoint.json` signs `tree_size` and `root_hash`

This is intentionally simpler than a full Merkle tile tree, but it already gives the static root:

- append-only release sequencing
- checkpoint pinning
- replay/tamper detection when the client persists checkpoints

## 9. Client Verification Flow

Clients must:

1. load `config.json`
2. load and verify `trust/root.json`
3. load and verify `trust/timestamp.json`
4. load and verify `trust/snapshot.json`
5. verify snapshot file digests
6. load and verify the namespace targets metadata
7. load the package index JSONL
8. verify the source or binary artifact digest before use
9. verify the transparency checkpoint against the published leaves

Offline mode is allowed only against previously trusted metadata and cached immutable objects.

The tracked verifier currently accepts:

- local directory roots
- `file://` roots
- `http://` and `https://` static roots

This keeps the read plane mirrorable and testable without coupling it to a specific server runtime.

## 10. Machine Contract Pack

Machine-readable `v2` contracts live under:

- `contracts/registry-v2/v1/`

The pack currently includes:

- contract overview
- example objects
- JSON Schemas for config, trust envelopes, package index records, and transparency-log objects

## 11. Current Limits

The tracked public tree now owns the `v2` static read plane, local publish tooling, control-plane contract gates, and the native client read path.

Not yet implemented here:

- authenticated public publish service
- multi-key threshold enforcement
- revocation and key rotation workflows
- Merkle tile transparency storage
- binary artifact build/publish pipeline

Those concerns are isolated behind the publish-control-plane boundary so the static protocol can still be stabilized now.
