# Rollups Docs

**Purpose:** Hold compressed active summaries for `spio`; these files provide the default reading order for current state and active gaps without forcing readers through raw planning and history first.

**Last updated:** 2026-04-17

## Scope

- current-state summary
- active gap ledger
- short operational summaries that stay relevant across more than one checkpoint

## Maintenance Rule

Rollups summarize active truth and must link back to owner documents. They do not replace governance, operations, planning, or ADR sources.
