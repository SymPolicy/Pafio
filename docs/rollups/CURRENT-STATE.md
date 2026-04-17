# Current State

**Purpose:** Provide the default active-state summary for `spio`, including the current governance posture, product posture, and next file-governance checkpoint before deeper planning docs are consulted.

**Last updated:** 2026-04-17

## Summary

1. `spio` is strong on engineering gates, package workflow, compiler compatibility gates, and the shared required-pattern / fixture-negate baseline.
2. `spio` has now closed the file-governance bootstrap work that used to separate it from `styio-nightly`: `history/archive/rollups`, generated indexes, docs audit, lifecycle validation, and docs-aware submit/delivery checks are all live.
3. The current active governance checkpoint is `FG4`: keep this baseline synchronized with `styio-nightly` and `styio-view` instead of letting each repo drift back to repo-specific rules.

## Read Order

1. `NEXT-STAGE-GAP-LEDGER.md`
2. `../planning/Styio-Ecosystem-File-Governance-Alignment-Plan.md`
3. `../planning/Spio-Master-Plan.md`
4. `../operations/Spio-Verification-Matrix.md`
5. `../governance/Docs-Maintenance-Model.md`

## Recovery Baseline

```bash
python3 scripts/docs-index.py --write
python3 scripts/docs-lifecycle.py refresh
python3 scripts/docs-audit.py
python3 scripts/repo-hygiene-check.py --repo-root . --mode tracked
python3 scripts/delivery-gate.py
python3 scripts/submit-gate.py --profile pre-push
```
