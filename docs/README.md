# pafio Docs

**Purpose:** Define the modular structure of `pafio/docs/` so planning, policy, operations, and external compiler knowledge can evolve without drifting into one another.

**Last updated:** 2026-04-24

## Entry Points

1. Build and dev environment: [BUILD-AND-DEV-ENV.md](./BUILD-AND-DEV-ENV.md)
2. Planning roadmap: [planning/Pafio-Master-Plan.md](./planning/Pafio-Master-Plan.md)
3. Stage review and future direction: [planning/Pafio-Stage-Review-and-Future-Features.md](./planning/Pafio-Stage-Review-and-Future-Features.md)
4. Version-decoupling rules: [governance/Pafio-Version-Decoupling-Constraints.md](./governance/Pafio-Version-Decoupling-Constraints.md)
5. Local offline package contract: [governance/Pafio-Local-Offline-Package-Contract.md](./governance/Pafio-Local-Offline-Package-Contract.md)
6. Platform migration handoff: [planning/Pafio-Styio-Cloud-Migration-Handoff.md](./planning/Pafio-Styio-Cloud-Migration-Handoff.md)
7. Package-manager verification matrix: [operations/Pafio-Verification-Matrix.md](./operations/Pafio-Verification-Matrix.md)
7. Verification matrix: [operations/Pafio-Verification-Matrix.md](./operations/Pafio-Verification-Matrix.md)
8. External compiler knowledge pack: [external/for-styio/Styio-for-Pafio-Developers.md](./external/for-styio/Styio-for-Pafio-Developers.md)

## Modules

- `assets/` — reusable workflow and gate documentation
- `governance/` — normative rules and single-source contracts
- `security/` — public/private security boundary for auth, account, and trust hooks
- `specs/` — cross-cutting agent, audit, repository-boundary, dependency, and documentation rules
- `adr/` — durable design and implementation decisions
- `planning/` — phases, workstreams, retrospectives, and TODO decomposition
- `registry/` — registry v2 static read-plane, package-manager client contracts, offline package expectations, and server control-plane handoff notes pointing to `styio-cloud`
- `operations/` — gates, preflight, package-manager validation, and split runbooks
- `external/for-styio/` — external compiler knowledge pack and public interface expectations
- `teams/` — owner runbooks, review routing, and delivery-facing ownership boundaries

## Recommended Reading Order

1. `BUILD-AND-DEV-ENV.md`
2. `planning/Pafio-Master-Plan.md`
3. `planning/Pafio-Stage-Review-and-Future-Features.md`
4. `planning/Pafio-Future-Direction-and-Styio-Coordination.md`
5. `governance/Pafio-Version-Decoupling-Constraints.md`
6. `adr/INDEX.md`
7. `governance/Pafio-Cloud-Control-Plane-Contract.md`
8. `governance/Pafio-Entry-Argument-Index.md`
9. `registry/Pafio-Registry-V2-Protocol.md`
10. `governance/Pafio-Local-Offline-Package-Contract.md`
11. `registry/Pafio-Registry-Control-Plane-Contract.md`
12. `registry/Pafio-Registry-V2-Publish-Control-Plane.md`
13. `governance/Docs-Maintenance-Model.md`
14. `specs/audit/CODE-AUDIT-CHECKLIST.md`
15. `security/Pafio-Private-Security-Module-Contract.md`
16. `registry/Pafio-Registry-Client-Contract.md`
17. `registry/Pafio-Registry-Deployment-Baseline.md`
18. `planning/Pafio-Workstreams-and-TODOs.md`
19. `operations/Pafio-Verification-Matrix.md`
20. `planning/Pafio-Styio-Cloud-Migration-Handoff.md`
21. `operations/Pafio-Repo-Split-Runbook.md`
22. `external/for-styio/Styio-External-Interface-Requirement-Spec.md`
23. `external/for-styio/Styio-for-Pafio-Developers.md`

## Precedence

When documents disagree:

1. `governance/`
2. `security/`
3. `registry/`
4. `specs/`
5. `adr/`
6. `operations/`
7. `planning/`
8. `external/for-styio/`

## Maintenance Rules

- Files here describe `pafio` as if it were already an independent repository.
- Do not duplicate a rule across modules; link to the owner document instead.
- Keep named gate commands in `operations/Pafio-Verification-Matrix.md`.
- Repository-level docs automation entrypoints are `scripts/docs-index.py`, `scripts/docs-lifecycle.py`, and `scripts/docs-audit.py`.
- When a rule here conflicts with an implementation shortcut, the shortcut must be treated as technical debt and recorded explicitly.
