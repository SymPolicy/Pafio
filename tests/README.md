# pafio Tests

**Purpose:** Define the split between `pafio` unit tests and black-box integration tests, with strict environment and cache isolation.

**Last updated:** 2026-09-08

## Test Classes

- `unit/` covers repository scripts and fixture-level policies.
- native `C++20` coverage under the CMake test target is authoritative for product behavior.
- `pafio_process_tests` is the independent native process-contract target. Its
  executable also supplies its child-process fixtures, including Windows
  argument, environment, pipe, handle-inheritance, and Job Object scenarios.
- `integration/` covers real subprocess execution against an external `styio` binary.

## Isolation Rules

- Every test run must set a fresh temporary `PAFIO_HOME`.
- Integration tests must use `PAFIO_STYIO_BIN` and must not assume a source checkout of `styio`.
- Tests must not write into the repository root except under explicit temporary directories created for the run.

Use focused CTest filters during implementation. Cross-repository compiler,
Platform, and Vityo acceptance runs from the fixed-revision product matrix.
Fixtures and logs must not contain credentials or backend runtime data.

Build `pafio_process_tests`, then run CTest with `-R '^PortableProcess\.'`.
On Windows multi-configuration builds, select the same `--build-config` as the
build. This focused target also runs in the explicitly dispatched Windows lane;
`native_process_only=true` selects that lane for adaptation work.
