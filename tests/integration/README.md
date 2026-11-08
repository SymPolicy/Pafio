# pafio Integration Tests

**Purpose:** Describe the black-box integration tests that validate `pafio` against external dependencies such as a published `styio` executable or a shared registry origin through public contracts only.

**Last updated:** 2026-10-11

## Requirements

- Explicitly select the real compiler with `--styio-bin` (or `PAFIO_TEST_STYIO_BIN` for CTest).
- Create an isolated temporary project workspace per test.
- Create an isolated temporary `PAFIO_HOME` per test.
- Use only fixture files under `fixtures/`.
- Never read compiler source files directly.

## Owner Contract Gates

- `pafio_workflow_gate`
- `pafio_extractability_gate`
- Styio compile-plan interoperability
- Platform publish/consume and worker interoperability
- Vityo local and hosted adapter interoperability

Primary preflight entry:

```text
./scripts/preflight-readiness-check.py --styio-bin /absolute/path/to/styio
```

The active Better Plan owns the fixed-revision gate definitions.

## Permanent real workflow

`scripts/styio-interface-gate.py --task-resource --require-compile-plan` drives
real Pafio check/build/test/run with the selected real Styio compiler. It uses
one project copied from `fixtures/task-resource/`; `--work-dir` retains it for
review and Vityo reuse. Each command asserts current output ownership, error
recovery and compiler identity, with offline/locked/frozen and dry-run coverage.
Two actual concurrent Pafio builds check independent output ownership. The same
mandatory gate covers opt-in static snapshots, retained-parent delta publication,
unreadable-parent full-snapshot fallback, runtime-only observation, and observed
success/error/repaired-success with current session and task-settlement evidence. Fake
interop scripts test gate plumbing only; they do not establish real E2E.

Configure CMake with `-DPAFIO_TEST_STYIO_BIN=<selected-compiler>` to register
`pafio_task_resource_real_workflow`. If unset, that external-candidate test is
not registered, not passed. The milestone's focused and integration commands
in `docs/plans/task-resource-delivery/checks.sh` also invoke the permanent gate
with explicit selection. Neither command claims Vityo integration or runs a
full regression.
