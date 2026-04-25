# Docs / Governance Runbook

**Purpose:** Provide the daily-work entrypoint for maintainers of `spio`'s written contracts, documentation topology, ADR hygiene, planning handoff, and repository-facing governance text.

**Last updated:** 2026-04-17

## Mission

Own documentation structure, normative written contracts, planning handoff clarity, and ADR placement. This team does not replace feature teams as owners of implementation details or executable gates.

## Owned Surface

Primary paths:

1. `README.md`
2. `docs/README.md`
3. `docs/governance/`
4. `docs/rollups/`
5. `docs/history/`
6. `docs/archive/`
7. `docs/adr/`
8. `docs/planning/`
9. `docs/teams/`
10. `scripts/docs-index.py`
11. `scripts/docs-lifecycle.py`
12. `scripts/docs-audit.py`

Key SSOTs:

1. `Docs maintenance model -> ../governance/Docs-Maintenance-Model.md`
2. `Ecosystem milestone mirror -> ../planning/Styio-Ecosystem-Delivery-Master-Plan.md`
3. `File-governance mirror -> ../planning/Styio-Ecosystem-File-Governance-Alignment-Plan.md`
4. `Current state -> ../rollups/CURRENT-STATE.md`
5. `Master plan -> ../planning/Spio-Master-Plan.md`
6. `Future direction and coordination -> ../planning/Spio-Future-Direction-and-Styio-Coordination.md`

## Daily Workflow

1. Update the owner document first when a rule, contract, or planning boundary changes.
2. Keep summary docs short and linked; do not let READMEs become a second owner copy.
3. Add or update an ADR when an accepted boundary or workflow decision needs durable rationale.
4. Refresh team runbooks whenever ownership, review routing, or recovery expectations move.
5. If a cross-repo milestone or repo-exit changes, update the nightly authority first, then the local ecosystem mirror, then repo-local planning docs.
6. If docs tree topology, index generation, lifecycle metadata, or ignore-policy baseline changes, update the file-governance mirror and the docs automation scripts in the same checkpoint.

## Change Classes

1. Small: link repair, summary cleanup, or isolated wording fix. Keep owner docs unchanged when the rule did not move.
2. Medium: moved owner document, changed doc topology, or updated coordination route. Update summaries and affected runbooks together.
3. High: normative policy change, repo split guidance change, or rewritten planning boundary. Use coordinator review and cross-team confirmation.

## Required Gates

Minimum:

```bash
python3 ./scripts/docs-audit.py
python3 ./scripts/repo-hygiene-check.py --repo-root . --mode tracked
python3 ./scripts/delivery-gate.py
```

Expanded:

```bash
python3 ./scripts/submit-gate.py --profile pre-push
```

## Cross-Team Dependencies

1. Every feature team must review owner-doc changes that alter its daily workflow.
2. Delivery / Quality must review changes to gate-entrypoint or operational-doc references.
3. Compat / Security must review contract-boundary or external compiler handoff edits.
4. Registry and CLI teams must review any normative contract text that changes their public behavior.

## Handoff / Recovery

Record:

1. Owner documents changed and why they own the rule.
2. ADRs, planning docs, and runbooks updated in the same delivery.
3. Cross-team document follow-ups still pending.
4. The next owner doc that must be updated before the branch can merge.
