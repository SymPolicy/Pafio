# ADR-0004: Project Workflows and External Styio

**Purpose:** Record that Pafio owns project workflow orchestration while Styio remains an external compiler.

**Last updated:** 2026-07-30

## Status

Accepted as revised by ADR-0008.

## Context

Users need one terminal entry for dependency preparation and build, run, test, and
check workflows. Earlier implementation coupled this entry to managed compiler
installation, source builds, pins, and local cloud policy.

## Decision

Pafio owns project workflow intent, target selection, the pre-workflow sync
transaction, compile-plan production, compiler invocation, and the stable workflow
envelope.

Styio is discovered per request through `--styio-bin`, `PAFIO_STYIO_BIN`, or
`PATH`. Styio owns compile-plan consumption, diagnostics, receipts, and runtime
events. Pafio owns no compiler installation, update, switch, pin, source-build,
cache, channel, or cloud-execution policy.

## Consequences

All four compiler workflows share one deterministic sync path. Compiler identity is
request-local rather than persisted Pafio state. Hosted execution remains a Styio
Platform concern whose workers invoke the public `pafio build` command.
