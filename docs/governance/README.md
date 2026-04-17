# Governance Docs

**Purpose:** Hold normative `spio` rules. These files define stable contracts, policy, and compatibility constraints.

**Last updated:** 2026-04-17

## Scope

- version decoupling rules
- CLI contract
- entrypoint and argument index
- manifest and lockfile conventions
- registry repository contract
- documentation maintenance model

Role-specific registry client/server documents live under `docs/registry/` and must link back to governance instead of redefining shared layout rules.
Private-boundary rules for auth/account/trust hooks live under `docs/security/` and must not be embedded here as deployment-specific detail.

## Maintenance Rule

If a governance document and a planning or operational document disagree, the governance document wins.
