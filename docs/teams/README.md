# pafio Team Runbooks

**Purpose:** Define the scope, naming rules, and maintenance rules for `docs/teams/`; product semantics, package-manager contracts, and registry behavior remain owned by their existing SSOT documents.

**Last updated:** 2026-04-19

## Scope

1. Store `pafio` team daily-work runbooks.
2. Provide ownership, review routing, checkpoint, and handoff entrypoints.
3. Keep cross-team coordination in [COORDINATION-RUNBOOK.md](./COORDINATION-RUNBOOK.md).
4. Do not restate detailed contract, planning, or registry SSOT here.

## Naming Rules

1. Team files use `<TEAM>-RUNBOOK.md`.
2. The top-level coordination entrypoint is `COORDINATION-RUNBOOK.md`.
3. The generated inventory lives in [INDEX.md](./INDEX.md).

## Maintenance Rules

1. If a change updates an owned surface, checkpoint path, or review trigger, update the affected runbook in the same delivery.
2. `scripts/team-docs-gate.py` is the automation entrypoint for this directory.
3. If a runbook changes, refresh [DOC-STATS.md](./DOC-STATS.md).
