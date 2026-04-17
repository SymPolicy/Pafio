# spio Team Runbooks

**Purpose:** Define the scope, naming, and maintenance rules for `docs/teams/`; stable policy, contracts, and gate commands remain owned by the existing `governance/`, `registry/`, `security/`, `operations/`, and `styio/` documents.

**Last updated:** 2026-04-16

## Scope

1. Store short daily-work runbooks for maintainers of tracked `spio` surfaces.
2. Provide ownership, review-routing, checkpoint, and handoff entrypoints.
3. Keep cross-team routing in [COORDINATION-RUNBOOK.md](./COORDINATION-RUNBOOK.md).
4. Do not redefine CLI contracts, compatibility policy, registry layout, security rules, or gate command details here.

## Naming Rules

1. Team files use `<TEAM>-RUNBOOK.md`.
2. The shared coordinator entrypoint is `COORDINATION-RUNBOOK.md`.
3. Inventory for this directory lives in [INDEX.md](./INDEX.md).
4. Team runbooks link to owner documents instead of copying full policy or gate tables.

## Maintenance Rules

1. When an owned surface, review trigger, checkpoint path, or handoff expectation changes, update the affected runbook in the same delivery.
2. If a team boundary changes, update both the affected runbook and [COORDINATION-RUNBOOK.md](./COORDINATION-RUNBOOK.md).
3. Until an automated runbook gate exists, reviewers should treat stale ownership docs as a delivery defect.
4. If a runbook and an owner contract disagree, the owner contract wins.

## Inventory

See [INDEX.md](./INDEX.md).
