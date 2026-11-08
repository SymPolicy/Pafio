# ADR-0009: Plan Workspace Split

**Purpose:** Record the split of the former `docs/plan/` workspace into completed delivery evidence (`docs/plan/`) and the active delivery programme (`docs/plans/`).

**Last updated:** 2026-10-15

## Status

Accepted.

## Supersedes

- The `docs/plan/` workspace-ownership clause of ADR-0001.

## Context

ADR-0001 designated `docs/plan/` as the active Better Plan workspace owning execution sequencing, checkpoints, and still-open future work. The task-resource delivery milestone reorganized that workspace: completed delivery groups stayed in `docs/plan/` as evidence, while the current programme — `Programme.json`, `Requirements.json`, and the per-milestone Tree/Node/Task state — moved to `docs/plans/`.

The durable record still described the retired layout, so a maintainer following the ADR set landed in the evidence archive instead of the active workspace.

## Decision

`docs/plan/` retains completed delivery evidence: `Manifest.json`, `Capabilities.json`, and the finished `pafio-product-convergence` and `pafio-thin-core` groups. Its README records that historical ownership.

`docs/plans/` is the active Better Plan workspace. It owns the current delivery programme (`Programme.json`, `Requirements.json`) and the per-milestone state under `docs/plans/<milestone>/`. It is internal coordination material — it lives in development checkouts and the internal coordination store, and is deliberately excluded from the published tree.

`docs/plan/` is a registered documentation collection covered by `scripts/docs-index.py` and `scripts/docs-audit.py`; `docs/governance/Docs-Maintenance-Model.md` assigns its owner entry. `docs/plans/` is not a published collection and carries no index or gate surface.

## Alternatives

Keeping active sequencing inside `docs/plan/` alongside completed evidence preserved the older path but forced readers to separate open state from closed records inside one tree. A single archive location for both active and completed plans would reintroduce that ambiguity.

## Consequences

README entry points, `docs/adr/README.md`, and the maintenance owner map name `docs/plans/` as the active internal workspace. ADR-0001's Decision text is updated to reference this split so its governance statements stay accurate. Future workspace relocations require an ADR update in the same change.
