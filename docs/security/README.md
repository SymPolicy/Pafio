# Security Docs

**Purpose:** Define the public/private boundary for security-sensitive `spio` code so registry auth, account policy, and trust decisions do not leak into the open-source tree.

**Last updated:** 2026-04-13

## Scope

- private module boundaries for auth/account/trust logic
- public interface ownership for security hooks
- gitignored source and test locations reserved for closed-source security implementations

## Ownership

- the public interface boundary lives in [Spio-Private-Security-Module-Contract.md](./Spio-Private-Security-Module-Contract.md)
- registry layout and non-secret transport rules remain owned by [../governance/Spio-Registry-Repository-Contract.md](../governance/Spio-Registry-Repository-Contract.md)
- client/server registry behavior remains owned by the documents under [../registry/](../registry/)

## Maintenance Rules

- do not put secret material, tokens, account rules, or trust allowlists under the tracked `src/`, `tests/`, `docs/`, or `scripts/` trees
- keep private implementation paths under the gitignored `*-private/` roots documented here
- when a public command surface reserves a security hook, the open-source contract must describe only the interface and redacted observability, not the private implementation details
- when extending registry security privately, prefer delegating to the public default helpers before adding deployment-specific behavior
