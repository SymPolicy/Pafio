# Styio Observable Identity Handoff Plan

**Purpose:** Record the conditional Pafio handoff for Styio observable-language identity without scheduling premature package-manager work.

**Last updated:** 2026-10-11

**Status:** Conditional, not authorized, not started, and dormant unless the activation gate in this document is met. The identity handoff is distinct from the delivered emission passthrough described in section 1.1.

## 1. Current Decision

No Pafio implementation stage is required for Styio's first public observable topology snapshot.

The published `compile-plan v1` already provides the candidate identity inputs needed by the compiler:

1. `packages[].id` and `packages[].name`,
2. each package's `root_dir` and declared targets,
3. `entry.package_id`, `entry.target_kind`, `entry.target_name`, and `entry.file`.

Styio should first derive its public logical identity from the published package name together with the selected target or entry path relative to the matching package root. Absolute `workspace_root`, `root_dir`, `manifest_path`, and entry-file paths are transport inputs only; they must never be serialized as persistent project or semantic identity. Content hashes, local cache keys, and compiler object IDs are also not substitutes for public logical identity.

A direct single-file compilation that has no package contract remains anonymous. Pafio must not fabricate a package identity for it.

### 1.1 Delivered Emission Passthrough

PLAN-004 models snapshot publication as an absent-by-default compile-plan emission request, so a caller needs a way to ask Pafio to include it. That passthrough is delivered and is not identity work:

1. `compile-plan v1` gains the optional `emit.observable_static_snapshot` object with `schema_version` (integer, minimum 1) and `required_capabilities` (sorted, unique strings, possibly empty); `emit.required` and every other field are unchanged.
2. `pafio check|build|run|test` accept `--emit-observable-static-snapshot[=<schema-version>]` (default `1`) and the repeatable `--observable-capability <name>`. Without the flag semantic inputs and cache identity are unchanged; output paths are invocation-owned.
3. Pafio forwards the request verbatim. Styio validates the schema version and capability names against its own `--machine-info=json` advertisement, publishes `<output-stem>.observable-static-snapshot.json` through its artifact-output/receipt path, and lists it in `<plan.build_root>/receipt.json`. Consumers locate both through the existing `plan` object of the Pafio success envelope.
4. Pafio still does not mint identifiers, inspect snapshots, or add identity fields; the activation gate below is untouched.

Stage S2 (delta/lineage) adds one more transport input to the same object, again without identity work:

5. `emit.observable_static_snapshot` gains the optional `parent_snapshot_path` string, filled only from `--observable-parent-snapshot <path>` (which requires the emission flag). It names the previous snapshot artifact as an absolute, lexically normalized filesystem path; a relative value is resolved against `workspace_root`. Like `workspace_root` it is transport, never identity, and it is excluded from the cache key so a run with a parent shares cache identity with the same run without one, while each invocation owns separate output paths.
6. Pafio passes the path through without checking that it exists. Styio owns the outcome: with a readable, matching parent it writes `<output-stem>.observable-delta.json` next to `<output-stem>.observable-static-snapshot.json` and lists both in `<plan.build_root>/receipt.json`; with an unreadable or mismatched parent it still publishes the snapshot and records a `full_snapshot_required` reason in the receipt. The IDE finds the snapshot, the delta, or the reason through that receipt.

Stage S3 (runtime and scheduler correlation, runtime-events v2) adds a second absent-by-default emission request, still without identity work:

7. `compile-plan v1` gains the optional `emit.runtime_observation` object with `version` (integer, minimum 1) and, only when the caller set them, `mode`, sorted unique `required_capabilities`, `lane_capacity`, `priority_reserved`, `producer_lanes`, and `sampling` (`numerator`, `denominator`, optional `seed`). `pafio check|build|run|test` accept `--emit-runtime-observation[=<version>]` (default `2`), `--runtime-observation-mode <disabled|aggregate|sampled|detailed>`, the repeatable `--runtime-observation-capability <name>`, `--runtime-observation-lane-capacity <n>`, and `--runtime-observation-sampling <numerator>/<denominator>[@<seed>]`; the secondary flags require the emission flag. The request enters the cache key so an observed request has distinct cache identity; without the flag ordinary semantic inputs and cache identity are unchanged.
8. Pafio forwards the request verbatim and applies no defaults. Styio owns the supported version, the capability names, the default mode, the lane-capacity bounds and defaults, the sampling defaults, and rejection before execution. Styio writes the runtime-events v2 JSONL artifact under the receipt-named output stem and lists it in `<plan.build_root>/receipt.json`; the IDE finds it through that receipt. Pafio does not read, correlate, or republish runtime events, and it still mints no identifiers.

## 2. Ownership Boundary

Pafio owns package, workspace, dependency, target-selection, and compile-plan production semantics. Styio owns semantic snapshot, node, site, runtime-correlation, evidence, and completeness semantics.

Pafio must not:

1. mint semantic node, edge, site, instance, snapshot, or runtime-event identifiers,
2. inspect compiler AST, IR, Sema, source content, or observable snapshots,
3. turn machine-local absolute paths into durable public identifiers,
4. add fields solely because an unapproved Styio plan mentions them,
5. make observable-language support a requirement for ordinary build, check, run, or test workflows.

The upstream delivery reference is the Styio `observable-static-snapshot` plan document (PLAN-004). That plan and the long-term evolution attachment are planning inputs, not authority to change Pafio.

## 3. Activation Gate

This handoff may be activated only if accepted PLAN-004 producer fixtures prove that `packages[].name` plus the package-relative selected target or entry path cannot distinguish two valid compile-plan inputs that require different persistent logical identities.

The evidence must include:

1. a minimal pair of valid `compile-plan v1` fixtures,
2. the exact identity collision or ambiguity,
3. why target kind/name and package-relative path do not resolve it,
4. a privacy review showing why the missing discriminator is safe to publish,
5. a consumer test demonstrating that the ambiguity cannot be solved inside Styio without guessing Pafio semantics.

Absent all five items, this plan remains dormant and no Pafio code or schema changes are justified.

## 4. Conditional Delivery If Activated

If the activation gate is met, create a separate authorized Pafio delivery plan with the smallest additive contract change that resolves the proven ambiguity:

1. define one package-manager-owned logical discriminator with explicit stability and privacy semantics,
2. publish it in a new compile-plan contract version or an additive field allowed by the current compatibility policy,
3. provide producer fixtures for workspace, path, git, registry, builtin, and renamed/moved package cases that are actually affected,
4. update the Styio black-box consumer gate against those fixtures,
5. advertise support through existing capability/version negotiation,
6. preserve `compile-plan v1` behavior for consumers that do not negotiate the extension.

The change must not introduce semantic identifiers, source-content hashes, machine paths, hidden resolver state, or a second project graph protocol.

## 5. Acceptance Conditions

The conditional delivery is complete only when:

1. the originally ambiguous fixtures produce distinct, stable logical package identities,
2. equivalent projects in different absolute locations produce the same logical identity,
3. ordinary compile-plan consumers remain compatible,
4. unsupported versions fail explicitly rather than being guessed,
5. Styio consumes the published field through the process boundary only,
6. no compiler-private or package-manager-private state crosses the contract.

## 6. Review Point

Review this dormant plan when PLAN-004 freezes its first candidate identity fixtures. Do not review it merely because PLAN-004 is drafted, ready, or authorized; only concrete fixture evidence can activate Pafio work.
