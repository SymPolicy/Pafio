# History Docs

**Purpose:** Hold active daily recovery notes for `pafio`; these files track interrupted checkpoints, recovery commands, and rollback points before older provenance moves to `docs/archive/history/`.

**Last updated:** 2026-04-17

## Scope

- active daily handoff notes
- recovery commands and rollback points
- checkpoint-sized provenance that is still operationally relevant

## Maintenance Rule

Use `YYYY-MM-DD.md` file names only. Keep the newest operationally relevant daily note active here, and archive older raw provenance through `python3 scripts/docs-lifecycle.py refresh` once archive tracking expands beyond the bootstrap manifest.
