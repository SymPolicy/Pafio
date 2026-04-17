# Registry Server Runbook

**Purpose:** Provide the daily-work entrypoint for maintainers of publish-side registry behavior, origin-facing upload paths, and immutable package publication.

**Last updated:** 2026-04-16

## Mission

Own write-side registry publication, immutable object creation, and origin-facing server behavior. This team does not own client cache layout or private auth/account policy.

## Owned Surface

Primary paths:

1. `src/SpioRegistryServer/`
2. `src/SpioPublish/`
3. `docs/registry/Spio-Registry-Server-Contract.md`
4. `docs/registry/Spio-Registry-Deployment-Baseline.md`
5. `docs/operations/Spio-Registry-Server-Runbook.md`
6. `tests/interop/registry-server-gate.sh`

Key SSOTs:

1. `Registry repository contract -> ../governance/Spio-Registry-Repository-Contract.md`
2. `Registry server contract -> ../registry/Spio-Registry-Server-Contract.md`
3. `Registry server operations -> ../operations/Spio-Registry-Server-Runbook.md`

## Daily Workflow

1. Read the shared repository contract before changing publish-side object layout or origin roots.
2. Update the server contract and operations runbook together when deployment behavior changes.
3. Keep public publish semantics separate from private auth or policy resolution.
4. Re-run isolated publish/fetch interop checks after changing duplicate handling, promotion flow, or upload roots.

## Change Classes

1. Small: local publish helper cleanup or isolated server bug fix. Run targeted interop checks.
2. Medium: changed publish object layout, duplicate rejection behavior, or deployment-path handling. Update server and deployment docs.
3. High: write-origin protocol, split-origin publish model, or public upload contract change. Use coordinator review and expanded gate coverage.

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

1. Registry Client must review changes that alter shared object layout, promotion, or read visibility.
2. Compat / Security must review any public policy hook or security-boundary change.
3. Delivery / Quality must review gate profile or interop fixture changes.
4. Docs / Governance must review normative contract or deployment-baseline edits.

## Handoff / Recovery

Record:

1. Write origin, object layout, or duplicate behavior changed.
2. Interop roots and publish fixtures used for validation.
3. Public versus private security boundary assumptions.
4. Remaining deployment or promotion follow-up needed after handoff.
