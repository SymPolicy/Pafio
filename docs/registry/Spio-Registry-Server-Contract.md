# Spio Registry Server Contract

**Purpose:** Define the server-side write and origin behavior for registry publication without mixing it with client fetch/cache rules.

**Last updated:** 2026-04-12

## 1. Scope

This document owns:

- server-facing upload behavior
- origin method requirements for publish
- write-root versus read-root separation rules
- the public write-side security hook boundary only at the level of interface
- immutable object expectations for marker, blob, and version-entry paths

Shared repository layout remains owned by [../governance/Spio-Registry-Repository-Contract.md](../governance/Spio-Registry-Repository-Contract.md).

## 2. Accepted Publish Targets

The current native core supports these writer targets:

- local filesystem path
- `file://<registry-root>`
- `http://<registry-root>`
- `https://<registry-root>`

Writers must emit the same canonical objects regardless of target:

- `spio-registry.json`
- `blobs/sha256/<xx>/<yy>/<sha256>.tar`
- `index/<namespace>/<name>/<version>.json`

## 3. Remote Origin Requirements

If the publish target is `http://` or `https://`, the origin used for writes must support:

- `GET`
- `HEAD`
- `PUT`

Expected behavior:

- `GET` serves the marker, entries, and blobs
- `HEAD` lets the client check whether immutable objects already exist
- `PUT` creates missing immutable objects at canonical paths

The public command surface reserves these remote write-side security hooks:

- `spio publish --registry-profile <name>`
- `spio publish --registry-policy-file <path>`
- `spio publish --registry-header <name:value>`

Rules:

- the actual auth/account implementation must live behind the private boundary defined in [../security/Spio-Private-Security-Module-Contract.md](../security/Spio-Private-Security-Module-Contract.md)
- the tracked open-source core rejects these hooks unless a private security module is linked from `src-private/`
- when a private module accepts them, they apply only to publish-side `GET`, `HEAD`, and `PUT` against the write origin
- they must not change client-side fetch semantics
- public JSON output must stay redacted:
  - do emit `registry_security_provider`
  - do emit `registry_write_security_mode`
  - do emit `registry_header_count`
  - may emit `registry_profile`
  - do not emit raw header values
  - do not emit resolved private policy file paths

## 4. Immutable Object Rules

The server side must preserve these semantics:

- blob paths are content-addressed and immutable
- version-entry paths are immutable per `package@version`
- overwriting an existing immutable object should be rejected with `409 Conflict`
- duplicate blob uploads may be treated as harmless reuse when the path already exists

## 5. Current Server Trust Assumption

The current remote publish path assumes:

- a trusted internal origin or upload proxy
- single-writer or otherwise immutable-origin discipline
- either anonymous upload or a private security module outside the tracked tree

This is sufficient for the current internal/shared-repository phase, but it is not a multi-tenant public registry design.

## 6. Split Write and Read Origins

The recommended current deployment shape is:

- internal write root or upload proxy that accepts publication traffic
- separate read root or CDN that serves only immutable objects over `GET` and `HEAD`

Rules:

- clients do not need to see the write root
- the read root must expose the same canonical marker, blob, and version-entry paths after promotion or replication
- promotion from write root to read root is out-of-band from `spio publish`
- the current repository-level helper for that promotion is `scripts/registry-promote.py`, which operates on local backing-store roots

## 7. Non-Goals

This document does not define:

- client-side fetch/cache behavior
- deployment topology
- signatures
- transparency log design
- account and permission systems
