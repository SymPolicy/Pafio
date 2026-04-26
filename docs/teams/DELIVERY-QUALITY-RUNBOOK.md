# Delivery / Quality Runbook

**Purpose:** Provide the daily-work entrypoint for maintainers of tests, submit gates, delivery export checks, and performance baselines.

**Last updated:** 2026-04-16

## Mission

Own executable verification, performance baselines, delivery export checks, and fixture discipline across the repository. This team does not redefine policy or feature semantics; it closes acceptance loops for them.

## Owned Surface

Primary paths:

1. `tests/`
2. `scripts/`
3. `docs/operations/`
4. `.github/workflows/submit-gate.yml`

Key SSOTs:

1. `Verification matrix -> ../operations/Spio-Verification-Matrix.md`
2. `Tests README -> ../../tests/README.md`
3. `Scripts README -> ../../scripts/README.md`

## Daily Workflow

1. Start from the named gate in the verification matrix rather than rebuilding gate logic from CI files.
2. Keep scripts, fixtures, and pass conditions aligned in the same change.
3. When a gate shape changes, update the verification matrix first and treat every summary link as downstream.
4. Re-run the smallest relevant gate locally before escalating to submit or release profiles.

## Change Classes

1. Small: isolated fixture update, test helper cleanup, or local gate wording fix. Run the owning gate directly.
2. Medium: changed gate pass condition, perf threshold, or export rule. Update operations docs and affected fixtures.
3. High: submit profile shape, delivery export policy, or broad fixture-oracle change. Use cross-team review and expanded validation.

## Required Gates

Minimum:

```bash
python3 ./scripts/check_no_binaries.py --repo-root . --mode tracked
python3 ./scripts/repo-hygiene-check.py --repo-root . --mode tracked
python3 ./scripts/perf-gate.py
python3 ./scripts/delivery-gate.py
```

Expanded:

```bash
python3 ./scripts/submit-gate.py --profile pre-push
python3 ./scripts/submit-gate.py --profile ci --json
```

## Cross-Team Dependencies

1. Every feature team must review oracle changes in its owned surface.
2. Compat / Security must review release-profile changes that add or remove external compiler checks.
3. Docs / Governance must review operations-doc ownership or gate-entrypoint changes.
4. Registry teams must review interop gate changes that alter publish or fetch expectations.

## Handoff / Recovery

Record:

1. Gates or fixtures changed.
2. Local commands already run and their profile.
3. Baseline files, export filters, or perf thresholds still pending.
4. Whether another team still needs to accept the new oracle.
