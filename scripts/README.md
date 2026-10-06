# pafio Scripts

**Purpose:** Hold repository-local helper scripts used to validate extractability, contract hygiene, and black-box test setup for `pafio`.

**Last updated:** 2026-04-24

## Current Public Scripts

For fresh-machine bootstrap and the common build/test flow, start with [../docs/BUILD-AND-DEV-ENV.md](../docs/BUILD-AND-DEV-ENV.md).

- docs index generator
- docs lifecycle validator
- docs/process gate
- repository hygiene gate
- team runbook maintenance gate
- external `General-Auditor` gate
- checkpoint health gate
- delivery gate
- curl-pipe installer for installing the `pafio` binary and managed `styio` shim into a PATH directory
- native configure/build/test entrypoint
- Debian 13 / compatible Debian/Ubuntu dev environment bootstrap with the shared LLVM 18.1.x, CMake/CTest 3.31.6, and Python 3.13.5 baseline
- extractability self-check
- contract fixture validation
- black-box integration runner setup
- published-binary compiler handoff and interface acceptance gate
- synthetic multi-tenant compile-cloud stress gate with container hot replacement checks
- registry server publish/fetch acceptance gate
- registry promotion from writable origin storage to read-side serving root
- registry `v2` role-key generation, local publish worker, `v1 -> v2` import, and static-root verification
- local HTTP server for the registry `v2` publish/verify control-plane contract
- copy subtree into an external standalone repository target
- migration preflight that chains bootstrap, extractability, and optional published external compiler handshake checks

## Private Script Rule

- auth-bearing registry publish smoke gates belong under the gitignored `scripts-private/` tree
- the tracked public scripts may validate redacted security metadata, but they must not carry credentials or deployment-owned auth policy
