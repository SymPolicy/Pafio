# pafio Source

**Purpose:** Hold the standalone `pafio` implementation. This directory must remain independent from `styio` compiler internals.

**Last updated:** 2026-07-30

## Rule

- Do not add source-level dependencies on `styio` implementation files.
- The authoritative implementation is native `C++20` built by `CMake`.
- `src/` is the package-manager and project-workflow implementation surface.
- Keep the native core layered:
  - `PafioCLI/` owns thin command routing, shared usage/help text, and machine-info contracts.
  - `PafioApp/` owns project workflow and package lifecycle orchestration.
  - domain modules such as `PafioResolve/`, `PafioPlan/`, and `PafioPublish/` own typed contracts, offline package behavior, and validation rules.
  - `PafioPlan/` renders the external Styio compile-plan handoff.
  - `PafioCore/` owns shared infrastructure such as process execution and path policy.

## Registry Split

- `src/PafioRegistryClient/` owns registry read-plane consumption and local materialization.
- `src/PafioSecurity/` owns trust, hash, archive, and client-input validation.
- `src/PafioPublish/` owns publish-candidate preparation and the bounded archive
  request sent to the platform-owned HTTP(S) control plane.
- Registry signing, storage mutation, write authorization, and promotion are
  platform-owned server responsibilities and must not be implemented here.

## Implementation Notes

- Public JSON payloads must come from domain-owned serializer modules such as:
  - `PafioResolve/MetadataContract.*`
  - `PafioResolve/ResolutionContract.*`
  - `PafioCLI/MachineInfoContract.*`
- `PafioCLI/CLI.cpp` must remain a routing shell; it must not grow new payload builders, direct workflow validation, or private `fork/exec` helpers.
- All external process execution must go through `PafioCore/Process.*`.
