# spio Control Console

**Purpose:** Hold the repo-hosted human control-console frontend for `spio`.

**Last updated:** 2026-04-21

## Scope

- workspace, registry, toolchain, execution, and deployment visibility
- operator-facing controls that call published backend routes
- static assets that can be hosted from the repository or a CDN

## Rules

- backend truth stays in `src/`, `docs/governance/`, and `docs/registry/`
- this frontend must consume published HTTP/JSON contracts only
- do not place manifest parsing, resolver logic, publish orchestration, or private auth policy here

See also:

- [../../docs/governance/Spio-Control-Console-And-Service-Split.md](../../docs/governance/Spio-Control-Console-And-Service-Split.md)
- [../../docs/governance/Spio-Control-Console-Backend-Surface.md](../../docs/governance/Spio-Control-Console-Backend-Surface.md)
- [../../docs/registry/Spio-Registry-Server-Contract.md](../../docs/registry/Spio-Registry-Server-Contract.md)
