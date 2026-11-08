# Pafio Build and Development Environment

**Purpose:** Define the supported local build, test, and Styio handoff workflow for Pafio contributors.

**Last updated:** 2026-08-01

## Prerequisites

- CMake 3.20 or newer
- a C++20 compiler
- Python 3
- Git
- rsync for extractability and delivery-tree validation
- a system-installed `styio` executable for compiler workflow tests

Pafio does not install or build Styio. A workflow discovers the compiler through
`--styio-bin`, then `PAFIO_STYIO_BIN`, then `styio` on `PATH`.

## Native Build

```bash
cmake -S . -B build-codex -DPAFIO_BUILD_TESTS=ON
cmake --build build-codex --target pafio pafio_native_tests
ctest --test-dir build-codex --output-on-failure
```

The repository wrapper builds on demand:

```bash
./scripts/pafio --help
./scripts/pafio machine-info --json
```

## Project Workflow

```bash
./scripts/pafio new hello
cd hello
pafio build
pafio run
pafio test
```

`check`, `build`, `run`, and `test` run the same dependency `sync` transaction
before invoking Styio. Use `--locked`, `--offline`, or `--frozen` to constrain
that transaction.

## Focused Validation

During implementation, run the smallest native test filter or contract gate
covering the changed surface. Run the full repository suite once after all
closures are complete.

Generated state belongs under `.pafio/` or `PAFIO_HOME`; neither location is a
source-of-truth document or an integration API.
