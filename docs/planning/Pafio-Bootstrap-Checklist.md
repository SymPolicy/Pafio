# Pafio Bootstrap Checklist

**Purpose:** Provide a compact bootstrap summary without duplicating the detailed task definitions and gate commands owned elsewhere.

**Last updated:** 2026-04-09

## Summary

The bootstrap stage is considered prepared when these areas all have an owner, a TODO list, and a gate:

- repository independence
- manifest and lockfile core
- compatibility and machine handshake
- resolver and cache
- CLI workflow
- external compiler integration
- verification infrastructure
- repository split runbook

## Detailed Owners

Detailed workstream TODOs live in:

- `Pafio-Workstreams-and-TODOs.md`

Detailed phase ordering lives in:

- `Pafio-Master-Plan.md`

Detailed gate commands and pass criteria live in:

- `../operations/Pafio-Verification-Matrix.md`

Detailed migration procedure lives in:

- `../operations/Pafio-Repo-Split-Runbook.md`

## Final Bootstrap Closure

Bootstrap planning is only considered closed when:

- `styio_contract_compat_gate` is green
- `pafio_extractability_gate` is green
- `styio_pafio_dual_maintenance_gate` has a documented preflight entry point

## Known Defect

- This file is intentionally only a summary; anyone trying to treat it as the source of truth for detailed tasks will recreate the original drift problem.
