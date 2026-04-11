# Docs Maintenance Model

**Purpose:** Keep `spio` documentation modular and maintainable by assigning a single owner module to each kind of knowledge.

**Last updated:** 2026-04-10

## 1. Module Roles

### `docs/governance/`

Owns:

- normative rules
- compatibility policy
- CLI and schema contracts
- documentation precedence rules

Must not:

- define implementation sequencing
- duplicate gate command lists from operations

### `docs/adr/`

Owns:

- durable decision records
- accepted architecture and workflow-boundary rationale
- implementation-scope decisions that need durable historical context

Must not:

- replace normative policy owned by governance
- become a second planning backlog
- become the only place a public contract is defined

### `docs/planning/`

Owns:

- delivery phases
- workstreams
- TODO decomposition
- planning summaries

Must not:

- redefine CLI, schema, or compatibility rules
- redefine exact gate commands

### `docs/operations/`

Owns:

- acceptance gates
- executable verification entry points
- repository split runbook

Must not:

- redefine policy that belongs to governance
- redefine backlog decomposition that belongs to planning

### `docs/styio/`

Owns:

- `styio` knowledge pack for `spio` developers
- public compiler interface expectations
- external dependency references

Must not:

- define `spio` internal policy
- define migration rules for files outside published interfaces

## 2. Single Source of Truth Map

- version decoupling rules: `docs/governance/Spio-Version-Decoupling-Constraints.md`
- CLI/exit-code/error contract: `docs/governance/Spio-CLI-Contract.md`
- entrypoint and argument index: `docs/governance/Spio-Entry-Argument-Index.md`
- manifest/lock conventions: `docs/governance/Spio-Manifest-and-Lock-Conventions.md`
- design and implementation decision records: `docs/adr/INDEX.md`
- overall roadmap: `docs/planning/Spio-Master-Plan.md`
- workstream TODOs: `docs/planning/Spio-Workstreams-and-TODOs.md`
- bootstrap summary: `docs/planning/Spio-Bootstrap-Checklist.md`
- gate definitions and commands: `docs/operations/Spio-Verification-Matrix.md`
- split procedure: `docs/operations/Spio-Repo-Split-Runbook.md`
- `styio` developer knowledge: `docs/styio/Styio-for-Spio-Developers.md`
- `styio` public interface expectations: `docs/styio/Styio-Public-Interface-Roadmap.md`

## 3. Drift Prevention Rules

- A rule must be defined in exactly one owner document.
- Summary documents may link to owner documents but must not restate detailed rules.
- Named gate commands live only in `docs/operations/Spio-Verification-Matrix.md`.
- The split runbook may reference preflight and copy commands, but must not become a second verification matrix.
- Workstream files may name gates, but gate pass commands must stay in operations.
- `styio` knowledge docs may describe published compiler behavior, but they must not define `spio` compatibility policy.
- ADRs may explain an accepted policy or implementation boundary, but the normative rule must still live in its owner document.

## 4. Update Workflow

When a change happens:

- CLI or exit-code change: update governance first, then tests, then planning/operations references if needed
- argument or helper-script parameter change: update `Spio-Entry-Argument-Index.md` first, then the owning contract/script/tests
- new public workflow-boundary or implementation-scope decision: add or update an ADR in `docs/adr/` with the same change
- compatibility change: update governance plus `contracts/compat/*`, then verification coverage
- gate command change: update operations first, then any summaries that link to the gate
- migration procedure change: update operations runbook and preflight script together
- new `styio` public interface: update `docs/styio/*`, then compatibility or workflow docs as needed

## 5. Known Defects

- This model adds one more governance document to maintain.
- It reduces duplication, but it cannot prevent stale links if files move again without a coordinated pass.
