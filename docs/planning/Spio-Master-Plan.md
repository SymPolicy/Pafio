# Spio Master Plan

**Purpose:** Provide the full delivery map for `spio` from bootstrap scaffold to split-ready package manager, while preserving strict decoupling from `styio`.

**Last updated:** 2026-04-09

## 1. Scope

`spio` is responsible for:

- project initialization
- manifest and lockfile management
- dependency resolution
- cache and build-directory management
- project-level workflow commands
- compatibility checks against published `styio` builds

`spio` is not responsible for:

- parsing Styio source semantics
- reimplementing the compiler
- embedding `styio` internals
- inventing unpublished language features

## 2. Non-Negotiable Constraints

- `spio` must remain movable as a self-contained subtree and later as its own repository.
- `spio` may talk to `styio` only through process boundaries and versioned machine contracts.
- `spio` releases trail the `styio` releases they support.
- `spio` must not assume compile-plan support until `styio` publishes it.
- source, cache, build output, test temp data, and integration fixtures must remain isolated.
- `spio` implementation work after the bootstrap freeze should converge on a native `C++20` + `CMake` codebase, aligned with `styio`'s operational toolchain but not coupled to compiler internals.

## 3. Delivery Phases

### Phase 0. Bootstrap Independence

Deliverables:

- self-contained `spio/` tree
- bootstrap CLI scaffold
- manifest/lockfile validation stubs
- extractability self-check
- developer context pack for `styio`

Exit gate:

- `spio_extractability_gate`

Primary defect:

- useful mostly for design freezing, not yet for real dependency orchestration

### Phase 1. Compatibility and Public Handshake

Deliverables:

- published `styio --machine-info=json`
- `spio` compatibility matrix
- `spio check --styio-bin ...`
- compatibility unit tests and black-box probe

Exit gate:

- `styio_contract_compat_gate`

Primary defect:

- still metadata-only; it does not yet enable project compilation

### Phase 2. Manifest, Lockfile, and Workspace Core

Deliverables:

- stable `spio.toml` schema
- stable `spio.lock` schema
- frozen `toolchain`, `lib` / `bin`, and workspace membership rules
- canonical write-back behavior
- workspace validation rules
- native `C++20` implementation of the phase-2 core
- fixture coverage for success and failure cases

Exit gate:

- `spio_manifest_lock_gate`

Primary defect:

- schema may still need revision once compile-plan and module import rules solidify

### Phase 3. Resolver and Cache

Deliverables:

- `path` resolution
- `git` resolution
- hermetic `SPIO_HOME`
- stable cache partition keys
- dependency tree rendering

Exit gate:

- `spio_resolver_gate`

Primary defect:

- no registry yet, and single-version resolution is intentionally conservative

### Phase 4. Compile-Plan Contract

Deliverables:

- published `compile-plan/v1`
- schema fixtures
- compatibility rules for plan negotiation
- `styio` consumer side published separately

Exit gate:

- `contract_schema_gate`

Primary defect:

- this phase depends on `styio` publishing a real consumer; `spio` must not guess ahead

### Phase 5. Build / Run / Test Workflow

Deliverables:

- `spio build`
- `spio run`
- `spio test`
- profile mapping
- isolated output layout under project-local `.spio/`

Exit gate:

- `spio_workflow_gate`

Primary defect:

- real usefulness appears only after compile-plan is accepted by published `styio`

### Phase 6. Publish / Registry / Tool Install

Deliverables:

- reproducibility workflow flags
- project-local vendor state
- source package format
- `spio vendor`
- `spio pack`
- `spio publish`
- `spio tool install`
- project-local toolchain pinning
- fake registry and later real registry rollout

Exit gate:

- `styio_spio_dual_maintenance_gate`

Primary defect:

- if introduced too early, registry work will consume time before local workflow is complete

## 4. Critical Path

The real critical path is:

1. independence
2. public compiler handshake
3. manifest/lock stability
4. resolver/cache
5. compile-plan publication
6. build/run/test orchestration

Anything registry-related is downstream of that path.

## 5. Parallel Work Policy

The implementation should be split into independent workstreams with disjoint ownership where possible:

- contracts
- CLI/bootstrap
- manifest/lock
- resolver/cache
- test infrastructure
- migration tooling

Each workstream must end in a gate that can be run without hidden local state.

## 6. Readiness Definition for Repository Split

`spio` is split-ready when all of the following are true:

- the subtree copies cleanly to `$HOME/DevSpace/Unka-Malloc/styio-spio`
- bootstrap and extractability checks pass in the copied tree
- external `styio` handshake passes through `SPIO_STYIO_BIN` or `--styio-bin`
- no `spio` code reads `styio/src` or `styio/tests`
- developer documentation inside `spio/docs` is sufficient for a new maintainer to work against published `styio` interfaces

## 7. Known Defects in the Overall Plan

- The plan is intentionally front-loaded with contracts and discipline, which slows short-term feature velocity.
- `compile-plan` remains the largest external dependency because it requires a published `styio` interface.
- Single-version resolution reduces ambiguity but will reject some dependency graphs that more permissive ecosystems accept.
- Migration safety increases documentation volume; this is useful but can drift if not maintained.
- During the implementation transition, legacy Python bootstrap code may coexist temporarily with the native C++ path; the native path is the one that should continue to grow.
