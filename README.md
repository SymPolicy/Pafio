# Pafio

**Purpose:** Define Pafio as the Styio ecosystem's package manager and local project workflow entry.

**Last updated:** 2026-07-30

Pafio is the Cargo/CMake-style terminal entry for Styio projects. It owns project
creation, manifests, dependency resolution, deterministic lock and vendor state,
build orchestration, project metadata, packaging, and publishing.

```text
pafio new|init|doctor|metadata|add|remove|sync|tree
pafio check|build|run|test
pafio vendor|pack|publish
pafio registry trust
pafio machine-info
```

`check`, `build`, `run`, and `test` synchronize dependencies before using the
externally installed `styio` compiler. Compiler discovery is
`--styio-bin` → `PAFIO_STYIO_BIN` → `styio` on `PATH`. Pafio never installs,
updates, switches, pins, builds, or caches Styio.

Pafio remains useful offline when the selected lock and required sources are
already available. `--locked` forbids lock mutation, `--offline` forbids network
access, and `--frozen` enables both restrictions.

## Ownership

- Pafio: `pafio.toml`, `pafio.lock`, `.pafio/`, dependency and project workflows,
  metadata v1, vendor, pack, and publish client behavior.
- Styio: compile-plan consumption, compilation, diagnostics, receipts, runtime
  events, and compiler machine information.
- Styio Platform: registry/control plane, hosted workspace, cloud jobs, and workers.
- Vityo: adapters over Pafio, Styio, and Platform machine contracts.

The completed trusted dependency kernel is an internal Pafio subsystem, not the
whole product boundary. Legacy names, commands, data, and protocol values are not
migrated or aliased.

## Developer entry

The implementation is native C++20/CMake. Start with
[docs/BUILD-AND-DEV-ENV.md](docs/BUILD-AND-DEV-ENV.md) and the
[CLI contract](docs/governance/Pafio-CLI-Contract.md); the active Better Plan
workspace is internal coordination state outside the published tree.
