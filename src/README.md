# pafio Source

**Purpose:** Hold the standalone `pafio` implementation. This directory must remain independent from `styio` compiler internals.

**Last updated:** 2026-04-24

## Rule

- Do not add source-level dependencies on `styio` implementation files.
- The authoritative implementation path should move toward native `C++20` sources built by `CMake`.
- Python modules under `src/` are package-manager automation, local package portability, and compatibility helpers where referenced by CLI scripts; platform service tooling moves to `pafio`.
- `src/` is the package-manager implementation surface. Human-facing control-console assets belong under `../frontend/console/` and must consume published contracts, not direct source coupling.
- Keep the native core layered:
  - `PafioCLI/` owns thin command routing, shared usage/help text, and machine-info contracts.
  - `PafioApp/` owns command-cluster orchestration for workflow, tool, local package, and package operations, plus temporary platform compatibility commands.
  - domain modules such as `PafioResolve/`, `PafioTool/`, and `PafioPublish/` own package-manager typed contracts, offline package behavior, and validation rules.
  - `PafioCloud/` and `PafioPlan/` are compatibility renderers until `pafio` consumes `pafio` APIs directly.
  - `PafioCore/` owns shared infrastructure such as process execution and path policy.

## Registry Split

- `src/PafioRegistryClient/` owns registry v2 static read-plane consumption and local materialization, but not private trust/auth policy.
- `src/pafio_registry_v2/` owns registry v2 key generation, local publish, verification, and temporary control-plane helpers used by scripts and tests; server-side ownership moves to `pafio`.
- `src/PafioSecurity/` owns the public registry-security interface boundary.
- `src/PafioPublish/` owns shared publish-candidate preparation and must not absorb registry control-plane logic.
- `src-private/` is gitignored and reserved for closed-source security implementations; public extractability excludes it.

## Implementation Notes

- Public JSON payloads must come from domain-owned serializer modules such as:
  - `PafioCloud/Contract.*` for platform compatibility payloads
  - `PafioResolve/ProjectGraphContract.*`
  - `PafioTool/Contract.*`
  - `PafioCLI/MachineInfoContract.*`
- `PafioCLI/CLI.cpp` must remain a routing shell; it must not grow new payload builders, direct workflow validation, or private `fork/exec` helpers.
- All external process execution must go through `PafioCore/Process.*`.
