# spio Docs

**Purpose:** Define the modular structure of `spio/docs/` so planning, policy, operations, and external compiler knowledge can evolve without drifting into one another.

**Last updated:** 2026-04-10

## Modules

- `governance/` — normative rules and single-source contracts
- `adr/` — durable design and implementation decisions
- `planning/` — phases, workstreams, and TODO decomposition
- `operations/` — gates, preflight, and split runbook
- `styio/` — external compiler knowledge pack and public interface expectations

## Recommended Reading Order

1. `planning/Spio-Master-Plan.md`
2. `governance/Spio-Version-Decoupling-Constraints.md`
3. `adr/INDEX.md`
4. `governance/Spio-Entry-Argument-Index.md`
5. `governance/Docs-Maintenance-Model.md`
6. `planning/Spio-Workstreams-and-TODOs.md`
7. `operations/Spio-Verification-Matrix.md`
8. `styio/Styio-for-Spio-Developers.md`
9. `operations/Spio-Repo-Split-Runbook.md`

## Precedence

When documents disagree:

1. `governance/`
2. `adr/`
3. `operations/`
4. `planning/`
5. `styio/`

## Maintenance Rules

- Files here describe `spio` as if it were already an independent repository.
- Do not duplicate a rule across modules; link to the owner document instead.
- Keep named gate commands in `operations/Spio-Verification-Matrix.md`.
- When a rule here conflicts with an implementation shortcut, the shortcut must be treated as technical debt and recorded explicitly.
