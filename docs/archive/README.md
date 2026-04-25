# Archive Docs

**Purpose:** Hold archived `spio` documentation provenance and lifecycle metadata; active summaries stay in `docs/rollups/`, active recovery notes stay in `docs/history/`, and only retired raw material belongs here.

**Last updated:** 2026-04-17

## Scope

- archive manifest and ledger
- archived daily history snapshots
- future archived planning or review provenance once lifecycle coverage expands

## Maintenance Rule

Use `python3 scripts/docs-lifecycle.py refresh` to keep `ARCHIVE-LEDGER.md` synchronized with `ARCHIVE-MANIFEST.json`. Do not archive active owner documents here just to hide unresolved drift.
