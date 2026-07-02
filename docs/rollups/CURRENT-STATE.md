# Current State

**Purpose:** Provide the default active-state summary for `pafio`, including the current governance posture, product posture, and next file-governance checkpoint before deeper planning docs are consulted.

**Last updated:** 2026-04-17

## Summary

1. `pafio` is strong on engineering gates, package workflow, compiler compatibility gates, and the shared required-pattern / fixture-negate baseline.
2. `pafio` has now closed the file-governance bootstrap work that used to separate it from `styio-nightly`: `history/archive/rollups`, generated indexes, docs audit, lifecycle validation, and docs-aware submit/delivery checks are all live.
3. The current active governance checkpoint is `FG4`: keep this baseline synchronized with `styio-nightly` and `styio-view` instead of letting each repo drift back to repo-specific rules.
4. `pafio` 现在还拥有一条真正的 cross-repo sample workflow gate：它已经覆盖 managed toolchain switch（真实 `styio` 安装、切到第二个 managed compiler 身份、再切回并继续 workflow）、多包 workspace 下 `run/test/publish` 的显式 `--package` 选择与歧义保护、vendored offline（`vendor -> clear PAFIO_HOME -> fetch/check/run --offline`），以及 registry-hosted source（本地 registry `publish -> republish conflict -> fetch -> project-graph -> check -> run`），而不再只依赖 native fixture。

## Read Order

1. `NEXT-STAGE-GAP-LEDGER.md`
2. `../planning/Styio-Ecosystem-File-Governance-Alignment-Plan.md`
3. `../planning/Pafio-Master-Plan.md`
4. `../operations/Pafio-Verification-Matrix.md`
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
