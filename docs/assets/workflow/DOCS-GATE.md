# Docs Gate

**Purpose:** Define the common docs/process gate entrypoint for `pafio` so contributors can run owner-runbook maintenance, docs audit, and ecosystem CLI contract consistency through one command.

**Last updated:** 2026-07-30

## Command

Worktree docs gate:

```bash
./scripts/docs-gate.sh
```

Staged-only docs gate:

```bash
./scripts/docs-gate.sh --mode staged
```

Push or CI docs gate:

```bash
./scripts/docs-gate.sh --mode push --base origin/main
```

## What It Runs

1. `python3 scripts/team-docs-gate.py`
2. `python3 scripts/docs-audit.py`
3. `python3 scripts/ecosystem-cli-doc-gate.py`

`docs-audit.py` runs with `STYIO_SKIP_TEAM_DOC_GATE=1` when invoked through this entrypoint so the owner gate uses the requested change source exactly once.
The ecosystem gate validates Pafio-owned contracts locally. Cross-repository
documentation is checked only through its explicit fixed-workspace form, so the
default gate never delegates to an adjacent mutable checkout.
