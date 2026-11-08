# pafio Scripts

**Purpose:** Hold repository-local helper scripts used to validate extractability, contract hygiene, and black-box test setup for `pafio`.

**Last updated:** 2026-07-30

## Current Public Scripts

For fresh-machine bootstrap and the common build/test flow, start with [../docs/BUILD-AND-DEV-ENV.md](../docs/BUILD-AND-DEV-ENV.md).

- docs index generator
- docs lifecycle validator
- docs/process gate
- repository hygiene gate
- team runbook maintenance gate
- repository-local exact-match secret scan
- policy check (CI authority, authorization defaults, transport, container runtime, release artifacts)
- dependency policy check- checkpoint health gate
- delivery gate
- native configure/build/test entrypoint
- Debian 13 trixie dev and CI environment bootstrap with the shared LLVM 18.1.x, CMake/CTest 3.31.6, and Python 3.13.5 baseline
- extractability self-check
- contract fixture validation
- published-binary compiler handoff and interface acceptance gate
- copy subtree into an external standalone repository target
- published external compiler handshake checks

## Security Rule

Repository scripts may validate redacted client contracts, but they must not
carry credentials, production keys, service runtime data, or deployment-owned
authorization policy.
