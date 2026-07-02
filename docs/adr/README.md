# ADR Docs

**Purpose:** Define the conventions and scope for `pafio/docs/adr/`, which holds durable design and implementation decisions for the standalone `pafio` project.

**Last updated:** 2026-04-10

## Scope

1. Store architecture, lifecycle, workflow-boundary, and implementation-scope decisions that need durable context here.
2. Keep normative policy in `docs/governance/`.
3. Keep execution sequencing in `docs/planning/`.
4. Keep gate commands and operational procedures in `docs/operations/`.

## Conventions

1. Filenames use `ADR-XXXX-<slug>.md`.
2. Each ADR includes `Status`, `Context`, `Decision`, `Alternatives`, and `Consequences`.
3. Governance documents remain the source of truth for normative contracts; ADRs explain the accepted decision and its tradeoffs.
4. New public CLI, manifest/lock, compatibility, or workflow-boundary decisions should land in ADR form before or with the implementation change.

## Inventory

See [INDEX.md](./INDEX.md).
