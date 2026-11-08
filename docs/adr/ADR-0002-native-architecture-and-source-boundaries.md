# ADR-0002: Native Architecture and Source Boundaries

**Purpose:** Record Pafio's native C++20 module boundaries and thin CLI architecture.

**Last updated:** 2026-07-30

## Status

Accepted.

## Decision

Pafio has one authoritative C++20/CMake implementation. `PafioCLI` parses and
routes commands; domain modules own manifests, resolution, registry clients,
compile-plan production, workflows, vendoring, packaging, and publishing.
Shared file, process, hashing, locking, and path policy stays in `PafioCore`.

Pafio does not compile or link Styio internals. It invokes a system executable
through the published process contract. Registry server, hosted workspace,
cloud job, worker, and control-plane implementations live only in Styio
Platform.

## Consequences

Native targets and focused tests follow domain ownership. The CLI cannot become
a second serializer or resolver, and this repository cannot host extension
trees that reintroduce compiler or Platform implementations.
