# Pafio Docs

**Purpose:** Route maintainers to the current Pafio product, contract, planning, and operational sources of truth.

**Last updated:** 2026-10-11

## Reading Order

1. [Pafio CLI Contract](./governance/Pafio-CLI-Contract.md)
2. [Pafio Product and Ecosystem Boundary](./adr/ADR-0008-pafio-product-and-ecosystem-boundary.md)
3. [Manifest and Lock Conventions](./governance/Pafio-Manifest-and-Lock-Conventions.md)
4. [Local Offline Package Contract](./governance/Pafio-Local-Offline-Package-Contract.md)
5. [Registry Client Contract](./registry/Pafio-Registry-Client-Contract.md)
6. [Delivery Workspace](./plan/README.md)
7. [Build and Development Environment](./BUILD-AND-DEV-ENV.md)

## Ownership

- `governance/` owns stable Pafio policy and public command contracts.
- `adr/` owns accepted architecture decisions.
- `plans/` owns current delivery checkpoints and acceptance.
- `plan/` preserves completed delivery workspaces and independent capability records.
- `registry/` owns Pafio client behavior only.
- `external/for-styio/` documents the public compiler handoff.
- `operations/` owns repository-local artifact and delivery procedures.
- `teams/` owns review routing for the current product boundary.

Styio owns compilation contracts. Styio Platform owns registry services, hosted
workspaces, cloud jobs, and workers. This repository does not duplicate either
owner's server or compiler implementation.

Generated `INDEX.md` files are inventories, not normative sources.

## Maintained Documentation Gates

Run the repository-owned tools from the Pafio root:

```bash
python3 scripts/docs-index.py --check
python3 scripts/docs-lifecycle.py validate
python3 scripts/docs-audit.py
```

The index and lifecycle checks establish deterministic collection wiring; the
audit composes them with required metadata and team-owner routing.
