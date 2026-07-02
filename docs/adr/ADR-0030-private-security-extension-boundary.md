# ADR-0030: Security-Sensitive Registry Auth and Trust Logic Moves Behind a Private Extension Boundary

**Purpose:** Record the decision to move auth-, trust-, and deployment-sensitive registry behavior behind a private extension boundary while keeping a public interface in tree.

**Last updated:** 2026-04-12

## Status

Accepted. 2026-04-12.

## Context

`pafio` now serves both client and server registry roles. That created two classes of security-sensitive logic:

- write-side auth/account policy for remote publish
- read-side trust policy for registry consumption

The public tree had started to accumulate write-side header, policy-file, and profile-resolution logic directly under `src/PafioRegistryServer/`, together with tests that echoed effective request headers in public JSON output. That was the wrong boundary for a repository that is intended to be open-sourced while still supporting private deployment policy.

## Decision

We move all auth/account/trust implementation behind a dedicated public interface:

- `src/PafioSecurity/RegistrySecurity.hpp`

The tracked open-source tree now keeps only:

- the interface types
- default non-secret behavior
- redacted observability fields

The build may optionally compile closed-source implementations from gitignored directories:

- `src-private/`
- `tests-private/`
- `docs-private/`
- `scripts-private/`

The public extractability path must exclude those directories.

## Consequences

Positive:

- auth/account/trust policy is no longer shipped in the open-source tree
- read-side and write-side security hooks are explicit and testable without leaking implementation
- public JSON output no longer needs to expose raw request headers or private policy paths

Tradeoffs:

- open-source builds no longer provide header/profile/policy-based remote publish behavior by default
- auth-specific integration tests move out of the tracked public test suite and into optional private tests

## Follow-Up Rules

- public docs must describe only the interface and redacted outputs
- private deployment behavior must live under `docs-private/`
- registry client/server docs must link to the security boundary rather than re-document private auth policy
