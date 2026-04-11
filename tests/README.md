# spio Tests

**Purpose:** Define the split between `spio` unit tests and black-box integration tests, with strict environment and cache isolation.

**Last updated:** 2026-04-09

## Test Classes

- `unit/` covers manifest parsing, lockfile behavior, resolver logic, cache-key derivation, and compatibility decisions.
- native `C++20` unit coverage lives under the CMake test targets and should become the authoritative phase-2 path.
- `integration/` covers real subprocess execution against an external `styio` binary.

## Isolation Rules

- Every test run must set a fresh temporary `SPIO_HOME`.
- Integration tests must use `SPIO_STYIO_BIN` and must not assume a source checkout of `styio`.
- Tests must not write into the repository root except under explicit temporary directories created for the run.

## Acceptance Gates

- `spio_manifest_lock_gate`
- `contract_schema_gate`
- `spio_resolver_gate`
- `spio_cli_gate`
- `spio_workflow_gate`
- `spio_extractability_gate`

See also:

- `../docs/operations/Spio-Verification-Matrix.md`
- `../scripts/preflight-readiness-check.py`
