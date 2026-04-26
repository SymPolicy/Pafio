# Coordination Runbook

**Purpose:** Provide the daily-work coordination entrypoint for `spio`; explicitly maintain team ownership, review routing, escalation paths, and checkpoint discipline without replacing SSOT planning or governance documents.

**Last updated:** 2026-04-24

## Mission

Keep module boundaries, review routing, and checkpoint recovery clear across
`spio` local-first package-manager workflow, registry client delivery,
compiler-facing contracts, offline package portability, platform handoff, and
docs/delivery operations.

## Module Map

```text
Docs / Delivery -> Core / Workflow
Docs / Delivery -> Registry / Publish
Docs / Delivery -> Styio / Contracts
Styio / Contracts -> Core / Workflow
Registry / Publish -> Core / Workflow
Styio / Contracts -> styio-platform handoff
```

## Ownership Table

| Team | Primary runbook | Main surface | Required review trigger |
|------|-----------------|--------------|-------------------------|
| Core / Workflow | [CORE-WORKFLOW-RUNBOOK.md](./CORE-WORKFLOW-RUNBOOK.md) | native CLI, manifests, lockfiles, resolver, local import/export, package workflow, platform compatibility shims | CLI shape, manifest/lock behavior, offline package workflow change |
| Registry / Publish | [REGISTRY-PUBLISH-RUNBOOK.md](./REGISTRY-PUBLISH-RUNBOOK.md) | registry client docs, offline package expectations, publish/fetch flow | registry transport, publish client semantics, local package portability change |
| Styio / Contracts | [STYIO-CONTRACTS-RUNBOOK.md](./STYIO-CONTRACTS-RUNBOOK.md) | `contracts/`, compiler handoff, local Styio environment contract, external `styio` interface expectations, `styio-platform` handoff | machine contract, compatibility payload, compile-plan/platform boundary change |
| Docs / Delivery | [DOCS-DELIVERY-RUNBOOK.md](./DOCS-DELIVERY-RUNBOOK.md) | docs tree, repo hygiene, docs gate, delivery-facing docs | docs topology, gate shape, repo hygiene rule, workflow entrypoint change |

## Review Matrix

1. Core CLI, manifest, offline package, local import/export, or build flow changes require Core / Workflow review.
2. Registry publish/fetch/promotion or remote transport client changes require Registry / Publish review.
3. External compiler handshake, `contracts/`, compiler-facing CLI contract changes, or platform migration handoff changes require Styio / Contracts review.
4. Docs tree, gate wiring, or workflow entrypoint changes require Docs / Delivery review.
5. Any change that crosses those boundaries needs both affected owners in the same checkpoint.

## Escalation Rules

1. If implementation and planning disagree, escalate to `docs/planning/`.
2. If CLI or compiler contract wording disagrees, escalate to `docs/governance/` and `docs/external/for-styio/`.
3. If registry delivery shape disagrees, escalate to `docs/registry/`.
4. If platform service ownership disagrees, escalate to `docs/planning/Spio-Platform-Migration-Handoff.md` and the downstream `styio-platform` docs.
5. If workflow or delivery process disagrees, escalate to `docs/assets/workflow/`.

## Checkpoint Policy

1. Keep risky work within one to three day mergeable batches.
2. Structural changes must update the owning runbook and the affected workflow docs in the same checkpoint.
3. Interrupted work must leave a recoverable note in the owning doc or active plan, not just chat state.

## Release / Cutover Gates

| Cutover | Minimum gate |
|---------|--------------|
| Core workflow or native CLI | `./scripts/checkpoint-health.sh` |
| Registry client delivery path | `./scripts/checkpoint-health.sh` and relevant registry acceptance commands |
| Compiler handoff / contracts | `./scripts/checkpoint-health.sh --styio-bin /absolute/path/to/styio` |
| Offline package / local import-export | `./scripts/checkpoint-health.sh` plus local resolver/cache coverage |
| Platform service handoff | `./scripts/docs-gate.sh`, platform migration docs, and downstream `styio-platform` gates |
| Docs or delivery process | `./scripts/docs-gate.sh` and `python3 scripts/repo-hygiene-gate.py --mode tracked` |

## Handoff / Recovery

1. Record the affected owner runbooks and workflow docs.
2. Record the next command and remaining blocker if a checkpoint stops mid-flight.
3. State whether an external `styio` binary is required for the next recovery step.
