# spio Scripts

**Purpose:** Hold repository-local helper scripts used to validate extractability, contract hygiene, and black-box test setup for `spio`.

**Last updated:** 2026-04-18

## Current Public Scripts

- native configure/build/test entrypoint
- extractability self-check
- contract fixture validation
- black-box integration runner setup
- compiler handoff and interface acceptance gate
- canonical sample-workflow gate that runs managed-toolchain switch, workspace `--package`, vendored-offline, and registry-hosted-source matrices against published `styio` + `spio` binaries
- registry server publish/fetch acceptance gate
- registry promotion from writable origin storage to read-side serving root
- copy subtree into an external standalone repository target
- migration preflight that chains bootstrap, extractability, and optional external compiler handshake checks
- tracked-file binary content gate for CI and local repository hygiene
- repository hygiene gate for ignored/generated/private-path policy checks
- performance baseline gate for stable CLI regression checks
- delivery package gate that validates exported tree cleanliness
- unified submit gate that chains quality, regression, performance, and delivery checks
- git hook installer that enforces submit gate at pre-push
- submit-gate feature-flag example for release/styio/cloud hard-control toggles
- single-source artifact policy (`artifact-policy.json`) for ignore/hygiene/export rules and narrow binary allowlist exceptions
- rsync exclude emitter that keeps delivery export filters aligned with artifact policy

## Private Script Rule

- auth-bearing registry publish smoke gates belong under the gitignored `scripts-private/` tree
- the tracked public scripts may validate redacted security metadata, but they must not carry credentials or deployment-owned auth policy
