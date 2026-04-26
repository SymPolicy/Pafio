# spio Build And Dev Environment

**Purpose:** Provide the repository-level entry point for bootstrapping a fresh machine, configuring the native build, and finding the next operational docs.

**Last updated:** 2026-04-21

## Who This Is For

1. Contributors bringing up `spio` on a fresh Debian/Ubuntu VM or container.
2. Contributors who need the common native build, test, and preflight commands.
3. Contributors switching projects between published binary mode and source-build mode.
4. Contributors validating `spio` against a published external `styio` binary through the public machine contract.

## Fresh Machine Bootstrap

From the repository root:

```bash
./scripts/bootstrap-dev-env.sh
```

That installs the native C++20 and Python tooling used by the repository on Debian/Ubuntu.

## Standardized Baseline

`styio-spio` and `styio-nightly` share the same standardized native baseline:

1. Development host standard: Debian `13` (`trixie`).
2. Compiler toolchain standard: LLVM / Clang / LLD `18.1.x` via the `clang-18` package line.
3. CMake / CTest standard: `3.31.6`.
4. Validation Python standard: `3.13.5`.
5. Repository compatibility floor: CMake `3.20+` and C++20.
6. CI mirror: GitHub Actions on `ubuntu-24.04`, plus Python `3.13.5` and `cmake==3.31.6` installed before validation steps.

## Required Toolchains

1. A C++20 compiler.
2. LLVM / Clang / LLD `18.1.x` on the standardized host toolchain, even though `spio` consumes `styio` through process boundaries rather than linking LLVM directly.
3. CMake / CTest `3.31.6` for the standardized local and CI toolchain.
4. Python `3.13.5` for preflight and verification scripts in the standardized validation pipeline.
5. A published external `styio` executable only when exercising compatibility and binary-mode non-dry-run workflow handoff.
6. `git` when using source-build mode so `spio` can fetch the official `styio` source tree on demand.

## Typical Build And Test Commands

Configure:

```bash
cmake -S . -B build -G Ninja
```

Build and run native tests:

```bash
cmake --build build
ctest --test-dir build
```

Select a project toolchain mode and defaults:

```bash
./scripts/spio use binary --manifest-path path/to/spio.toml
./scripts/spio use build --manifest-path path/to/spio.toml
./scripts/spio set channel as stable --manifest-path path/to/spio.toml
./scripts/spio set channel as nightly --manifest-path path/to/spio.toml
./scripts/spio set build as minimal --manifest-path path/to/spio.toml
./scripts/spio set risk as trusted-internal --manifest-path path/to/spio.toml
./scripts/spio set lane as warm-shared --manifest-path path/to/spio.toml
./scripts/spio set security as trusted-warm --manifest-path path/to/spio.toml
./scripts/spio project-graph --json --manifest-path path/to/spio.toml
./scripts/spio cloud status --json --manifest-path path/to/spio.toml
./scripts/spio cloud plan --json build minimal --manifest-path path/to/spio.toml
./scripts/spio tool status --json --manifest-path path/to/spio.toml
```

`./scripts/spio` is the repository-local convenience wrapper. It ensures the native binary exists under `./build-codex/bin/spio` and then forwards the remaining arguments. Use the wrapper in developer-facing examples; use the explicit binary path when another tool or gate needs a concrete executable argument.

Cloud execution notes:

1. The tracked native core currently exposes a local cloud-execution contract baseline rather than a remote scheduler.
2. `risk`, `lane`, and `security` are persisted in `spio-toolchain.lock`.
3. The resolved `cloud` policy may downgrade a preferred lane; for example, `untrusted-user` always resolves to `isolated`.
4. `spio cloud plan --json ...` renders the normalized request body shape that `POST /api/styio-platform/v1/jobs` must continue to accept.

Run the project build flow:

```bash
./scripts/spio build minimal --manifest-path path/to/spio.toml --dry-run
./scripts/spio run --manifest-path path/to/spio.toml --dry-run
./scripts/spio test --manifest-path path/to/spio.toml --dry-run
```

Source-build mode may fetch the official `styio` source tree when needed:

```bash
./scripts/spio use build --manifest-path path/to/spio.toml
./scripts/spio build minimal --manifest-path path/to/spio.toml --yes
```

The default source-build origin is `https://github.com/SymPolicy/Styio.git`, and the project channel selects the matching source branch:

1. `stable` -> `stable`
2. `nightly` -> `nightly`

Current cloud-execution boundary:

1. `spio cloud status --json` and `spio cloud plan --json` are implemented in the tracked native core today.
2. Those commands freeze local execution-policy and future request-body shapes.
3. They do **not** imply that the tracked open-source tree already ships a remote scheduler, queue, or worker pool.

Run repository-native verification:

```bash
./scripts/checkpoint-health.sh
./scripts/delivery-gate.sh --mode checkpoint --skip-health
```

Run binary-mode preflight against a published external compiler:

```bash
./scripts/checkpoint-health.sh --styio-bin /absolute/path/to/styio
```

Run source-build mode without a published external compiler:

```bash
./scripts/spio use build --manifest-path tests/unit/fixtures/manifests/ok-single-package/spio.toml
./scripts/spio build minimal --manifest-path tests/unit/fixtures/manifests/ok-single-package/spio.toml --yes
```

## Subsystem-Specific Follow-Ups

1. Planning and migration roadmap: [planning/Spio-Master-Plan.md](./planning/Spio-Master-Plan.md)
2. Current implementation-stage snapshot and remaining gaps: [planning/Spio-Stage-Review-and-Future-Features.md](./planning/Spio-Stage-Review-and-Future-Features.md)
3. Verification matrix: [operations/Spio-Verification-Matrix.md](./operations/Spio-Verification-Matrix.md)
4. Published external compiler requirements for `binary` mode: [external/for-styio/Styio-External-Interface-Requirement-Spec.md](./external/for-styio/Styio-External-Interface-Requirement-Spec.md)
5. Cloud execution policy contract: [governance/Spio-Cloud-Control-Plane-Contract.md](./governance/Spio-Cloud-Control-Plane-Contract.md)
6. Script inventory: [../scripts/README.md](../scripts/README.md)

## Related Docs

1. Docs tree guide: [README.md](./README.md)
2. Version-decoupling rules: [governance/Spio-Version-Decoupling-Constraints.md](./governance/Spio-Version-Decoupling-Constraints.md)
3. Repo split runbook: [operations/Spio-Repo-Split-Runbook.md](./operations/Spio-Repo-Split-Runbook.md)
