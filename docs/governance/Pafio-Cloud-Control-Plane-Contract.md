# Pafio Cloud Control-Plane Contract

**Purpose:** Define the baseline machine contract for `pafio` cloud execution policy, worker-pool routing keys, and project-local execution preferences without pretending the tracked open-source tree already ships the full remote control plane.

**Last updated:** 2026-04-20

## Scope

This document owns:

- cloud execution policy terminology used by the native CLI
- the project-local cloud preference fields recorded in `pafio-toolchain.lock`
- the JSON shape exposed through `pafio cloud status --json`
- the JSON shape exposed through `pafio cloud plan --json`
- the machine-readable cloud policy payload embedded in workflow success JSON
- the worker-pool key dimensions that future remote execution must preserve

This document does not own:

- registry repository layout
- publish/fetch path contracts
- external `styio` binary compatibility
- full remote API routes for a future cloud control plane

Those remain in the existing governance, registry, and `styio` contract documents.

## Route Ownership

The route-level HTTP contract for hosted workspace and deployment operations is now owned by:

- [`./Pafio-Hosted-Control-Plane-Contract.md`](./Pafio-Hosted-Control-Plane-Contract.md)
- [`../../contracts/hosted-control-plane/v1/hosted-control-plane.contract.json`](../../contracts/hosted-control-plane/v1/hosted-control-plane.contract.json)
- [`../../contracts/hosted-control-plane/v1/hosted-control-plane.examples.json`](../../contracts/hosted-control-plane/v1/hosted-control-plane.examples.json)

This cloud-policy document still owns the execution-lane, risk-class, security-profile, cache-policy, and worker-pool semantics that those route payloads must preserve.

## Baseline Position

The tracked open-source native core currently exposes a **local cloud-execution baseline**. It does **not** yet implement a production multi-service control plane, queue, worker pool, or remote scheduler.

What is implemented today:

- project-local persistence of cloud execution preferences in `pafio-toolchain.lock`
- deterministic policy resolution from toolchain mode, channel, build mode, risk class, preferred execution lane, and security profile
- machine-readable introspection through `pafio machine-info --json` and `pafio cloud status --json`
- machine-readable build-job request rendering through `pafio cloud plan --json`
- workflow success payloads that surface the resolved cloud execution policy
- deterministic synthetic stress testing for multi-tenant compile-cloud scheduling, container hot replacement, and worker lifecycle gates through `./scripts/cloud-compile-stress.py`

The purpose of this baseline is to freeze the terminology and policy surface before remote execution is introduced.

Implementation rules for the open-source native core:

- `CloudBuildJobRequest` is created only through the domain factory that validates workflow invariants.
- `pafio cloud plan --json` and future workflow success payloads must serialize cloud policy and build-job payloads through shared contract serializers, not ad-hoc CLI JSON builders.
- compile-cloud stress validation must use the public framework described in [Pafio Cloud Compile Stress Framework](./Pafio-Cloud-Compile-Stress-Framework.md), not a page-local or service-local ad hoc benchmark.

## Terms

### Execution Lane

`ExecutionLane` currently accepts:

- `isolated`
- `warm-shared`

Normative rules:

- `isolated` is the default lane
- `warm-shared` is opt-in and may be downgraded by policy resolution
- callers must treat the resolved lane, not the preferred lane, as authoritative

### Risk Class

`RiskClass` currently accepts:

- `trusted-internal`
- `partner-controlled`
- `untrusted-user`

Normative rules:

- `untrusted-user` is the default
- `untrusted-user` must resolve to `isolated`
- `partner-controlled` may request `warm-shared`, but the open-source baseline resolves it back to `isolated`
- only `trusted-internal` may currently keep `warm-shared`

### Security Profile

`SecurityProfile` currently accepts:

- `sandbox-default`
- `partner-restricted`
- `trusted-warm`

Normative rules:

- `sandbox-default` is the default profile
- the resolved security profile follows the resolved lane and risk class
- callers may not assume the project-local preferred profile remains unchanged after policy resolution

## Project-Local State

`pafio-toolchain.lock` remains the project-local source of truth for execution preferences.

Required persisted fields:

- `[toolchain]`
  - `mode = "binary" | "build"`
  - `channel = "stable" | "nightly"`
  - `build = "minimal"`
- `[cloud]`
  - `risk = "trusted-internal" | "partner-controlled" | "untrusted-user"`
  - `lane = "isolated" | "warm-shared"`
  - `security = "sandbox-default" | "partner-restricted" | "trusted-warm"`

Defaults:

- `mode = "binary"`
- `channel = "stable"`
- `build = "minimal"`
- `risk = "untrusted-user"`
- `lane = "isolated"`
- `security = "sandbox-default"`

## Machine-Readable Introspection

### `pafio machine-info --json`

The self-description endpoint must advertise:

- `supported_contracts.cloud_execution_policy = [1]`
- `supported_contracts.worker_pool_keys = [1]`
- `supported_contracts.build_job_request = [1]`

This only indicates that the local native core understands the cloud policy contract. It does **not** imply that a remote scheduler or distributed worker system is active.

### `pafio cloud status --json`

Canonical form:

```text
pafio cloud status --json [--manifest-path <path>]
```

This command must report at least:

- selected manifest path
- project-local `toolchain_mode`
- project-local `channel`
- project-local `build_mode`
- persisted `risk_class`
- persisted `preferred_execution_lane`
- persisted `security_profile`
- `supported_execution_lanes`
- `supported_risk_classes`
- `supported_security_profiles`
- resolved `cloud` policy object

### `pafio cloud plan --json`

Canonical form:

```text
pafio cloud plan --json <build|run|test> ...
```

This command must report at least:

- `command = "cloud plan"`
- a top-level `job_request`
- `job_request.schema_version = 1`
- `job_request.api_path = "/api/pafio/v1/jobs"`
- `job_request.action`
- `job_request.toolchain`
- `job_request.workflow`
- `job_request.target`
- `job_request.source`
- resolved `job_request.cloud`

## Resolved Cloud Policy

The resolved `cloud` object must include:

- `execution_lane`
- `risk_class`
- `security_profile`
- `worker_trust_tier`
- `cache_policy`
- `worker_pool_key`

### Cache Policy

`cache_policy` currently reports:

- `shared_toolchain_read_only`
- `shared_source_read_only`
- `shared_registry_read_only`
- `worker_local_reuse`
- `shared_cache_promotion_eligible`
- `promotion_policy`

Normative baseline:

- shared toolchain, source, and registry cache mounts are always read-only
- worker-local reuse is allowed only for `warm-shared`
- shared-cache promotion eligibility is currently true only for `trusted-internal`

### Worker Pool Key

The worker-pool key dimensions are:

- `platform`
- `architecture`
- `toolchain_mode`
- `channel`
- `build_mode`
- `compiler_fingerprint`
- `base_image_revision`

These dimensions are frozen now so future remote worker pools do not invent incompatible routing keys later.

## Command Grammar Ownership

The cloud preference surface currently uses the same public grammar family as the rest of the project-local toolchain state:

- `pafio use <binary|build>`
- `pafio set channel as <stable|nightly>`
- `pafio set build as minimal`
- `pafio set risk as <trusted-internal|partner-controlled|untrusted-user>`
- `pafio set lane as <isolated|warm-shared>`
- `pafio set security as <sandbox-default|partner-restricted|trusted-warm>`

The parser may accept compact compatibility forms without `as`, but normative docs and help output must keep the `as` spelling.

## Relationship To Future Remote Execution

When the enterprise async control plane is introduced, it must preserve this document's:

- execution-lane vocabulary
- risk-class vocabulary
- security-profile vocabulary
- cache-policy semantics
- worker-pool key dimensions
- project-local state meanings

Remote APIs may extend these concepts, but they must not silently redefine them.
