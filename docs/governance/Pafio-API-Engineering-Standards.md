# Pafio API Engineering Standards

**Purpose:** Freeze the native JSON engineering standards used for public `pafio` HTTP contract packages so clients and backend services can develop independently against the same interface artifacts.

**Last updated:** 2026-04-24

## Contract Model

`pafio` API contract work uses repository-native JSON packages under `contracts/`.
The package manager must not rely on generated third-party API description
formats for client/server compatibility. Each contract package owns:

1. operation identifiers, methods, route paths, and versioned base paths
2. request, success, and failure envelope shapes
3. canonical examples used by tests and documentation
4. local gates that validate shape, example, snapshot, smoke, and malformed-input behavior

## Required Local Rules

1. Public HTTP APIs ship as versioned native JSON contract packages under `contracts/`.
2. Multi-step client/server workflows are modeled as named operations and example packs in the native contract package.
3. Reusable request, response, schema, and example payloads must live in contract shapes instead of being repeated inline.
4. `operationId` values are stable integration identifiers and must remain unique within a contract version.
5. Additive optional fields are allowed inside a stable major version; removing operations, renaming fields, changing required fields, or changing enum meaning requires a new version.
6. Contract packages must be checked for drift in CI; human-readable docs explain the package but do not replace it.
7. Consumer repos bind to published contracts and examples, not to private source layout or unpublished payloads.
8. Native source gates must reject reintroducing forbidden generated API-description artifacts or references.
