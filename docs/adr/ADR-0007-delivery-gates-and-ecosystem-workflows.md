# ADR-0007: Delivery Gates and Ecosystem Workflows

**Purpose:** Require executable, fixed-revision evidence for Pafio's cross-repository product claims.

**Last updated:** 2026-07-30

## Status

Accepted.

## Decision

Focused gates validate each changed Pafio domain. Cross-repository acceptance
pins exact Styio, Pafio, Platform, Vityo, site, audit, and aggregate-workspace
revisions and exercises owner contracts rather than implementation internals.

The matrix covers external Styio discovery, compile-plan handoff, metadata v1,
workflow JSON, Platform publish/consume and worker execution, and Vityo local
and hosted adapters. Only after all implementation and independent audit
closures pass does each affected repository run one full regression.

## Consequences

Documentation cannot claim compatibility based on prose or mutable branch
heads. Failed ownership handoffs block coordinated release without creating a
compatibility alias or duplicated fallback path.
