# spio Docs

**Purpose:** Define the modular structure and lifecycle of `spio/docs/` so planning, policy, operations, active summaries, history, and archived provenance can evolve without drifting into one another.

**Last updated:** 2026-04-17

## Modules

- `rollups/` — compressed active summaries and default reading order
- `history/` — active daily recovery notes
- `archive/` — archived provenance and lifecycle metadata
- `governance/` — normative rules and single-source contracts
- `security/` — public/private security boundary for auth, account, and trust hooks
- `adr/` — durable design and implementation decisions
- `planning/` — phases, workstreams, retrospectives, and TODO decomposition
- `registry/` — client contract, server contract, and deployment baseline for shared package distribution
- `operations/` — gates, preflight, registry server runbook, and split runbook
- `teams/` — daily-work ownership, review routing, handoff, and checkpoint entrypoints
- `styio/` — external compiler knowledge pack and public interface expectations

## Entry Points

1. `INDEX.md`
2. `rollups/CURRENT-STATE.md`
3. `planning/Styio-Ecosystem-Delivery-Master-Plan.md`
4. `planning/Styio-Ecosystem-File-Governance-Alignment-Plan.md`
5. `governance/Docs-Maintenance-Model.md`
6. `teams/COORDINATION-RUNBOOK.md`

## Recommended Reading Order

1. `rollups/CURRENT-STATE.md`
2. `rollups/NEXT-STAGE-GAP-LEDGER.md`
3. `planning/Styio-Ecosystem-Delivery-Master-Plan.md`
4. `planning/Styio-Ecosystem-File-Governance-Alignment-Plan.md`
5. `planning/Spio-Master-Plan.md`
6. `planning/Spio-Stage-Review-and-Future-Features.md`
7. `planning/Spio-Future-Direction-and-Styio-Coordination.md`
8. `governance/Spio-Version-Decoupling-Constraints.md`
9. `adr/INDEX.md`
10. `governance/Spio-Entry-Argument-Index.md`
11. `governance/Spio-Registry-Repository-Contract.md`
12. `governance/Docs-Maintenance-Model.md`
13. `teams/COORDINATION-RUNBOOK.md`
14. `security/Spio-Private-Security-Module-Contract.md`
15. `registry/Spio-Registry-Client-Contract.md`
16. `registry/Spio-Registry-Server-Contract.md`
17. `registry/Spio-Registry-Deployment-Baseline.md`
18. `planning/Spio-Workstreams-and-TODOs.md`
19. `operations/Spio-Verification-Matrix.md`
20. `operations/Spio-Registry-Server-Runbook.md`
21. `styio/Styio-External-Interface-Requirement-Spec.md`
22. `styio/Styio-for-Spio-Developers.md`
23. `operations/Spio-Repo-Split-Runbook.md`

## Precedence

When documents disagree:

1. `governance/`
2. `security/`
3. `registry/`
4. `adr/`
5. `operations/`
6. `planning/`
7. `teams/`
8. `styio/`

## Maintenance Rules

- Files here describe `spio` as if it were already an independent repository.
- Do not duplicate a rule across modules; link to the owner document instead.
- Refresh generated indexes with `python3 scripts/docs-index.py --write` after docs-tree changes.
- Validate docs lifecycle with `python3 scripts/docs-lifecycle.py validate`.
- Verify docs structure with `python3 scripts/docs-audit.py`.
- Keep named gate commands in `operations/Spio-Verification-Matrix.md`.
- Keep team ownership, review routing, and handoff summaries in `teams/`; do not move normative contract text there.
- When a rule here conflicts with an implementation shortcut, the shortcut must be treated as technical debt and recorded explicitly.
