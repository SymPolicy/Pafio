# spio Tests

**Purpose:** Define the split between `spio` unit tests and black-box integration tests, with strict environment and cache isolation.

**Last updated:** 2026-04-12

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
- `spio_registry_server_gate`
- `spio_registry_promotion_gate`
- `spio_registry_split_origin_http_gate`
- `styio_contract_compat_gate`
- `styio_compile_plan_contract_gate`

## Private Test Rule

- auth-bearing registry publish gates such as header, policy-file, and profile validation belong under the gitignored `tests-private/` tree
- the tracked public suite validates only the redacted open-source behavior and the absence of private security implementations

See also:

- `../docs/operations/Spio-Verification-Matrix.md`
- `../scripts/preflight-readiness-check.py`
- `../scripts/registry-server-gate.py`
- `../scripts/styio-interface-gate.py`
