# Pafio Local Offline Package Contract

**Purpose:** Define the local-first package-manager contract for offline package use, local import/export, and project-local Styio environment optimization.

**Last updated:** 2026-04-24

## Mission

`pafio` is a client product. It must be able to resolve, validate, build, and
inspect projects without `pafio` when all required packages are already
available from local sources, vendored snapshots, cache state, or explicitly
imported offline package bundles.

## Offline Sources

Offline package resolution may use:

- workspace members
- path dependencies
- vendored dependency snapshots
- existing `PAFIO_HOME` cache entries
- deterministic source archives created by `pafio pack`
- future local import bundles that carry package metadata and source archives

When `--offline` or `--frozen` is active, `pafio` must not contact
`pafio`, registry mirrors, git remotes, or other network sources.

## Local Import / Export

The local portability path is client-side:

- export creates a deterministic package or dependency bundle that can move
  between machines without platform access
- import validates the bundle and materializes it into local cache or an
  explicit project-local package source
- imported packages must be visible to resolver and lock validation without
  requiring a platform endpoint
- bundle metadata must preserve package name, version, source fingerprint,
  archive hash, and dependency metadata

This contract does not make `pafio` a registry server. Global package
distribution, regional mirrors, and authoritative write/control planes belong
to `pafio`.

## Local Compiler Environment

`pafio` should optimize and improve the local Styio compiler environment through
binary-mode discovery, source-build mode, project-local compiler pins, cache
warm-up, and build-plan validation. Hosted execution and cloud workers remain
platform responsibilities.
