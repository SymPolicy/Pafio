# Pafio CLI Contract

**Purpose:** Freeze Pafio's terminal entry, external Styio discovery, and stable machine contracts.

**Last updated:** 2026-10-11

## Commands

```text
pafio [-h|--help] [--version] [--json] <command> [arguments]

new  init  doctor  metadata  add  remove  sync  tree
check  build  run  test  vendor  pack  publish
registry trust  machine-info
```

`install`, `use`, `set`, `tool`, `cloud`, `fetch`, `lock`, and `project-graph`
are not commands and have no aliases.

## Workflow transaction

`check`, `build`, `run`, and `test` execute the same sync transaction before
compiler validation or invocation. `--locked` forbids lock mutation, `--offline`
forbids network access, and `--frozen` enables both.

## External Styio

Discovery order is command-local `--styio-bin`, `PAFIO_STYIO_BIN`, then `styio`
on `PATH`. Pafio validates the machine contract but never installs, updates,
switches, pins, builds, repairs, or caches Styio.

## Machine contracts

`pafio metadata --json` emits metadata v1 and contains only package, workspace,
dependencies, targets, lock, resolution, and vendor state.

`pafio --json check|build|run|test` emits a stable envelope containing action,
target intent, status, sync result, and Styio process status. Styio owns concrete
diagnostic, receipt, and runtime-event schemas. The envelope's `plan` object
names the compile-plan outputs: `plan.path`, `plan.build_root`,
`plan.artifact_dir`, `plan.diag_dir`, and `plan.cache_key`. Styio writes its
receipt to `<plan.build_root>/receipt.json` and its artifacts under
`plan.artifact_dir`; Pafio verifies those paths exist after a successful run but
does not read or republish them.

Every generated plan owns a newly allocated directory under
`.pafio/build/<cache_key>/<invocation>/`, including dry-run plans. Directory
creation claims ownership atomically across processes; plan, diagnostics,
receipt and artifact paths are never shared with earlier or concurrent calls.
The invocation directory name is opaque transport data, not a public identity.
`cache_key` identifies equivalent compilation inputs; it is not a result handle
or a promise of cached execution. Consumers must follow the paths from their
own successful envelope and discard their current-success selection on error.
A failed invocation cannot adopt another invocation's receipt or artifacts.
Earlier results remain intact for readers and parent snapshots. Dry-run writes
only its own plan and creates its output directories; it does not execute Styio
or invalidate earlier results. There is no old-layout fallback. Derived output
retention is bounded: each cache key keeps the newest 8 invocation directories
plus the directory owning a caller-referenced parent snapshot, and the current
invocation's outputs are never removed. Workflow compilation has no internal
elapsed-time cutoff.

`pafio doctor` is read-only and diagnoses project state, cache, registry trust,
and external Styio capability.

## Observable static snapshot passthrough

`check`, `build`, `run`, and `test` accept the opt-in flag
`--emit-observable-static-snapshot[=<schema-version>]` (default schema version
`1`) and the repeatable `--observable-capability <name>`. When present, Pafio adds
`emit.observable_static_snapshot` with `schema_version` and the sorted, unique
`required_capabilities` to the compile plan and nothing else changes; without the
flag the semantic inputs and cache identity are unchanged; output paths are
allocated per invocation. `--observable-capability`
without the emission flag, a non-positive or non-numeric schema version, or an
empty capability name is a `UsageError`.

Pafio does not check whether the discovered Styio supports the requested
snapshot; Styio rejects unsupported schema versions and capability names with its
own diagnostic, which surfaces as the ordinary `CompilerError` workflow failure.
A consumer that requested the snapshot reads `<plan.build_root>/receipt.json`
from the success envelope and follows the receipt's artifact list to the
`<output-stem>.observable-static-snapshot.json` file under `plan.artifact_dir`.
Pafio never mints snapshot identifiers, inspects snapshots, or adds identity
fields for them.

### Parent snapshot passthrough

The same four commands accept `--observable-parent-snapshot <path>`, which
requires `--emit-observable-static-snapshot` and adds
`emit.observable_static_snapshot.parent_snapshot_path` to the compile plan. The
value is a filesystem path to the previous snapshot artifact; Pafio passes it
through as an absolute, lexically normalized path (a relative value is resolved
against the plan's `workspace_root`), does not check that it exists, and keeps
it out of the cache key. The same request without a parent has the same cache
identity, but each invocation has a separate `plan.build_root`. The flag without the emission flag, a
missing value, or an empty value is a `UsageError`.

Styio owns the delta: with a readable, matching parent it writes
`<output-stem>.observable-delta.json` next to
`<output-stem>.observable-static-snapshot.json` and lists both in
`<plan.build_root>/receipt.json`; with an unreadable or mismatched parent it
still publishes the snapshot and records a `full_snapshot_required` reason in the
receipt. An IDE consumer finds both artifacts, or the reason, through that
receipt path from the success envelope. Pafio does not read the parent, the
delta, or the reason.

## Runtime observation passthrough

`check`, `build`, `run`, and `test` accept the opt-in flag
`--emit-runtime-observation[=<version>]` (default version `2`), which adds
`emit.runtime_observation` with `version` to the compile plan for Styio stage S3
runtime and scheduler correlation (runtime-events v2). Four secondary flags each
add one field and are only accepted together with the emission flag:
`--runtime-observation-mode <disabled|aggregate|sampled|detailed>` sets `mode`;
the repeatable `--runtime-observation-capability <name>` fills the sorted, unique
`required_capabilities`; `--runtime-observation-lane-capacity <n>` sets
`lane_capacity`; and
`--runtime-observation-sampling <numerator>/<denominator>[@<seed>]` sets
`sampling` (`seed` is present only when given). Pafio emits only the fields the
caller set and applies no defaults. A secondary flag without the emission flag,
a non-positive or non-numeric version, an unknown mode, an empty capability
name, a non-positive or non-numeric lane capacity, or a sampling value that is
not two positive decimal integers separated by `/` with an optional
`@<non-negative seed>` is a `UsageError`. The request enters the cache key, so
an observed request has distinct cache identity; without the flag the compile
plan keeps ordinary semantic inputs and cache identity, with independent output paths.

Styio owns the semantics: the supported version, the capability names, the
default mode (`aggregate`), the power-of-two lane-capacity bounds and default
(256 with 32 priority-reserved slots), the default sampling (1/16, seed 0), and
the rejection of anything it does not support, which surfaces as the ordinary
`CompilerError` workflow failure. Styio writes the runtime-events v2 JSONL
artifact under the receipt-named output stem and lists it in
`<plan.build_root>/receipt.json`; a consumer that requested observation reads
that receipt from the success envelope to find it. Pafio never reads, filters,
or republishes runtime events.
