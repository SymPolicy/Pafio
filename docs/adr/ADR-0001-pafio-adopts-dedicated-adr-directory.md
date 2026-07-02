# ADR-0001: Pafio Adopts a Dedicated ADR Directory

**Purpose:** Record the decision, context, alternatives, and consequences for storing durable `pafio` design and implementation decisions under `docs/adr/`.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

`pafio` began as a bootstrap-heavy repository with decisions spread across planning documents, governance contracts, and implementation patches. That was workable for early scaffolding, but it made it too easy for design rationale and accepted implementation boundaries to disappear into code or chat history.

The repository also needs to align with the decision-record discipline already used in `styio`, where durable ownership, lifecycle, and workflow-boundary decisions are recorded as ADRs.

## Decision

1. Add a dedicated `docs/adr/` directory to the `pafio` repository.
2. Record durable design and implementation decisions in `ADR-XXXX-<slug>.md` files.
3. Maintain `docs/adr/README.md` for rules and `docs/adr/INDEX.md` for discoverability.
4. Keep governance documents as the normative contract source of truth; ADRs explain accepted decisions and tradeoffs, not replace the contracts.

## Alternatives

1. Keep decisions only in governance and planning documents.
   - Rejected because those documents own policy and sequencing, not durable rationale for individual accepted decisions.
2. Keep decisions only in commit history or pull requests.
   - Rejected because decision context becomes hard to discover after repository split or refactor churn.
3. Use one monolithic architecture document.
   - Rejected because independent decisions would become harder to update and reference cleanly.

## Consequences

Positive:

1. `pafio` now has a stable place for durable decision records.
2. Future contributors can trace why a boundary or scope was accepted without reading code archaeology.
3. The repository aligns better with the development discipline already used by `styio`.

Negative:

1. Contributors must keep ADRs updated as the design evolves.
2. The docs tree gains another maintenance surface that can drift if ignored.
