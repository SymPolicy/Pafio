# Governance Docs

**Purpose:** Hold normative `spio` rules. These files define stable contracts, policy, and compatibility constraints.

**Last updated:** 2026-04-24

## Scope

- version decoupling rules
- CLI contract
- local offline package and import/export contract
- package-manager compatibility view of cloud execution policy
- hosted control-plane handoff notes for `styio-platform`
- compile-cloud stress handoff notes for `styio-platform`
- API engineering standards
- entrypoint and argument index
- manifest and lockfile conventions
- registry repository contract
- documentation maintenance model

Role-specific registry client documents live under `docs/registry/`; server-side control-plane ownership moves to `styio-platform` and must be linked instead of redefined here.
Private-boundary rules for auth/account/trust hooks live under `docs/security/` and must not be embedded here as deployment-specific detail.

## Maintenance Rule

If a governance document and a planning or operational document disagree, the governance document wins.
