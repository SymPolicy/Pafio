# Pafio Local Offline Package Contract

**Purpose:** Define deterministic local, vendored, cached, offline, and frozen dependency behavior.

**Last updated:** 2026-07-30

## Sources

Pafio may resolve workspace members, path dependencies, vendored snapshots,
content-addressed cached packages, pinned Git revisions, and exact registry
versions. Every non-local source is represented by immutable identity and
validated content metadata in the lock and resolution state.

## Modes

- `--locked` forbids lock mutation.
- `--offline` forbids network access.
- `--frozen` enables both restrictions.

An offline or frozen operation fails before partial state is committed when a
required immutable object is unavailable locally.

## Portability

`pafio vendor` creates project-local dependency state for offline use.
`pafio pack` creates a deterministic `.pafio.src.tar` source archive. Pafio
validates archive paths, size, manifest count, and content hashes before
materialization.

## Compiler Boundary

Offline dependency preparation does not imply compiler ownership. Project
workflows still require an independently installed compatible Styio binary.
Pafio neither stores nor mutates compiler installations under `PAFIO_HOME`.
