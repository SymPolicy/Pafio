# ADR-0001: Documentation Governance and Plan Workspace

**Purpose:** Record the functional documentation decision that ADRs, governance documents, generated indexes, and the `docs/plans/` workspace each own a distinct part of the package-manager project workflow.

**Last updated:** 2026-10-15

## Status

Accepted.

## Supersedes

- `ADR-0001-pafio-adopts-dedicated-adr-directory.md`
- `ADR-0003-entry-argument-index-ssot.md`
- `ADR-0032-docs-lifecycle-and-generated-indexes.md`
- `ADR-0038-plan-directory-is-better-plan-workspace.md`

## Context

The project started with planning notes, implementation notes, and durable decisions spread across several documentation locations. That made it easy for command syntax, helper-script arguments, owner documents, and historical rationale to drift from the implemented package-manager behavior.

The documentation system now needs a smaller set of durable records grouped by feature and function, not by delivery phase. Active planning also needs a machine-checkable home compatible with the Better Plan workflow.

## Decision

`docs/adr/` is the durable decision-record collection for implemented architecture, workflow-boundary, manifest/lock, registry, trust, and documentation-governance decisions.

`docs/governance/` owns normative contracts: command syntax, argument indexes, manifest and lockfile rules, compatibility boundaries, lifecycle rules, and operating policies.

`docs/plans/` is the active internal Better Plan workspace — coordination state kept out of the published tree. It owns execution sequencing, checkpoints, and still-open future work; `docs/plan/` retains completed delivery evidence (see ADR-0009). Implemented behavior must move into ADR form before the plan or historical source that introduced it is retired.

Generated indexes remain checked artifacts. `scripts/docs-index.py` writes `INDEX.md` files, `scripts/docs-lifecycle.py validate` checks lifecycle metadata, and `scripts/docs-gate.sh` composes the documentation gate.

User-visible entry points, helper scripts, environment variables, and workflow flags remain centralized through the entry and argument index instead of being redefined independently in plans, runbooks, and scripts.

## Alternatives

Keeping every historical ADR as a separate phase record preserved chronology but made the current design harder to scan by function.

Keeping active plans in `docs/planning/` would have preserved the older path name but would not reflect the explicit Better Plan workspace contract now used by the repository.

Storing historical notes under `docs/history/` or `docs/archive/` would duplicate ADR responsibilities once their durable decision value has already been captured.

## Consequences

ADR count should grow by meaningful functional decision area, not by every implementation increment.

Plans can be deleted or rewritten once they are no longer active, provided durable implemented decisions have been recorded in ADR form.

Documentation maintenance has a clear gate: generated indexes must be current, lifecycle metadata must be valid, and new implemented behavior must have an ADR owner when it changes a durable contract.
