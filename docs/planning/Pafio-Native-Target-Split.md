# Pafio Native Target Split

**Purpose:** Define the native target graph inside `src/` so backend/service work can evolve independently from the CLI shell and the repo-hosted control console frontend.

**Last updated:** 2026-04-21

## Problem

`pafio` already has a product split between:

1. `frontend/console/` as the repo-hosted human control console
2. `src/` as the backend/domain implementation surface

But the native build graph used to collapse all backend, package, workflow, toolchain, registry, and CLI code into one `pafio_core` target. That made future extraction harder because every new binary or service-side entrypoint would inherit the CLI shell by default.

## Fixed Native Target Families

The native split now treats `src/` as three layers:

1. shared backend foundations
   `pafio_foundation`, `pafio_manifest`, `pafio_resolution`
2. backend/service domains
   `pafio_toolchain_service`, `pafio_package_service`, `pafio_project_service`
3. CLI shell
   `pafio_cli_support`, `pafio_cli_commands`, `pafio_cli_shell`

`pafio_backend_services` is the aggregation edge for service-side reuse. The `pafio` executable links the CLI shell only.

## Ownership Rules

1. New package, registry, project-graph, workflow, publish, or toolchain logic belongs in backend/service targets, not in CLI shell targets.
2. `PafioCLI/CLI.cpp` remains a routing shell and must not become a second service layer.
3. Future hosted/control-plane binaries should compose from backend/service targets and must not depend on `pafio_cli_shell` unless they intentionally expose CLI behavior.
4. `frontend/console/` must stay decoupled from `src/` implementation details and consume published contracts instead of direct native coupling.
5. If a new domain needs public machine payloads, the owning contract still lives under `contracts/` or `docs/governance/`, not inside this planning note.

## Immediate Outcome

This split is not yet a process-level service decomposition. It is the native build-graph prerequisite for that next step:

1. service binaries can be introduced without dragging the CLI shell along
2. backend domains can be tested and linked independently
3. the repo-hosted console frontend remains a separate product surface
