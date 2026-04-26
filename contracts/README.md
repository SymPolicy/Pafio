# spio Contracts

**Purpose:** Hold the source-of-truth machine contracts owned by the `spio` side of the `spio` / `styio` boundary.

**Last updated:** 2026-04-21

## Rules

- Files here are versioned public machine contracts.
- `styio` may vendor snapshots from here, but must not become the source of truth for them.
- Any breaking change requires a new versioned directory, not an in-place rewrite.

## Contents

- `compile-plan/` — build orchestration contract from `spio` to `styio`
- `compat/` — supported compiler matrix declarations used by `spio`
- `hosted-control-plane/` — repo-hosted/cloud workspace API contract consumed by frontend clients
- `registry-control-plane/` — shared native JSON service contract for operating a registry `v2` root; `styio-platform` hosts the service side while `spio` retains client compatibility
- `registry-v2/` — industrial static package-distribution contract with signed metadata and append-only package indexes

Generated third-party API-description artifacts are not contract sources in this
tree. Compatibility is proven from the versioned JSON packages and examples.
