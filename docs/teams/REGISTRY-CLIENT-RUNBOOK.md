# Registry Client Runbook

**Purpose:** Provide the daily-work entrypoint for maintainers of registry consumption, cache materialization, and read-side package acquisition.

**Last updated:** 2026-04-16

## Mission

Own client-side fetch, cache, extraction, and read-root materialization behavior. This team does not own publish transports, write-origin policy, or private trust implementation.

## Owned Surface

Primary paths:

1. `src/PafioRegistryClient/`
2. `docs/registry/Pafio-Registry-Client-Contract.md`
3. `docs/registry/Pafio-Registry-Deployment-Baseline.md`
4. `tests/interop/`
5. `scripts/registry-promote.py`

Key SSOTs:

1. `Registry repository contract -> ../governance/Pafio-Registry-Repository-Contract.md`
2. `Registry client contract -> ../registry/Pafio-Registry-Client-Contract.md`
3. `Deployment baseline -> ../registry/Pafio-Registry-Deployment-Baseline.md`

## Daily Workflow

1. Read the repository contract and client contract before changing object layout assumptions or fetch semantics.
2. Keep cache-path changes and deployment-baseline docs aligned in the same delivery.
3. Re-run isolated interop checks whenever extraction or materialization behavior changes.
4. Treat client-visible fallback behavior as contract, not incidental implementation detail.

## Change Classes

1. Small: local cache helper cleanup or isolated fetch bug fix. Run targeted interop or native checks.
2. Medium: changed cache layout, extraction semantics, or read-root behavior. Update client contract and deployment notes.
3. High: public fetch protocol, registry object addressing, or split-origin read behavior change. Use cross-team review and release-profile checks.

## Required Gates

Minimum:

```bash
python3 ./scripts/registry-server-gate.py
python3 ./scripts/preflight-readiness-check.py
```

Expanded:

```bash
python3 ./scripts/submit-gate.py --profile pre-push
```

## Cross-Team Dependencies

1. Registry Server must review any shared object-layout or promotion-path change.
2. Compat / Security must review changes that touch public auth/trust boundaries or compatibility metadata.
3. Resolve / Workflow must review changes that alter package graph fields or local materialization expectations.
4. Delivery / Quality must review interop fixture or gate expectation changes.

## Handoff / Recovery

Record:

1. Read path or cache rule changed.
2. Interop fixtures and temporary roots used.
3. Promotion or deployment assumptions still pending.
4. Rollback point and next verification command.
