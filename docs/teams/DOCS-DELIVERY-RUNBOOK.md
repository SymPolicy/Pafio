# Docs / Delivery Runbook

**Purpose:** Provide the daily-work entrypoint for `pafio` docs tree, repo hygiene, docs gate, and delivery-facing workflow documentation.

**Last updated:** 2026-05-02

## Mission

Own docs topology, generated indexes, gate wiring, delivery-facing entrypoints,
offline package docs, local import/export docs, and platform handoff docs
without redefining planning, registry, compiler, or service contract semantics.

## Owned Surface

1. `README.md`
2. `LICENSE`
3. `LICENSE-POLICY.md`
4. `DEPENDENCY-USAGE.md`
5. `.github/workflows/local-ci-gate.yml`
6. `docs/`
7. `docs/external/`
8. `docs/audit/`
9. `scripts/audit-gate.sh`
10. `scripts/docs-index.py`
11. `scripts/docs-lifecycle.py`
12. `scripts/docs-audit.py`
13. `scripts/repo-hygiene-gate.py`
14. `scripts/team-docs-gate.py`
15. `scripts/docs-gate.sh`
16. `scripts/delivery-gate.sh`
17. `scripts/ecosystem-cli-doc-gate.py`
18. `scripts/install-pafio.sh`
19. `scripts/repo-hygiene-check.py`
20. `scripts/submit-gate.py`
21. `scripts/perf-gate.py`
22. `scripts/delivery-gate.py`

## Daily Workflow

1. Keep repository-level build, docs, and delivery entrypoints consistent.
2. Regenerate `INDEX.md` files after docs-tree changes.
3. Keep workflow docs in `docs/assets/workflow/` aligned with the actual scripts.
4. Keep the shared `styio-pafio` / `styio-nightly` toolchain baseline explicit in docs and CI: Debian 13, LLVM 18.1.x, CMake/CTest 3.31.6, and Python 3.13.5.
5. Keep the official command grammar consistent across docs: `pafio use <mode>`, `pafio set <subject> as <value>`, `pafio sync`, `pafio project-graph --json`, `pafio install styio@latest`, `pafio cloud status --json`, `pafio cloud plan --json`, and `pafio tool status --json`.
6. Keep repo entry docs and closure docs aligned: `README.md`, `docs/BUILD-AND-DEV-ENV.md`, `docs/plan/repository-delivery-convergence/Evidence.md`, `docs/plan/repository-delivery-convergence/Evidence.md`, `docs/plan/repository-delivery-convergence/Evidence.md`, `docs/operations/Pafio-Verification-Matrix.md`, `docs/operations/Pafio-Cloud-Compile-Stress-Runbook.md`, and `docs/operations/Pafio-Repo-Split-Runbook.md` must agree on wrapper-vs-binary entrypoints, current implementation status, and root-relative command paths.
7. When `docs/governance/Pafio-CLI-Contract.md` changes source-build wording, run the cross-repo ecosystem CLI doc gate from `styio-nightly` and keep its fixed source-build needles exact.
8. Keep [../specs/POST-COMMIT-CI-CHECKS.md](../specs/POST-COMMIT-CI-CHECKS.md) aligned with actual GitHub Actions monitoring practice whenever commit, push, or CI handoff rules change.
9. Keep sibling-repository handoff docs under `docs/external/for-*` or explicit planning handoff docs; do not recreate root-level external handoff collections.
10. Keep `docs/plan/Pafio-Platform-Migration-Handoff.md` aligned with downstream `styio-platform` docs when server/platform ownership moves.
11. Keep `docs/governance/Pafio-Local-Offline-Package-Contract.md` aligned with README and registry docs when offline package or local import/export wording changes.
12. Keep top-level Apache-2.0 license, source-distribution policy, and dependency usage-boundary evidence aligned with `styio-audit`.
13. Treat regenerated `docs/audit/` reports as evidence snapshots: update ownership metadata and indexes when they move, but leave defect status changes to code/test gate evidence.
14. Keep [../specs/TECHNOLOGY-COMPONENT-INVENTORY.md](../specs/TECHNOLOGY-COMPONENT-INVENTORY.md) aligned with `styio-audit` whenever the technology stack, internal components, open-source components, dependency manifests, Apache-2.0 evidence, or commercial-risk boundaries change.
15. For registry-management documentation changes, require explicit coverage of publish, verify, mirror handoff, offline behavior, cache reuse, and public/private security boundary before closing docs/audit work.
16. Maintain GitHub merge gates through Rulesets rather than legacy classic branch protection; audit effective branch rules when required status-check governance changes, and keep `local-ci-gate` as the pafio repository's own CI status-check surface rather than treating it as the shared upstream `styio-ci-gate` ecosystem resource gate.
17. Keep `local-ci-gate` sibling checkouts on downstream `nightly` branches when the downstream repositories have collapsed their branch set to `nightly`.
18. Keep `docs/governance/Pafio-Entry-Argument-Index.md`, `docs/governance/Docs-Maintenance-Model.md`, and `docs/operations/Pafio-Verification-Matrix.md` aligned whenever delivery scripts or submit gates change.
19. Keep `scripts/delivery-gate.sh` as the delivery-facing entrypoint with a
    safe default. The no-argument command must cover dirty worktree changes
    and PR push ranges, infer the fork-parent base when possible, and fail
    when it cannot infer a delivery base.
20. Keep `staged` checks as low-level hook/debugging tools only; do not use an
    empty staged diff as evidence that a PR or local delivery is ready.

## Change Classes

1. Small: link fixes, README cleanup, or index refreshes.
2. Medium: docs tree, `docs/audit/` evidence snapshots, `docs/external/` handoff routing, gate wiring, workflow entrypoint changes, curl installer wording, source-build contract wording, offline package wording, local import/export wording, registry-management audit coverage, post-push CI checking rules, technology/component inventory updates, or platform handoff updates.
3. High: ownership boundary or delivery-floor policy changes.

## Required Gates

```bash
./scripts/docs-gate.sh
./scripts/audit-gate.sh
python3 scripts/submit-gate.py --profile ci --json
python3 scripts/perf-gate.py
python3 scripts/repo-hygiene-check.py --mode tracked
python3 scripts/repo-hygiene-gate.py --mode tracked
python3 scripts/delivery-gate.py --json
./scripts/delivery-gate.sh --skip-health
```

## Cross-Team Dependencies

1. Core / Workflow reviews workflow entrypoint changes.
2. Styio / Contracts reviews ecosystem doc or machine-contract wording changes.
3. Registry / Publish reviews registry runbook or gate changes.
4. Styio / Contracts reviews `styio-platform` handoff wording.

## Handoff / Recovery

Record the affected owner docs, generated indexes still pending, and which gate or workflow entrypoint still needs follow-up.
