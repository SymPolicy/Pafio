# ADR Docs

**Purpose:** Define the conventions and scope for `pafio/docs/adr/`, which holds durable design and implementation decisions for the standalone `pafio` project.

**Last updated:** 2026-10-15

## Scope

1. Store architecture, lifecycle, workflow-boundary, and implementation-scope decisions that need durable context here.
2. Keep normative policy in `docs/governance/`.
3. Keep execution sequencing in `docs/plans/` (internal workspace, excluded from the published tree); `docs/plan/` retains completed delivery evidence.
4. Keep gate commands and operational procedures in `docs/operations/`.
5. Once a planned behavior is implemented, ensure the durable decision is represented here before removing the plan or historical source that introduced it.

## Conventions

1. Filenames use `ADR-XXXX-<functional-slug>.md`.
2. Each ADR includes `Status`, `Context`, `Decision`, `Alternatives`, and `Consequences`.
3. Governance documents remain the source of truth for normative contracts; ADRs explain the accepted decision and its tradeoffs.
4. New public CLI, manifest/lock, compatibility, or workflow-boundary decisions should land in ADR form before or with the implementation change.
5. ADRs are grouped by feature or function, not by phase, sprint, or implementation increment.
6. When smaller historical ADRs are merged, the functional ADR records them under `Supersedes`.
7. Historical notes may be removed after their durable decision value has been captured in ADR form.

## Inventory

See [INDEX.md](./INDEX.md).
