# Docs Maintenance Model

**Purpose:** Keep `spio` documentation modular and maintainable by assigning a single owner module to each kind of knowledge.

**Last updated:** 2026-04-12

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

### `docs/registry/`

Owns:

- registry client/server role separation
- client-side consumption contract details
- server-side write contract details
- registry deployment baseline

Must not:

- redefine shared repository layout owned by governance
- redefine generic gate commands owned by operations
- duplicate planning priorities

### `docs/security/`

Owns:

- public/private security extension boundaries
- rules about what auth/account/trust logic must stay out of the tracked tree
- reserved private source, test, script, and documentation roots

Must not:

- redefine registry repository layout
- redefine operational gate commands
- embed deployment-specific secrets or credentials

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
- registry server runbook
- repository split runbook

Must not:

- redefine policy that belongs to governance
- redefine backlog decomposition that belongs to planning

### `docs/external/for-styio/`

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
- private security boundary: `docs/security/Spio-Private-Security-Module-Contract.md`
- registry v2 static read-plane protocol: `docs/registry/Spio-Registry-V2-Protocol.md`
- registry v2 control-plane contract: `docs/registry/Spio-Registry-Control-Plane-Contract.md`
- registry v2 publish-plane responsibilities: `docs/registry/Spio-Registry-V2-Publish-Control-Plane.md`
- registry client contract: `docs/registry/Spio-Registry-Client-Contract.md`
- registry deployment baseline: `docs/registry/Spio-Registry-Deployment-Baseline.md`
- design and implementation decision records: `docs/adr/INDEX.md`
- overall roadmap: `docs/planning/Spio-Master-Plan.md`
- stage review and future feature priorities: `docs/planning/Spio-Stage-Review-and-Future-Features.md`
- future direction and cross-team coordination: `docs/planning/Spio-Future-Direction-and-Styio-Coordination.md`
- workstream TODOs: `docs/planning/Spio-Workstreams-and-TODOs.md`
- bootstrap summary: `docs/planning/Spio-Bootstrap-Checklist.md`
- gate definitions and commands: `docs/operations/Spio-Verification-Matrix.md`
- registry server operational validation: `docs/operations/Spio-Registry-Server-Runbook.md`
- split procedure: `docs/operations/Spio-Repo-Split-Runbook.md`
- `styio` developer knowledge: `docs/external/for-styio/Styio-for-Spio-Developers.md`
- `styio` handoff interface spec: `docs/external/for-styio/Styio-External-Interface-Requirement-Spec.md`
- `styio` public interface expectations: `docs/external/for-styio/Styio-Public-Interface-Roadmap.md`

## 3. Drift Prevention Rules

- A rule must be defined in exactly one owner document.
- Summary documents may link to owner documents but must not restate detailed rules.
- Named gate commands live only in `docs/operations/Spio-Verification-Matrix.md`.
- The split runbook may reference preflight and copy commands, but must not become a second verification matrix.
- Workstream files may name gates, but gate pass commands must stay in operations.
- `styio` knowledge docs may describe published compiler behavior, but they must not define `spio` compatibility policy.
- registry docs may specialize client or server responsibilities, but they must not redefine the shared registry object layout.
- security docs may define private-boundary rules, but they must not carry deployment secrets or environment-owned credentials.
- ADRs may explain an accepted policy or implementation boundary, but the normative rule must still live in its owner document.

## 4. Update Workflow

When a change happens:

- CLI or exit-code change: update governance first, then tests, then planning/operations references if needed
- argument or helper-script parameter change: update `Spio-Entry-Argument-Index.md` first, then the owning contract/script/tests
- new public workflow-boundary or implementation-scope decision: add or update an ADR in `docs/adr/` with the same change
- compatibility change: update governance plus `contracts/compat/*`, then verification coverage
- gate command change: update operations first, then any summaries that link to the gate
- migration procedure change: update operations runbook and preflight script together
- new `styio` public interface: update `docs/external/for-styio/Styio-External-Interface-Requirement-Spec.md` first, then compatibility or workflow docs as needed
- security boundary change: update `docs/security/Spio-Private-Security-Module-Contract.md` first, then the affected public contract, tests, and private-module scaffolding

## 5. Known Defects

- This model adds one more governance document to maintain.
- It reduces duplication, but it cannot prevent stale links if files move again without a coordinated pass.
