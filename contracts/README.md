# spio Contracts

**Purpose:** Hold the source-of-truth machine contracts owned by the `spio` side of the `spio` / `styio` boundary.

**Last updated:** 2026-04-09

## Rules

- Files here are versioned public machine contracts.
- `styio` may vendor snapshots from here, but must not become the source of truth for them.
- Any breaking change requires a new versioned directory, not an in-place rewrite.

## Contents

- `compile-plan/` — build orchestration contract from `spio` to `styio`
- `compat/` — supported compiler matrix declarations used by `spio`
