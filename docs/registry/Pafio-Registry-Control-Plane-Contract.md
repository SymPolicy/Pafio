# Pafio Registry Control-Plane Contract

**Purpose:** Freeze the HTTP control-plane contract used by backend services and automation to operate a `pafio` registry `v2` root independently of the static read-plane contract.

**Last updated:** 2026-04-24

## Source Of Truth

The authoritative machine contract lives under:

- [`../../contracts/registry-control-plane/v1/registry-control-plane.contract.json`](../../contracts/registry-control-plane/v1/registry-control-plane.contract.json)
- [`../../contracts/registry-control-plane/v1/registry-control-plane.examples.json`](../../contracts/registry-control-plane/v1/registry-control-plane.examples.json)

Human-readable docs explain those files. They do not replace them.

## Scope

`v1` currently freezes these control-plane operations:

1. `GET /api/pafio-registry-control/v1/status`
2. `POST /api/pafio-registry-control/v1/publish`
3. `POST /api/pafio-registry-control/v1/verify`

This contract owns:

- control-plane status and readiness discovery
- publish requests that commit source packages into a `v2` root
- verification requests for the static read plane

This contract does not own:

- the immutable read-plane object layout itself
- browser-facing frontend deployment flows
- future hosted auth and tenancy policy

Those remain in:

- [`Pafio-Registry-V2-Protocol.md`](./Pafio-Registry-V2-Protocol.md)
- [`Pafio-Registry-V2-Publish-Control-Plane.md`](./Pafio-Registry-V2-Publish-Control-Plane.md)

## Repository Boundary

`pafio` is the hosted registry and mirror service owner. This
repository keeps the same native JSON contract package so the local
package-manager client, publish automation, and offline-capable workflows can
validate compatibility without owning the hosted service implementation.

The shared contract is limited to status, publish, and verify envelopes.
Mirror freshness/replay belongs to platform docs; offline cache behavior
belongs to [Pafio-Registry-Client-Contract.md](./Pafio-Registry-Client-Contract.md)
and [../governance/Pafio-Local-Offline-Package-Contract.md](../governance/Pafio-Local-Offline-Package-Contract.md).

## Current Implementation Status

The tracked open-source repository now ships a local HTTP implementation:

- [`../../scripts/registry-v2-control-plane-server.py`](../../scripts/registry-v2-control-plane-server.py)

That server binds:

- one local registry root
- one role-key directory
- one `pafio` binary for dry-run publish preparation

It is the current executable reference for the contract package. It does not yet represent the final hosted multi-tenant service shape.

## Gate Rule

Registry control-plane compatibility is validated from the native JSON contract
and example package. Any route or envelope change must update both JSON files,
this governance page, and the owning runbook before claiming the service
boundary is stable. Generated third-party API-description artifacts are not
accepted as contract source.
