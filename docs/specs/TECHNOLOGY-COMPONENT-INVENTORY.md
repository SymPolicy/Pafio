# Technology And Component Inventory

**Purpose:** Define the required technology-stack, internal-component, open-source-component, and dependency-manifest inventory for `pafio`.

**Last updated:** 2026-04-24

This document is the repository-local maintenance rule for the manifest inventory audited by `General-Auditor`. The canonical audit module must list the same surfaces in `for-pafio/module.json`; if this document and the audit manifest diverge, the change is not closed.

## Required Inventory Fields

Every audit manifest for this repository must maintain these non-empty lists:

1. `technology_stack`
2. `internal_components`
3. `open_source_components`
4. `dependency_manifests`

Missing or stale lists are audit failures. They block license, commercial-risk, ownership, and usage-boundary review because auditors cannot prove what stack and components are in scope.

## Current Inventory

Technology stack:

- C++ package manager, resolver, registry, toolchain, and process code built with CMake and CTest.
- Python registry, control-plane, docs, hygiene, and interop gates.
- Bash delivery and checkpoint scripts.
- JSON, TOML, and lockfile contract artifacts.
- TypeScript and web fixture surfaces present in the repository.
- GitHub Actions workflow automation.

Internal components:

- `PafioManifest`, `PafioResolve`, and `PafioPlan` manifest/resolver/compile-plan graph.
- `PafioRegistryClient` and `pafio_registry_v2` registry materialization and trust layer.
- `PafioToolchain` and `PafioTool` managed compiler/toolchain state.
- `PafioCore` process handling and `PafioCloud` control-plane integration.
- Native, interop, unit, docs, and delivery gate suites.
- Registry v2 publish, verify, keygen, promote, and control-plane helper scripts.

Open-source and external components:

- CMake and CTest.
- `nlohmann_json`.
- `tomlplusplus`.
- `googletest`.
- Python standard library tooling.
- Bash shell tooling.
- GitHub Actions.

Dependency manifest surfaces:

- `CMakeLists.txt`.
- `src/CMakeLists.txt`.
- `tests/CMakeLists.txt`.
- TOML support files and lockfiles where present.
- `.github/workflows/*.yml`.

## Maintenance Rule

Update this document and the matching `General-Auditor` project module in the same change whenever any of these occur:

1. A language, SDK, runtime, build system, CI system, package manager, or generated-code tool is added or removed.
2. A first-party package, registry, resolver, toolchain, cloud/control-plane, gate, or workflow boundary is added, renamed, or retired.
3. An open-source or external component is introduced, removed, vendored, promoted from fixture-only to production use, or given a new usage boundary.
4. A dependency manifest is added, removed, renamed, or moved.
5. License, Apache-2.0, commercial-authorization, subscription, membership, trial-only, or proprietary-use evidence changes.

For new external dependencies, create or update dependency usage-boundary evidence before the change can pass audit.
