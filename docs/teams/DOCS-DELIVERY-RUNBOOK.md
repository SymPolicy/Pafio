# Docs / Delivery Runbook

**Purpose:** Keep Pafio product contracts, generated indexes, planning state, and delivery evidence aligned.

**Last updated:** 2026-10-23

## Mission

Maintain one current product model across repository entry docs, governance, ADRs,
the Better Plan workspace, and release evidence.

## Owned Surface

1. Repository README and policy documents.
2. `docs/` collections and generated indexes.
3. Documentation, lifecycle, audit, hygiene, and delivery gates.
4. Public handoff documentation after executable owner contracts pass.

## Daily Workflow

1. Change the normative governance contract before its summaries.
2. Record durable ownership changes in an ADR.
3. When the authorized work uses Better Plan, follow the active installed skill's
   current schema and tools. Keep observed repository facts separate from delivery
   authority, and preserve completed state and evidence as history.
4. Do not duplicate planning lifecycle commands, role counts, or state topology in
   this runbook. Routine authorized maintenance does not require creating a Plan.
5. Regenerate indexes after the docs tree changes.
6. Keep the public Pafio command inventory aligned with executable help.
7. Publish no site or release wording before the fixed-revision product matrix passes.
8. Remove superseded owner documents during a clean break; do not retain legacy
   names, compatibility notes, or permanent migration gates as active policy.
9. Keep post-commit instructions on public executable gates and repository-
   relative placeholders; never publish private machine paths or removed
   sibling-repository script entrypoints.
10. When the fixed-revision product matrix passes, close the Better Plan and gap
   ledger together. Keep branch promotion as an explicit maintainer handoff
   unless repository promotion is separately authorized.
11. Name the exact platform and source revision in release wording. Treat Linux,
    macOS, and Windows artifacts as independent publications; never infer one
    platform's readiness from another platform's gate or from shared `nightly`
    branch promotion.
12. Keep the fresh-host bootstrap aligned with every delivery tool used by CI,
    including `rsync` for extractability and exported-tree checks.
13. Keep documented global help aliases aligned with executable CLI output and
    its short-form regression test.
14. Route current programme requirements and acceptance through `docs/plans/`;
    retain independent historical workspaces in `docs/plan/`. Handoffs distinguish
    commands actually run, valid reused evidence and host check results. An
    upstream observation failure cannot be waived by ordinary workflow passes.

## Change Classes

1. Small: link, wording, or generated-index refresh.
2. Medium: contract, plan topology, gate, or handoff update.
3. High: product ownership, public command, schema, or release-boundary change.

## Required Gates

```bash
python3 scripts/docs-audit.py
python3 scripts/repo-hygiene-gate.py --mode tracked
git diff --check
```

Select product-focused and final release gates for the authorized scope through
the owning workflow. Follow [Post-Commit CI Checks](../specs/POST-COMMIT-CI-CHECKS.md)
for one final complete regression after source review, repairs, and focused
verification. When a gate fails, diagnose its stage, repair ordinary in-scope
defects, and run the necessary follow-up checks. Request a developer decision
only when the repair would change the objective, a published contract, or the
risk boundary. Do not repeat a gate simply because another workflow cites the
same evidence.

`PAFIO_GATE_LOG_DIR` enables complete ignored submit/delivery child records and
performance samples, including output omitted from short console summaries.
The milestone final entrypoint records the dirty candidate and explicit command
before yielding execution to the host. Use the full retained logs and child exits
for acceptance; a console tail is only a reference. Keep the CI profile, ordered
checks, first-failure stop and performance thresholds unchanged. Recovery selects
only diagnosed affected/remaining checks in the milestone entrypoint.

The export policy excludes root-level `build-*` directories before copying.
Keep that pattern rooted so similarly named documentation and test assets remain
in the delivery. Source build directories are preserved; exported trees still
must pass the existing structural, binary and hygiene checks.

## Cross-Team Dependencies

1. Core / Workflow reviews terminal and local workflow behavior.
2. Registry / Publish reviews client-versus-Platform ownership.
3. Styio / Contracts reviews compiler and machine-contract handoffs.
4. Platform and Vityo owners review their own adapters before public documentation.

## Handoff / Recovery

Record the affected owner contract, last passing focused gate, generated indexes
still pending, and the exact revision that needs repair. Never include credentials,
private machine paths, user information, or backend runtime payloads.

2026-09-04: Added a single-purpose conditional handoff plan under
`docs/external/for-styio/` and refreshed the generated indexes. The document
records why no Pafio implementation is currently needed and defines the fixture
evidence required before a separate delivery could be authorized.

2026-09-05: Documented the opt-in observable static snapshot emission
passthrough in the CLI contract, the compile-plan v1 contract README, and the
Styio handoff docs, and refreshed the generated indexes. The consumer locates the
Styio receipt and snapshot artifact through the existing `plan` envelope fields.

2026-09-05: Documented the `--observable-parent-snapshot <path>` passthrough and
the optional `parent_snapshot_path` field in the same contract and handoff docs,
including that Styio emits `<output-stem>.observable-delta.json` or records a
`full_snapshot_required` receipt reason, both found through `receipt.json`.

2026-09-05: Documented the `--emit-runtime-observation[=<version>]` passthrough,
its four dependent flags, and the optional `emit.runtime_observation` field in
the same contract and handoff docs, including that Styio owns every default and
validation and lists the receipt-named runtime-events v2 JSONL artifact in
`receipt.json`.

2026-09-06: Stopped empty generated indexes from stamping today's date, which
made `docs/plan/INDEX.md` and `docs/security/INDEX.md` fail CI across timezones.
Empty collections now inherit `README.md`'s last-updated date, and Better Plan
group directories are indexed through `Architecture.md` when they have no
`README.md` or `INDEX.md`.

2026-09-08: Documented the native Windows process implementation and its
portable process-contract test target. The manual `native_process_only` CI
input validates the Windows CLI and process contracts before the final complete
regression within existing adaptation authorization; it does not establish
Linux, macOS, or release readiness. Preserve
the request's existing timeout and process-group semantics across platforms.

2026-10-15: Recorded the plan-workspace split from the task-resource delivery
milestone. ADR-0009 owns the decision: `docs/plans/` is the internal
coordination workspace holding the active programme, requirements, and
per-milestone state — it is kept out of the published tree — while
`docs/plan/` retains completed delivery evidence. ADR-0001's decision text,
the ADR index conventions, the Docs-Maintenance-Model owner map, and the
README developer entry now name the new layout.

2026-10-15: Recorded the coordinator join and delivery closure for the
task-resource milestone. The joined evidence is the expanded integration gate
on the current candidate (bounded per-invocation retention), the same-candidate
cross-repo E2E run, and the bounded-retention probe record kept in the ignored
evidence tree; the milestone delivery state carries the join result and its
boundaries. The registered close-out review executed as host, and the review
items invalidated by baseline and repair result changes were resolved against
the joined evidence.

2026-10-23: Completed the `docs/plans/` internal-workspace split in the gate
surface: `docs-audit.py` no longer requires the internal collection, the
delivery export excludes `docs/plans/` via artifact policy, and the delivery
gate now runs the exported copies of `docs-audit.py` and `native-check.sh`
from `export_root` so each step validates the published tree rather than the
source checkout. The public README reading order now links the published
`docs/plan/` collection instead of the internal workspace path.
