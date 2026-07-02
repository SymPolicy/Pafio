# Registry / Publish Runbook

**Purpose:** Provide the daily-work entrypoint for `pafio` registry and publish maintainers covering registry client docs, offline package expectations, publish/fetch flows, and promotion tooling.

**Last updated:** 2026-05-02

## Mission

Own registry transport, offline package expectations, and package publish/fetch
client behavior without redefining core workflow semantics, external compiler
contracts, or `pafio` server control-plane behavior.

## Owned Surface

1. `docs/registry/`
2. `scripts/registry-promote.py`
3. `scripts/registry-server-gate.py`
4. compatibility references to registry server gates until platform gates replace them
5. local offline package client expectations that interact with registry sources
6. public registry trust descriptor import and read-root pin validation from the client side

## Daily Workflow

1. Keep registry transport, promotion behavior, registry-v2 static protocol, and package-manager client contract docs aligned in `docs/registry/`.
2. Link server-side control-plane, mirror sync, and global package distribution behavior to `pafio` instead of adding new service implementation rules here.
3. Keep offline package and local import/export rules client-owned; they must not depend on mirror availability.
4. Keep acceptance commands discoverable from the verification matrix and checkpoint health docs.
5. Coordinate with Core / Workflow when publish/fetch behavior changes user-facing workflow outcomes.
6. Keep `RegistryHttpTransport` as a transport-only strategy boundary. Registry semantics stay in `RemotePublish` / publish domain code, and external process execution stays in `PafioCore::Process`.
7. Keep registry control-plane references on native JSON contract and example packs; do not reintroduce generated API-description artifacts or lint gates.
8. Keep the minimum measurable registry-management checklist visible in registry docs: publish, verify, mirror handoff, offline behavior, cache reuse, and security boundary.
9. For HTTP read-root validation, import the platform descriptor through
   `pafio registry trust import` or `registry-server-gate.py --fetch-trust-descriptor`
   before claiming fetch coverage.

## Change Classes

1. Small: local registry test or runbook cleanup.
2. Medium: registry layout, publish semantics, native JSON client compatibility contract, or promotion flow updates.
3. High: split-origin, auth-adjacent, hosted deployment model, publish authorization changes, or service-side control-plane handoff changes.

## Required Gates

```bash
./scripts/checkpoint-health.sh
python3 ./tests/interop/registry-control-plane-contract-gate.py
python3 ./tests/interop/native-contract-source-gate.py
./scripts/delivery-gate.sh --skip-audit --skip-health
```

## Cross-Team Dependencies

1. Core / Workflow reviews changes that alter user-facing publish/fetch commands.
2. Docs / Delivery reviews changes to gate docs or delivery entrypoints.

## Handoff / Recovery

Record the registry mode affected, the acceptance command that still fails, whether local or remote storage assumptions changed, and whether the next fix belongs in `pafio` client code or `pafio` service code.
