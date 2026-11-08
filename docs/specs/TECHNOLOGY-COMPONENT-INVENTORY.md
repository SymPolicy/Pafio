# Technology and Component Inventory

**Purpose:** Record Pafio's maintained implementation stack and ownership boundaries for audit.

**Last updated:** 2026-07-30

## Technology

- C++20 and CMake/CTest
- `tomlplusplus` and `nlohmann/json`
- GoogleTest
- Python and shell repository gates

## Pafio Components

- `PafioManifest`: manifest v1 and canonical lock state
- `PafioResolve`: deterministic graph, metadata v1, and resolution v1
- `PafioRegistryClient` and `PafioSecurity`: registry reads, trust, hashes, and archive validation
- `PafioWorkflow` and `PafioPlan`: automatic sync and external Styio handoff
- `PafioVendor`, `PafioPack`, and `PafioPublish`: package lifecycle clients
- `PafioCLI` and `PafioApp`: stable terminal and machine interfaces

## External Owners

- Styio owns compilation, diagnostics, receipts, and runtime events.
- Styio Platform owns registry/control-plane services, hosted workspaces, cloud jobs, and workers.
- Vityo owns UI adapters over Pafio, Styio, and Platform contracts.

## Open-Source Components

- `nlohmann_json`
- `tomlplusplus`
- `googletest`
- CMake and CTest
- Python standard library tooling
- Bash shell tooling
- GitHub Actions

## Dependency Manifests

- `CMakeLists.txt`
- `src/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `.github/workflows/*.yml`
