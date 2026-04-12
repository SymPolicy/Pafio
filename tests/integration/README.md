# spio Integration Tests

**Purpose:** Describe the black-box integration tests that validate `spio` against external dependencies such as a published `styio` executable or a shared registry origin through public contracts only.

**Last updated:** 2026-04-12

## Requirements

- Use `SPIO_STYIO_BIN` to locate the compiler.
- Create an isolated temporary project workspace per test.
- Create an isolated temporary `SPIO_HOME` per test.
- Use only fixture files under `fixtures/`.
- Never read compiler source files directly.

## Planned Gates

- `spio_registry_server_gate`
- `spio_registry_promotion_gate`
- `spio_registry_split_origin_http_gate`
- `spio_workflow_gate`
- `spio_extractability_gate`
- `styio_spio_dual_maintenance_gate`

Private auth-bearing publish gates are intentionally excluded from this tracked directory and should live under `tests-private/` when a closed-source security module is in use.

Primary preflight entry:

```text
./scripts/preflight-readiness-check.py --styio-bin /absolute/path/to/styio
```

Gate definitions live in:

- `../../docs/operations/Spio-Verification-Matrix.md`
