# Spio Registry Deployment Baseline

**Purpose:** Provide the current pragmatic deployment baseline for teams that operate a shared `spio` registry and only need safe internal distribution with checksum verification.

**Last updated:** 2026-04-12

## 1. Baseline Goal

The current baseline is:

- package downloads must come from a trusted registry origin
- downloaded blobs must pass `sha256` verification before use
- server-side object paths must be immutable

This baseline explicitly does not require signatures or transparency logs.

## 2. Recommended Topology

Recommended near-term deployment:

1. internal upload origin that accepts `PUT`
2. read-only download origin or CDN for `GET` and `HEAD`
3. promotion or replication from the upload backing store into the read-side serving root
4. object storage or filesystem backing store with immutable/versioned objects

This keeps write risk and read traffic separated without inventing a new registry protocol.

## 3. Promotion Model

The current repository-level promotion helper is:

```text
./scripts/registry-promote.py --source-root <write-root> --dest-root <read-root> [--package <namespace/name>] [--version <x.y.z>] [--json]
```

Use it when:

- the upload root is a writable filesystem or mounted object-store view
- the read root is a separate static-serving directory or staging area for CDN sync

Do not treat this helper as a generic remote replication protocol. It is a practical local promotion tool for the current internal/shared-registry phase.

## 4. Minimum Server Controls

- use `https` for remote registry roots in production
- reject overwrite attempts for existing immutable objects
- preserve audit logs for package publication attempts
- restrict upload access at the network or proxy layer if auth is still deferred in `spio`
- if write-origin auth is needed, provision it through the private security boundary and deployment-owned config instead of committing it under the tracked project tree
- back up marker, index, and blobs together

## 5. Minimum Client Controls

- keep blob `sha256` verification mandatory
- prefer locked workflows for CI and release paths
- use `--offline` or vendored state where deterministic offline builds matter
- pin registry dependencies in `spio.lock`

## 6. When This Baseline Stops Being Enough

You should move beyond this baseline when any of these become true:

- multiple independent publishers need strong publisher identity
- you do not fully trust the registry origin
- you need tamper-evident history
- you need public multi-tenant package distribution

That is the point where signatures, stronger policy, or transparency-style append-only guarantees become justified.

## 7. Operational Validation

The executable deployment validation path lives in [../operations/Spio-Registry-Server-Runbook.md](../operations/Spio-Registry-Server-Runbook.md).
