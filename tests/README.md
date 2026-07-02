# pafio Tests

**Purpose:** Define the split between `pafio` unit tests and black-box integration tests, with strict environment and cache isolation.

**Last updated:** 2026-04-24

## Test Classes

- `unit/` covers manifest parsing, lockfile behavior, resolver logic, cache-key derivation, and compatibility decisions.
- native `C++20` unit coverage lives under the CMake test targets and should become the authoritative phase-2 path.
- `integration/` covers real subprocess execution against an external `styio` binary.

## Isolation Rules

- Every test run must set a fresh temporary `PAFIO_HOME`.
- Integration tests must use `PAFIO_STYIO_BIN` and must not assume a source checkout of `styio`.
- Tests must not write into the repository root except under explicit temporary directories created for the run.

## Acceptance Gates

- `pafio_manifest_lock_gate`
- `quality_no_binaries_gate`
- `quality_repo_hygiene_gate`
- `performance_baseline_gate`
- `delivery_package_gate`
- `pafio_submit_gate`
- `contract_schema_gate`
- `pafio_resolver_gate`
- `pafio_cli_gate`
- `pafio_workflow_gate`
- `pafio_extractability_gate`
- `pafio_registry_server_gate`
- `pafio_registry_promotion_gate`
- `pafio_registry_split_origin_http_gate`
- `pafio_registry_v2_unit_gate`
- `pafio_registry_v2_contract_gate`
- `pafio_registry_v2_publish_gate`
- `pafio_registry_v2_static_http_gate`
- `pafio_registry_control_plane_contract_gate`
- `pafio_registry_control_plane_http_gate`
- `pafio_native_contract_source_gate`
- `pafio_hosted_api_contract_unit_gate`
- `pafio_hosted_api_contract_integration_gate`
- `pafio_hosted_api_contract_regression_gate`
- `pafio_hosted_api_contract_smoke_gate`
- `pafio_hosted_api_contract_fuzz_gate`
- `pafio_cloud_compile_stress_gate`
- `styio_contract_compat_gate`
- `styio_compile_plan_contract_gate`

## Private Test Rule

- auth-bearing registry publish gates such as header, policy-file, and profile validation belong under the gitignored `tests-private/` tree
- the tracked public suite validates only the redacted open-source behavior and the absence of private security implementations

See also:

- `../docs/operations/Pafio-Verification-Matrix.md`
- `../scripts/preflight-readiness-check.py`
- `../scripts/registry-server-gate.py`
- `../scripts/styio-interface-gate.py`
