# compile-plan v1

**Purpose:** Define the first machine-readable build plan that Pafio emits and Styio consumes through `--compile-plan`.

**Last updated:** 2026-10-11

## Source of Truth

- `compile-plan.schema.json` is the canonical schema file.
- `generated_by.tool` is exactly `pafio`; compiler channel data comes from the
  validated Styio machine contract rather than the project manifest.
- Human-facing design notes remain secondary to the schema.
- `package.targets.tests` is an additive optional field in `v1`; older plans may omit it when no explicit package tests exist.
- `emit.observable_static_snapshot` is an additive optional field in `v1`. It is
  absent unless a caller opts in through the `check`, `build`, `run`, or `test`
  flag `--emit-observable-static-snapshot[=<schema-version>]`; then it carries
  `schema_version` (integer, minimum 1, default 1) and `required_capabilities`
  (sorted, unique strings filled from repeated `--observable-capability <name>`,
  possibly empty). Pafio only forwards the request; Styio validates the schema
  version and capability names, publishes the snapshot artifact, and lists it in
  its receipt. Without the field, ordinary semantic inputs and cache identity are unchanged; output paths remain invocation-owned.
- `emit.observable_static_snapshot.parent_snapshot_path` is an additive optional
  string (minimum length 1) inside that object, present only when the caller also
  passes `--observable-parent-snapshot <path>`. It is a transport input like
  `workspace_root`, never identity: it names the previous snapshot artifact and
  is written as an absolute, lexically normalized path (a relative value is
  resolved against `workspace_root`). It does not enter the cache key, so a plan
  with a parent shares its cache identity with the same request without
  one, while each invocation has independent output paths. Pafio does not check that the file exists. Styio owns the consequence:
  with a readable, matching parent it also writes `<stem>.observable-delta.json`
  next to `<stem>.observable-static-snapshot.json` and lists it in the receipt;
  with an unreadable or mismatched parent it still publishes the snapshot and
  records a `full_snapshot_required` reason in the receipt.
- `emit.runtime_observation` is an additive optional object in `v1` for Styio
  stage S3 runtime and scheduler correlation (runtime-events v2). It is absent
  unless a caller opts in through the `check`, `build`, `run`, or `test` flag
  `--emit-runtime-observation[=<version>]`; then it carries `version` (integer,
  minimum 1, default 2) plus only the fields the caller set: `mode` (one of
  `disabled`, `aggregate`, `sampled`, `detailed`, from
  `--runtime-observation-mode`), `required_capabilities` (sorted, unique strings
  from repeated `--runtime-observation-capability <name>`), `lane_capacity`
  (positive integer from `--runtime-observation-lane-capacity <n>`),
  `priority_reserved` and `producer_lanes` (positive integers, library-only),
  and `sampling` (`{numerator, denominator[, seed]}` from
  `--runtime-observation-sampling <numerator>/<denominator>[@<seed>]`). Pafio
  only forwards the request and applies no defaults; Styio owns every default
  (mode `aggregate`, capacity 256 with 32 reserved, sampling 1/16 seed 0), the
  supported version, the capability names, the power-of-two capacity bounds, and
  the sampling ratio, and rejects anything else before execution. The request is
  part of the cache key, so an observed request has distinct cache identity;
  without the field, ordinary semantic inputs and cache identity are unchanged. Styio writes
  the runtime-events v2 JSONL artifact under the receipt-named output stem and
  lists it in `<plan.build_root>/receipt.json`; Pafio does not read it.

## Output ownership

`outputs.build_root` is `.pafio/build/<cache_key>/<invocation>/`; the invocation
component is opaque and atomically allocated for each plan. `artifact_dir` and
`diag_dir` belong exclusively to it. Identical input/cache identities do not
share output paths. Consumers use returned paths, preserve earlier parent
artifacts, and never infer success from an earlier receipt. Retention is
bounded: each cache key keeps the newest 8 invocation directories plus the
directory owning a caller-referenced parent snapshot; the current invocation's
outputs are never removed. This corrects the unpublished shared-output
implementation without changing the v1 field schema.

## Stability Rules

- Additive optional fields are allowed within `v1`.
- Breaking field removals, required-field changes, or semantic rewrites require `v2`.
- Paths in concrete plans are expected to be absolute and ephemeral.
- Decision (2026-10): the `outputs.build_root` ownership correction stayed
  inside `v1` because the shared-root behavior never shipped to a published
  consumer. This repository's interface gate covers the new layout; the Vityo
  consumer was source-reviewed using returned `plan.*` paths, and its
  executable validation is the milestone's external join, not proven by this
  repository. Any consumer built against the unpublished shared-root
  assumption must adopt the returned `plan.*` paths and retain prior artifacts
  before upgrading. If a published consumer is discovered later, this change
  moves to `v2`.
