# ADR-0008: Pafio Product and Ecosystem Boundary

**Purpose:** Record the clean ownership split between Pafio, Styio, Styio Platform, and Vityo.

**Last updated:** 2026-07-30

## Status

Accepted.

## Context

The trusted dependency kernel is necessary but not a complete user-facing product.
Conversely, managed compiler installation, hosted execution, registry services, and a
control console created duplicate owners inside the package-manager repository.

## Decision

Pafio is the Styio ecosystem's Cargo/CMake-style package manager and project workflow
entry. It owns manifests, locks, resolution, project creation, dependency sync, metadata,
build orchestration, vendoring, packaging, and publishing.

Styio is installed externally. Pafio discovers it through `--styio-bin`, then
`PAFIO_STYIO_BIN`, then `styio` on `PATH`; Pafio never installs, updates, switches, pins,
builds, or caches the compiler.

Styio owns compile-plan consumption, diagnostics, receipts, and runtime events. Styio
Platform owns registry and hosted execution services. Vityo composes the published Pafio,
Styio, and Platform contracts and never reads Pafio's private home state.

The public identity changes to Pafio once, with no legacy aliases or data migration.

## Consequences

Every compiler workflow performs the same sync transaction first. Metadata is a narrow
project snapshot rather than an editor or cloud aggregate. Existing legacy project files
and caches are intentionally not migrated.
