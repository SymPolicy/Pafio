# Resolve / Workflow Runbook

**Purpose:** Provide the daily-work entrypoint for maintainers of dependency resolution, local workflow orchestration, graph rendering, and compile-plan preparation.

**Last updated:** 2026-04-17

## Mission

Own resolver behavior, workflow planning, graph materialization, and local orchestration state. This team does not own CLI policy text, registry transport, or public security rules.

## Owned Surface

Primary paths:

1. `src/SpioResolve/`
2. `src/SpioPlan/`
3. `src/SpioWorkflow/`
4. `src/SpioCore/`
5. `src/SpioTree/`
6. `src/SpioVendor/`
7. `src/SpioPack/`
8. `docs/planning/Spio-Workstreams-and-TODOs.md`

Key SSOTs:

1. `Ecosystem milestone mirror -> ../planning/Styio-Ecosystem-Delivery-Master-Plan.md`
2. `Master plan -> ../planning/Spio-Master-Plan.md`
3. `Workstreams -> ../planning/Spio-Workstreams-and-TODOs.md`
4. `Verification matrix -> ../operations/Spio-Verification-Matrix.md`

## Daily Workflow

1. Start from the active workstream and the owning contract that the workflow consumes.
2. Change resolver, plan, and rendered graph outputs together so graph or workflow views do not lag behind semantics.
3. Update affected fixtures before changing expected workflow success payloads.
4. Re-check whether a plan output is a public contract or only an internal step before publishing a format change.

## Change Classes

1. Small: local resolver fix, graph display cleanup, or internal helper change. Run targeted native checks.
2. Medium: dependency selection change, compile-plan payload change, or workflow summary change. Update fixtures and related docs.
3. High: published plan schema, workflow contract, or multi-command orchestration change. Use coordinator review and submit-gate coverage.

## Required Gates

Minimum:

```bash
./scripts/native-check.sh
python3 ./scripts/bootstrap-check.py
```

Expanded:

```bash
python3 ./scripts/submit-gate.py --profile pre-push
```

## Cross-Team Dependencies

1. CLI / Manifest must review changes that alter command payloads or manifest interpretation.
2. Compat / Security must review any change that affects `compile-plan` or published compatibility contracts.
3. Registry teams must review changes that alter package graph fields consumed by fetch or publish flows.
4. Delivery / Quality must review fixture or workflow gate expectation changes.

## Handoff / Recovery

Record:

1. Resolver rule, plan payload, or workflow stage changed.
2. Fixture roots or graph renders updated.
3. Gates already run and remaining blocking command.
4. Whether downstream CLI, registry, or compatibility consumers have already been adapted.
