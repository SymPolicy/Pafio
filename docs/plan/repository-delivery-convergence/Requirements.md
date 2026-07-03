# Requirements

## Product Problem

`SymPolicy/Pafio` had planning material split across old planning directories and document names. The current requirement is a single Better Plan workspace under `docs/plan` that captures open work without preserving duplicate planning roots.

## Target Users And Workflows

- Maintainers need one project-local plan root for delivery decisions and status checks.
- Agents need machine-readable `Manifest.json` and `Checkpoints.json` state that validates with the Better Plan manifest tool.
- Reviewers need plan-local evidence that shows which previous planning sources were consolidated.

## Functional Requirements

- REQ-001: All current planning sources for `SymPolicy/Pafio` are represented under `docs/plan`.
- REQ-002: `docs/plan/Manifest.json` indexes only Plan objects, and every Plan points to a plan-local `Checkpoints.json`.
- REQ-003: Open or uncertain work extracted from prior planning sources is represented as current Better Plan acceptance criteria rather than as old checklist files.
- REQ-004: The workspace uses current project naming and does not retain separate planning roots named `docs/plan`, `docs/plan`, or `docs/plan`.
- REQ-005: Validation evidence maps every requirement to concrete repository checks.

## Non-Goals

- This refactor does not claim underlying product implementation tasks are finished.
- This refactor does not introduce compatibility paths, historical phases, or duplicate planning formats.

## Final Acceptance Targets

- `python <better-plan>/scripts/manifest_tool.py validate docs/plan` succeeds.
- Repository search finds no planning root outside `docs/plan` for consolidated plan material.
- The source inventory in `Evidence.md` lists the imported planning sources and their open-work signals.
