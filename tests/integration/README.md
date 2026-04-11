# spio Integration Tests

**Purpose:** Describe the black-box integration tests that validate `spio` against an external `styio` executable through the public machine contract only.

**Last updated:** 2026-04-09

## Requirements

- Use `SPIO_STYIO_BIN` to locate the compiler.
- Create an isolated temporary project workspace per test.
- Create an isolated temporary `SPIO_HOME` per test.
- Use only fixture files under `fixtures/`.
- Never read compiler source files directly.

## Planned Gates

- `spio_workflow_gate`
- `spio_extractability_gate`
- `styio_spio_dual_maintenance_gate`

Primary preflight entry:

```text
./scripts/preflight-readiness-check.py --styio-bin /absolute/path/to/styio
```

Gate definitions live in:

- `../../docs/operations/Spio-Verification-Matrix.md`
