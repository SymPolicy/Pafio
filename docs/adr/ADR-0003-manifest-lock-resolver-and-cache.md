# ADR-0003: Manifest, Lock, Resolver, and Content-Addressed Cache

**Purpose:** Record the deterministic dependency transaction shared by every Pafio project workflow.

**Last updated:** 2026-07-30

## Status

Accepted.

## Decision

`pafio.toml`, `pafio.lock`, resolution v1, and content-addressed materialization
form one transaction. Pafio resolves workspace, path, pinned Git, and exact
registry dependencies with the conservative `single-version-v1` policy.

`pafio sync` owns lock generation and immutable source preparation. `add` and
`remove` use the same resolver and rollback on failure. `check`, `build`, `run`,
and `test` call the same sync transaction before compiler validation.

`--locked` prohibits lock mutation, `--offline` prohibits network access, and
`--frozen` combines both. Lock and resolution output is canonical and never
contains host-absolute paths.

## Consequences

The resolver may reject graphs accepted by a multi-version ecosystem. Cache
presence never substitutes for digest and archive verification. Dependency
changes commit atomically or leave the previous manifest, lock, and resolution
state intact.
