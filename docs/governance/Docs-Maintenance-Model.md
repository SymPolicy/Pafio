# Docs Maintenance Model

**Purpose:** Keep `spio` documentation modular and maintainable by assigning a single owner module to each kind of knowledge.

**Last updated:** 2026-04-17

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

### `docs/rollups/`

Owns:

- compressed active-state summaries
- current-state reading order
- active gap ledgers

Must not:

- redefine normative policy
- replace planning detail, ADR rationale, or operational gate commands
- become a shadow archive

### `docs/history/`

Owns:

- active dated recovery notes
- rollback points and restart commands
- checkpoint-local provenance that is still operationally relevant

Must not:

- replace rollups as the default entrypoint
- replace archive as the long-term provenance store
- become a second planning backlog

### `docs/archive/`

Owns:

- archived provenance
- archive manifest and ledger
- lifecycle metadata for moved documentation

Must not:

- hide unresolved active owner docs
- replace rollups or history as the active surface
- redefine policy or planning priorities

### `docs/operations/`

Owns:

- acceptance gates
- executable verification entry points
- registry server runbook
- repository split runbook

Must not:

- redefine policy that belongs to governance
- redefine backlog decomposition that belongs to planning

### `docs/teams/`

Owns:

- daily-work runbooks
- ownership routing
- review and handoff entrypoints
- checkpoint-facing recovery summaries

Must not:

- define normative contract text
- duplicate gate command details from operations
- replace planning or governance as the owner of a rule

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
- registry repository contract: `docs/governance/Spio-Registry-Repository-Contract.md`
- private security boundary: `docs/security/Spio-Private-Security-Module-Contract.md`
- registry client contract: `docs/registry/Spio-Registry-Client-Contract.md`
- registry server contract: `docs/registry/Spio-Registry-Server-Contract.md`
- registry deployment baseline: `docs/registry/Spio-Registry-Deployment-Baseline.md`
- design and implementation decision records: `docs/adr/INDEX.md`
- overall roadmap: `docs/planning/Spio-Master-Plan.md`
- three-repo milestone mirror and program exits: `docs/planning/Styio-Ecosystem-Delivery-Master-Plan.md`
- three-repo file-governance alignment mirror: `docs/planning/Styio-Ecosystem-File-Governance-Alignment-Plan.md`
- current active-state summary: `docs/rollups/CURRENT-STATE.md`
- active gap ledger: `docs/rollups/NEXT-STAGE-GAP-LEDGER.md`
- active recovery notes: `docs/history/`
- archive lifecycle ledger: `docs/archive/ARCHIVE-LEDGER.md`
- stage review and future feature priorities: `docs/planning/Spio-Stage-Review-and-Future-Features.md`
- future direction and cross-team coordination: `docs/planning/Spio-Future-Direction-and-Styio-Coordination.md`
- workstream TODOs: `docs/planning/Spio-Workstreams-and-TODOs.md`
- bootstrap summary: `docs/planning/Spio-Bootstrap-Checklist.md`
- gate definitions and commands: `docs/operations/Spio-Verification-Matrix.md`
- registry server operational validation: `docs/operations/Spio-Registry-Server-Runbook.md`
- split procedure: `docs/operations/Spio-Repo-Split-Runbook.md`
- team ownership and review routing: `docs/teams/COORDINATION-RUNBOOK.md`
- `styio` developer knowledge: `docs/styio/Styio-for-Spio-Developers.md`
- `styio` handoff interface spec: `docs/styio/Styio-External-Interface-Requirement-Spec.md`
- `styio` public interface expectations: `docs/styio/Styio-Public-Interface-Roadmap.md`

## 3. Drift Prevention Rules

- A rule must be defined in exactly one owner document.
- Summary documents may link to owner documents but must not restate detailed rules.
- Named gate commands live only in `docs/operations/Spio-Verification-Matrix.md`.
- The split runbook may reference preflight and copy commands, but must not become a second verification matrix.
- Workstream files may name gates, but gate pass commands must stay in operations.
- Team runbooks may describe owned surfaces, review triggers, and handoff expectations, but they must not become the owner of CLI, registry, compatibility, or security rules.
- Rollups may summarize active state and gaps, but they must not become the owner of policy or gate commands.
- History files may record recovery notes, but they must not become the only place a rule or plan is defined.
- Archive files may preserve provenance, but they must not become a dumping ground for unresolved active documents.
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
- docs-tree topology, index-generation, or lifecycle change: update this model first, then the file-governance mirror, then affected READMEs, runbooks, and scripts
- migration procedure change: update operations runbook and preflight script together
- ownership, review-route, or handoff change: update the affected `docs/teams/*.md` file and `docs/teams/COORDINATION-RUNBOOK.md`
- cross-repo milestone, repo-exit, or checkpoint-ID change: update the authoritative nightly plan first, then `docs/planning/Styio-Ecosystem-Delivery-Master-Plan.md`, then the affected local planning, team, and handoff docs
- docs tree topology, index-generation, lifecycle, ignore-policy, or fixture-negate rule change: update the authoritative nightly file-governance plan first, then `docs/planning/Styio-Ecosystem-File-Governance-Alignment-Plan.md`, then the affected governance, planning, team, and operations docs
- new `styio` public interface: update `docs/styio/Styio-External-Interface-Requirement-Spec.md` first, then compatibility or workflow docs as needed
- security boundary change: update `docs/security/Spio-Private-Security-Module-Contract.md` first, then the affected public contract, tests, and private-module scaffolding

## 4.1 File-Governance Automation

The current file-governance automation surface is:

- `scripts/docs-index.py`
- `scripts/docs-lifecycle.py`
- `scripts/docs-audit.py`

These scripts do not own policy, but they enforce freshness, lifecycle consistency, and required docs skeletons for the tracked tree.

## 5. Known Defects

- This model adds one more governance document to maintain.
- It reduces duplication, but it cannot prevent stale links if files move again without a coordinated pass.
