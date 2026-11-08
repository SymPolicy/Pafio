# Docs Maintenance Model

**Purpose:** Assign one documentation owner to every active Pafio knowledge class.

**Last updated:** 2026-10-15

## Owner Map

- `governance/`: normative Pafio interfaces and policy
- `adr/`: accepted decisions and rationale
- `plan/`: completed delivery evidence
- `plans/`: active programme, milestone sequencing, and pending state
- `registry/`: registry client behavior only
- `security/`: local supply-chain and subprocess boundaries
- `external/for-styio/`: published compiler handoff
- `operations/`: repository-local artifact and delivery procedures
- `teams/`: review routing

Styio and Styio Platform contracts are linked from their owning repositories;
Pafio does not keep a second service or compiler specification.

## Update Rule

Change the owner document and executable tests in the same closure. Regenerate
indexes after tree changes. A completed plan decision must be promoted to an
ADR or governance contract before obsolete plan or history text is removed.
Generated indexes and rollups summarize owners but never redefine them.

Automation entrypoints are `scripts/docs-index.py`,
`scripts/docs-lifecycle.py`, and `scripts/docs-audit.py`.
