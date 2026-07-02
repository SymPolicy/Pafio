# ADR-0003: Entry and Argument Index as the Parameter SSOT

**Purpose:** Record the decision, context, alternatives, and consequences for centralizing user-visible command, helper-script, and environment-variable parameters in a dedicated index document.

**Last updated:** 2026-04-10

## Status

Accepted

## Context

`pafio` parameters were drifting across governance documents, helper scripts, and native implementation code. The command surface was documented, but detailed argument spellings were only partially visible in contracts and partially inferable from source.

The repository owner explicitly requested that scattered parameters be collected into a single entry-index document.

## Decision

1. Add `docs/governance/Pafio-Entry-Argument-Index.md` as the single index for:
   - global CLI flags
   - implemented per-command arguments
   - helper-script arguments
   - public environment variables
2. Keep exit-code, error-shape, and schema rules in their existing governance contracts.
3. Require parameter changes to update the index first, then the owning implementation and tests.

## Alternatives

1. Keep parameter details spread across code and multiple documents.
   - Rejected because discovery cost stays high and drift remains likely.
2. Move all CLI and script rules into one giant contract file.
   - Rejected because it would blur the distinction between contract ownership and parameter indexing.
3. Rely on `--help` output alone.
   - Rejected because helper-script parameters and environment variables still need durable documentation.

## Consequences

Positive:

1. Parameter discovery now has a clear entrypoint.
2. Helper scripts and public environment variables are documented alongside CLI flags.
3. Future command implementation work has a better chance of staying synchronized with docs.

Negative:

1. Another governance document must be kept current.
2. The index can still drift if contributors bypass the documented update flow.
