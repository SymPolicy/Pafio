# Pafio

**Pafio is the local-first package manager and project workflow client for Styio.**
Part of the [Styio](https://styio.io) ecosystem.

[![License](https://img.shields.io/github/license/SymPolicy/Pafio?style=flat-square)](LICENSE)

[Build guide](docs/BUILD-AND-DEV-ENV.md) | [Registry contracts](docs/registry/) | [Governance](docs/governance/)

---

Pafio manages package manifests, lockfiles, dependency resolution, cache layout,
local package import/export, package build orchestration, and project-level
commands. It is designed to work offline when packages are available locally and
to use Pafio only as an optional service foundation.

## Quick Start

Bootstrap the development environment and build:

```bash
./scripts/bootstrap-dev-env.sh
cmake -S . -B build-codex
cmake --build build-codex
```

Select the project toolchain mode:

```bash
./scripts/pafio use binary        # published compiler via versioned contract
./scripts/pafio use build         # source-build via official Styio checkout
./scripts/pafio set channel as stable
./scripts/pafio set channel as nightly
```

Common workflow commands:

```bash
./scripts/pafio sync
./scripts/pafio build minimal
./scripts/pafio project-graph --json
./scripts/pafio tool status --json
```

`./scripts/pafio` is the repository-local convenience wrapper. It ensures the
native binary exists under `./build-codex/bin/pafio` and then forwards arguments.

## Scope

- Manages package manifests, lockfiles, dependency resolution, cache layout,
  local package import/export, and project-level commands.
- Does not parse Styio source semantics on its own.
- Does not own hosted compile services, registry server control planes, or
  cloud-service backends; those live in `styio-cloud`.
- Must remain useful without a platform connection when dependencies are
  satisfied by workspace, path, vendor, cache, or explicitly imported offline
  packages.

### Toolchain Modes

| Mode | Description |
|---|---|
| `binary` | Published compiler path through a versioned machine contract and a process boundary. |
| `build` | Source-build path through an official `styio` source checkout and local compiler build cache. |

## Architecture

### Product Surface Split

- `frontend/console/` — repo-hosted human control console page.
- `src/` — native package-manager core and compatibility renderer.
- `docs/registry/` and `docs/governance/` — SSOT for package-manager client
  contracts.
- The native CLI is the machine/admin surface; it is not the user-facing
  control console.

The normative split is defined in
[Pafio-Control-Console-And-Service-Split.md](docs/governance/Pafio-Control-Console-And-Service-Split.md).

### Native Target Split

The codebase composes from internal static libraries:

- `pafio_foundation`, `pafio_manifest`, `pafio_resolution`
- `pafio_toolchain_service`, `pafio_package_service`, `pafio_project_service`
- CLI surface: `pafio_cli_support`, `pafio_cli_commands`, `pafio_cli_shell`

The `pafio` executable links only the shell target. Source-level details live in
[src/README.md](src/README.md) and
[Pafio-Native-Target-Split.md](docs/planning/Pafio-Native-Target-Split.md).

## Independence Rules

- Pafio must not include or link against Styio implementation headers or
  libraries.
- Pafio may depend on a published external `styio` executable only through the
  documented binary-mode discovery path (`--styio-bin` or `PAFIO_STYIO_BIN`).
- Source-build mode fetches the official Styio source tree from
  `https://github.com/SymPolicy/Styio.git` using `stable` and `nightly` as
  channel roots.
- `pafio/contracts/` is the source of truth for package-manager-side machine
  contracts. Hosted workspace and cloud-service contracts are downstream in
  `pafio`.

## Implementation Stack

The active implementation target is a native C++20 + CMake codebase aligned
with the toolchain used by Styio. Python remains in-tree only for repository
automation, contract gates, and registry helper tooling.

## Verification

```bash
./scripts/docs-gate.sh
./scripts/checkpoint-health.sh
./scripts/delivery-gate.sh --mode checkpoint
```

## Planning

- [Pafio-Master-Plan.md](docs/planning/Pafio-Master-Plan.md)
- [Pafio-Stage-Review-and-Future-Features.md](docs/planning/Pafio-Stage-Review-and-Future-Features.md)
- [Pafio-Workstreams-and-TODOs.md](docs/planning/Pafio-Workstreams-and-TODOs.md)
- [Pafio-Verification-Matrix.md](docs/operations/Pafio-Verification-Matrix.md)
- [Pafio-Local-Offline-Package-Contract.md](docs/governance/Pafio-Local-Offline-Package-Contract.md)

## License

Apache-2.0. See [LICENSE](LICENSE).
