# pafio Integration Tests

**Purpose:** Describe the black-box integration tests that validate `pafio` against external dependencies such as a published `styio` executable or a shared registry origin through public contracts only.

**Last updated:** 2026-04-12

## Requirements

- Use `PAFIO_STYIO_BIN` to locate the compiler.
- Create an isolated temporary project workspace per test.
- Create an isolated temporary `PAFIO_HOME` per test.
- Use only fixture files under `fixtures/`.
- Never read compiler source files directly.

## Planned Gates

- `pafio_registry_server_gate`
- `pafio_registry_promotion_gate`
- `pafio_registry_split_origin_http_gate`
- `pafio_workflow_gate`
- `pafio_extractability_gate`
- `styio_pafio_dual_maintenance_gate`

Private auth-bearing publish gates are intentionally excluded from this tracked directory and should live under `tests-private/` when a closed-source security module is in use.

Primary preflight entry:

```text
./scripts/preflight-readiness-check.py --styio-bin /absolute/path/to/styio
```

Gate definitions live in:

- `../../docs/operations/Pafio-Verification-Matrix.md`
