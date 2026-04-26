# Spio Registry Deployment Baseline

**Purpose:** Provide the current pragmatic deployment baseline for teams that operate a shared `spio` registry `v2` root and need safe internal distribution before a hosted publish service is deployed.

**Last updated:** 2026-04-21

## 1. Baseline Goal

The current baseline is:

- package downloads must come from a trusted registry origin
- downloaded source artifacts must pass `sha256` verification before use
- the read root must expose the `v2` object families: `config/`, `trust/`, `index/`, `artifacts/`, and `log/`
- artifact paths must be immutable
- metadata and package indexes must be updated only by the publish worker or a compatible control-plane service

The tracked baseline already writes signed metadata and a hash-chain transparency checkpoint. It does not yet require a hosted multi-tenant publish service.

## 2. Recommended Topology

Recommended deployment:

1. internal publish worker or control-plane service that writes a staging `v2` root
2. read-only download origin or CDN for static `GET` and `HEAD`
3. promotion or replication from the staging root into the read-side serving root
4. object storage or filesystem backing store with immutable/versioned artifact objects

This keeps write risk and read traffic separated while preserving the static read-plane protocol.

## 3. Promotion Model

The current repository-level promotion helper is:

```text
./scripts/registry-promote.py --source-root <write-root> --dest-root <read-root> [--package <namespace/name>] [--version <x.y.z>] [--json]
```

Use it when:

- the upload root is a writable filesystem or mounted object-store view
- the read root is a separate static-serving directory or staging area for CDN sync

Do not treat this helper as a generic remote replication protocol. It is a practical local promotion tool for moving a complete, valid `v2` static root from a staging view to a read-serving view.

## 4. Minimum Server Controls

- use `https` for remote registry roots in production
- reject overwrite attempts for existing artifact and log objects
- preserve audit logs for package publication attempts
- restrict publish-worker or control-plane access at the network, storage, or service layer
- if write-origin auth is needed, provision it through the private security boundary and deployment-owned config instead of committing it under the tracked project tree
- back up `config/`, `trust/`, `index/`, `artifacts/`, and `log/` together

## 5. Minimum Client Controls

- keep artifact `sha256` verification mandatory
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
