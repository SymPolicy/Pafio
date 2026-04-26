# spio

**Purpose:** `spio` is the local-first package manager and project workflow client for Styio. It is designed to work offline when packages are available locally and to use `styio-platform` only as an optional service foundation.

**Last updated:** 2026-04-24

## Scope

- `spio` manages package manifests, lockfiles, dependency resolution, cache layout, local package import/export, package build orchestration, and project-level commands.
- `spio` does not parse Styio source semantics on its own.
- `spio` does not own hosted compile services, registry server control planes, worker pools, or extensible cloud-service backends; those live in `styio-platform`.
- `spio` must remain useful without a platform connection when dependencies are satisfied by workspace, path, vendor, cache, or explicitly imported offline packages.
- `spio` supports two project toolchain modes:
  - `binary`: published compiler path through a versioned machine contract and a process boundary
  - `build`: source-build path through an official `styio` source checkout and local compiler build cache

## Product Surface Split

- `frontend/console/` is reserved for the repo-hosted human control console page.
- `src/` remains the native package-manager core and compatibility renderer for client-side package, registry, toolchain, and compile-plan handoff behavior.
- `docs/registry/` and `docs/governance/` remain the SSOT for package-manager client contracts; service-side runbooks and control planes move to `styio-platform`.
- the native CLI stays the machine/admin surface; it is not the user-facing control console.

The normative split is defined in
[`docs/governance/Spio-Control-Console-And-Service-Split.md`](docs/governance/Spio-Control-Console-And-Service-Split.md).

## Native Target Split

- `src/` no longer builds as one monolithic `spio_core`.
- package-manager/domain code now composes from internal static libraries such as `spio_foundation`, `spio_manifest`, `spio_resolution`, `spio_toolchain_service`, `spio_package_service`, and `spio_project_service`.
- the CLI surface now sits on top as `spio_cli_support`, `spio_cli_commands`, and `spio_cli_shell`, with the `spio` executable linking the shell target only.
- this keeps the local CLI split from compatibility payload rendering while platform service binaries move to `styio-platform`.

The source-level ownership summary lives in
[`src/README.md`](src/README.md) and the planning note for the split lives in
[`docs/planning/Spio-Native-Target-Split.md`](docs/planning/Spio-Native-Target-Split.md).

## Independence Rules

- `spio` must not include or link against `styio` implementation headers or libraries.
- `spio` must not depend on files under `../src`, `../tests`, or any other compiler-internal path.
- `spio` may depend on a published external `styio` executable only through the documented binary-mode discovery path such as `--styio-bin` or `SPIO_STYIO_BIN`.
- `spio` source-build mode may fetch the official `styio` source tree from `https://github.com/SymPolicy/Styio.git`, using the `stable` and `nightly` branches as the channel roots, through the documented source-build contract and cache layout.
- `spio/contracts/` is the source of truth for package-manager-side machine contracts. Hosted workspace, registry control-plane server, and cloud-service contracts are downstream in `styio-platform`.
- Local package import/export is a client-side contract. It must not require
  `styio-platform`; platform mirrors only improve discovery and distribution.

## Tree

```text
spio/
  frontend/
    console/
  src/
  tests/
    unit/
    integration/
  docs/
  contracts/
  scripts/
```

## Transitional Note

The current repository root still hosts the existing `styio` compiler project directly. This `spio/` subtree is being prepared so it can later be moved wholesale into `$HOME/DevSpace/Unka-Malloc/styio-spio` without dragging compiler source code along with it.

## Implementation Stack Note

The active implementation target is a native `C++20` + `CMake` codebase aligned with the operational toolchain used by `styio`.

The native core is now the active implementation path for:

- CLI shape
- manifest and lockfile validation rules
- machine-facing contract boundaries
- registry `v2` static distribution and control-plane contract gates
- offline package cache, local import/export, and project-local Styio environment optimization

Python remains in-tree only where it owns repository automation, contract gates, and registry/control-plane helper tooling.

## Developer Context Pack

Before moving this subtree into `$HOME/DevSpace/Unka-Malloc/styio-spio`, `spio` developers should read:

- `docs/external/for-styio/Styio-for-Spio-Developers.md`
- `docs/external/for-styio/Styio-Public-Interface-Roadmap.md`
- `docs/governance/Spio-Version-Decoupling-Constraints.md`

Those documents are the migration knowledge pack for working against `styio` without creating hidden source-level dependencies.

## Developer Entry Points

Start repo bootstrap and common build/test commands from [docs/BUILD-AND-DEV-ENV.md](docs/BUILD-AND-DEV-ENV.md).

Project-local workflow mode selection now uses:

- `./scripts/spio use binary`
- `./scripts/spio use build`
- `./scripts/spio set channel as stable`
- `./scripts/spio set channel as nightly`
- `./scripts/spio set build as minimal`
- `./scripts/spio set risk as trusted-internal|partner-controlled|untrusted-user`
- `./scripts/spio set lane as isolated|warm-shared`
- `./scripts/spio set security as sandbox-default|partner-restricted|trusted-warm`
- `./scripts/spio project-graph --json`
- `./scripts/spio cloud status --json`
- `./scripts/spio cloud plan --json build minimal`
- `./scripts/cloud-compile-stress.py --require-hot-replacement --summary-json /tmp/spio-cloud-stress-summary.json --events-jsonl /tmp/spio-cloud-stress-events.jsonl`
- `./scripts/spio sync`
- `./scripts/spio tool status --json`
- `./scripts/spio build minimal`

`./scripts/spio` is the repository-local convenience wrapper. It ensures the native binary exists under `./build-codex/bin/spio` and then forwards the remaining arguments. Use the wrapper in day-to-day developer docs; use the explicit binary path when a gate or external harness needs a concrete executable.

Current source-build and platform boundary:

- `build` mode is implemented as a local source-build workflow rooted in the official `https://github.com/SymPolicy/Styio.git` source tree.
- `cloud status` and `cloud plan` remain local machine-readable compatibility surfaces for package-manager clients.
- the remote async control plane, queue, worker pools, hosted workspace APIs, registry server control planes, and extensible cloud-service runbooks belong to `styio-platform`.
- offline package use, local package import/export, local cache warm-up, vendor snapshots, and project-local compiler environment tuning belong to `styio-spio`.

统一 docs/process 与交付入口分别为：

- `./scripts/docs-gate.sh`
- `./scripts/checkpoint-health.sh`
- `./scripts/delivery-gate.sh --mode checkpoint`

## Planning Entry Points

For the full implementation and migration plan, start with:

- `docs/planning/Spio-Master-Plan.md`
- `docs/planning/Spio-Stage-Review-and-Future-Features.md`
- `docs/planning/Spio-Workstreams-and-TODOs.md`
- `docs/operations/Spio-Verification-Matrix.md`
- `docs/operations/Spio-Repo-Split-Runbook.md`
- `docs/governance/Spio-Local-Offline-Package-Contract.md`

Recommended preflight before moving this subtree:

```text
./scripts/bootstrap-dev-env.sh
./scripts/preflight-readiness-check.py --styio-bin /absolute/path/to/styio
```
