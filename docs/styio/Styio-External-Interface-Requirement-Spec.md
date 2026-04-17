# Styio External Interface Requirement Spec

**Purpose:** Define the published `styio` interfaces that `spio` depends on, so the compiler team can implement them without reading `spio` internals and the package-manager team can validate them through black-box gates.

**Audience:** `styio` maintainers implementing compiler-side interfaces, and `spio` maintainers validating published compiler compatibility.

**Last updated:** 2026-04-17

## 1. Ownership and Boundary

`spio` owns:

- the package-manager-side compile-plan schema under `contracts/compile-plan/`
- the compatibility matrix under `contracts/compat/styio-support.toml`
- the black-box acceptance gate that validates a published `styio` binary

`styio` owns:

- the compiler implementation behind the published CLI
- the machine-info handshake payload emitted by released compiler binaries
- the compiler behavior behind `--compile-plan <path>`
- the stable diagnostics emitted for published workflows

The integration boundary is always:

- process boundary
- versioned machine-readable payloads
- explicit CLI commands

The integration boundary is never:

- compiler-private headers
- parser or IR internals
- compiler test harness internals
- direct linkage against `styio` implementation libraries

## 2. Required Published Commands

The compiler team must publish these commands on released `styio` binaries.

### 2.1 `styio --machine-info=json`

Canonical form:

```text
styio --machine-info=json
```

Required behavior:

- exit `0` on success
- write exactly one valid JSON object to stdout
- write no human-only prefix or suffix around the JSON payload
- reject unsupported invocations with non-zero exit

Required JSON fields:

- `tool`
  - required string
  - must equal `"styio"`
- `compiler_version`
  - required string
  - strict semver `x.y.z`
- `channel`
  - required non-empty string
- `active_integration_phase`
  - required non-empty string
- `supported_contracts`
  - required object
- `supported_contract_versions`
  - required object
- `supported_adapter_modes`
  - required array of strings
- `feature_flags`
  - required object
- `supported_contracts.compile_plan`
  - required array
  - array items must be integers
  - empty array is valid during metadata-only phases
- `capabilities`
  - required array of strings
- `edition_max`
  - required string
  - numeric edition ceiling such as `"2026"`

Required baseline capabilities:

- `machine_info_json`
- `jsonl_diagnostics`

Current bootstrap compatibility also expects:

- `single_file_entry`

Rules:

- advertising a compile-plan version in `supported_contracts.compile_plan` means the binary accepts `styio --compile-plan <path>` for that version
- `supported_contract_versions.machine_info = [1]` is the current published handshake floor
- `supported_adapter_modes` must currently include `cli`
- owning a schema file in source control does not count as support
- unpublished local branches do not count as support

### 2.2 `styio --compile-plan <path>`

Canonical form:

```text
styio --compile-plan <path>
```

Required behavior when a supported plan version is passed:

- exit `0` on success
- read the compile-plan JSON from the provided path
- treat the plan as the complete execution request
- write artifacts and diagnostics only inside directories declared by the plan
- not require additional source-discovery flags outside the plan

Required behavior when the plan is invalid or unsupported:

- exit non-zero
- emit stable diagnostics through stderr and, when possible, through the diagnostics output declared in the plan
- reject unsupported plan versions explicitly instead of silently guessing behavior
- invalid plan and CLI-conflict failures should remain machine-readable so downstream tools do not have to parse prose

## 3. Compile-Plan Consumer Contract

`styio` must treat `compile-plan v1` as a compiler request envelope, not as advisory metadata.

The compiler must consume at least these top-level plan fields:

- `plan_version`
- `intent`
- `entry`
- `toolchain`
- `profile`
- `packages`
- `resolution`
- `outputs`
- `emit`

### 3.1 Intent Semantics

Supported plan intents:

- `build`
- `check`
- `run`
- `test`

Required compiler behavior:

- `build` produces the entry target artifact set
- `check` validates and lowers the selected target graph without executing the entry target
- `run` produces the entry binary artifact set and may execute the selected entry according to the published compiler contract
- `test` builds and executes the selected explicit test target according to the published compiler contract

If a published compiler does not yet support one of these intents, it must reject the plan explicitly instead of silently treating it as `build`.

### 3.2 Entry Selection

The compiler must honor:

- `entry.package_id`
- `entry.target_kind`
- `entry.target_name`
- `entry.file`

The compiler must not re-discover a different default target when the plan already names one.

### 3.3 Output Directories

The compiler must treat these plan paths as authoritative:

- `outputs.build_root`
- `outputs.artifact_dir`
- `outputs.diag_dir`

Required rules:

- create directories as needed
- keep all compiler-created state inside these declared roots
- not spill artifacts or diagnostics into the project root unless the plan points there explicitly

Minimum success postconditions:

- `outputs.build_root` exists
- `outputs.artifact_dir` exists
- `outputs.diag_dir` exists
- `outputs.build_root/receipt.json` exists and is valid JSON
- `outputs.diag_dir/diagnostics.jsonl` exists, even when no diagnostics were emitted

## 4. Diagnostics Contract

The compiler side must provide stable machine-readable diagnostics for both handshake-era and compile-plan-era workflows.

Required rules:

- `jsonl_diagnostics` remains a published capability
- each JSONL line is a standalone valid JSON object
- compiler failures from `--compile-plan <path>` must be representable in machine-readable diagnostics
- diagnostics written for one execution must stay inside the requested `outputs.diag_dir`

Minimum diagnostic information per entry:

- severity
- stable code or subcode
- message
- optional file/line/column location

Human-readable stderr may still exist, but `spio` integration must not depend on parsing prose-only messages.

## 5. Machine-Info to Compatibility Mapping

`spio` decides compiler compatibility from:

1. `styio --machine-info=json`
2. `contracts/compat/styio-support.toml`

That means the `styio` team must not ask `spio` to infer support from:

- repository branch names
- compiler source layout
- unpublished headers
- release notes without handshake changes

The published handshake is the compatibility source of truth.

## 6. Black-Box Acceptance

The handoff gate for the compiler team is:

```text
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio
```

When compile-plan support is published, the handoff gate becomes:

```text
./scripts/styio-interface-gate.py --styio-bin /absolute/path/to/styio --require-compile-plan
```

The gate validates:

- machine-info command availability
- required handshake fields and types
- direct compatibility acceptance through `spio check`
- compile-plan advertisement when requested
- direct `styio --compile-plan <path>` execution against a dry-run plan when requested
- output-directory materialization for compile-plan execution
- published `receipt.json` and `diagnostics.jsonl` artifacts for compile-plan execution

## 7. Delivery Checklist for the Styio Team

The compiler team handoff is complete only when all of these are true:

1. A released `styio` binary ships `--machine-info=json`.
2. The machine-info payload contains the required fields from this spec.
3. `tool = "styio"` is present.
4. The payload advertises accurate `supported_contracts.compile_plan` versions.
5. A released `styio` binary ships `--compile-plan <path>` for each advertised plan version.
6. Compile-plan execution respects declared output roots.
7. Machine-readable diagnostics remain available and stable.
8. `./scripts/styio-interface-gate.py` passes against the released binary.

## 8. Non-Goals

This spec does not require:

- embedding `spio` inside `styio`
- exposing parser or type-checker APIs
- linking `spio` against compiler libraries
- defining remote package registry behavior
- defining package-manager-side resolver policy

## 9. Known Tradeoffs

- This spec is intentionally stricter than ad hoc CLI compatibility.
- Requiring explicit machine contracts slows one-off local experimentation.
- The payoff is that `spio` and `styio` can evolve independently once the contract is published.
