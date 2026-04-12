# spio Docs

**Purpose:** Define the modular structure of `spio/docs/` so planning, policy, operations, and external compiler knowledge can evolve without drifting into one another.

**Last updated:** 2026-04-12

## Modules

- `governance/` — normative rules and single-source contracts
- `security/` — public/private security boundary for auth, account, and trust hooks
- `adr/` — durable design and implementation decisions
- `planning/` — phases, workstreams, retrospectives, and TODO decomposition
- `registry/` — client contract, server contract, and deployment baseline for shared package distribution
- `operations/` — gates, preflight, registry server runbook, and split runbook
- `styio/` — external compiler knowledge pack and public interface expectations

## Recommended Reading Order

1. `planning/Spio-Master-Plan.md`
2. `planning/Spio-Stage-Review-and-Future-Features.md`
3. `planning/Spio-Future-Direction-and-Styio-Coordination.md`
4. `governance/Spio-Version-Decoupling-Constraints.md`
5. `adr/INDEX.md`
6. `governance/Spio-Entry-Argument-Index.md`
7. `governance/Spio-Registry-Repository-Contract.md`
8. `governance/Docs-Maintenance-Model.md`
9. `security/Spio-Private-Security-Module-Contract.md`
10. `registry/Spio-Registry-Client-Contract.md`
11. `registry/Spio-Registry-Server-Contract.md`
12. `registry/Spio-Registry-Deployment-Baseline.md`
13. `planning/Spio-Workstreams-and-TODOs.md`
14. `operations/Spio-Verification-Matrix.md`
15. `operations/Spio-Registry-Server-Runbook.md`
16. `styio/Styio-External-Interface-Requirement-Spec.md`
17. `styio/Styio-for-Spio-Developers.md`
18. `operations/Spio-Repo-Split-Runbook.md`

## Precedence

When documents disagree:

1. `governance/`
2. `security/`
3. `registry/`
4. `adr/`
5. `operations/`
6. `planning/`
7. `styio/`

## Maintenance Rules

- Files here describe `spio` as if it were already an independent repository.
- Do not duplicate a rule across modules; link to the owner document instead.
- Keep named gate commands in `operations/Spio-Verification-Matrix.md`.
- When a rule here conflicts with an implementation shortcut, the shortcut must be treated as technical debt and recorded explicitly.
