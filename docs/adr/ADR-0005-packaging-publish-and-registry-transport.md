# ADR-0005: Packaging, Publish Client, and Registry Ownership

**Purpose:** Record the split between Pafio package lifecycle clients and Styio Platform registry services.

**Last updated:** 2026-07-30

## Status

Accepted as revised by ADR-0008.

## Context

Deterministic package archives and publish validation are project lifecycle
operations, while registry storage, control-plane policy, promotion, and hosted
availability are service responsibilities.

## Decision

Pafio owns `vendor`, deterministic `pack`, publish preflight, registry trust, and
the `publish` client. Package archives and registry dependencies use immutable
content identities.

Styio Platform owns the registry/control-plane service, write authorization,
promotion, hosted storage, and operational policy. Pafio contains no production
registry server or hosted control-plane implementation.

## Consequences

Pafio client tests use only bounded fixtures. The production registry contract and
network service tests live with Platform. The Pafio publish client may target a
filesystem fixture or a Platform endpoint without becoming the server owner.
