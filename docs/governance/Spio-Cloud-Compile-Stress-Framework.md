# Spio Cloud Compile Stress Framework

**Purpose:** Define the public stress-test framework for future multi-tenant spio compile-cloud service deployments without pretending the tracked tree already ships the production scheduler, container runtime, or tenant control plane.

**Last updated:** 2026-04-22

## Scope

This document owns the tracked public rules for:

- synthetic high-concurrency compile-cloud pressure tests
- tenant isolation checks
- multi-container pool lifecycle checks
- hot replacement checks for compile workers
- machine-readable stress reports used by delivery gates

It does not own:

- production container runtime implementation
- tenant billing, account, auth, or quota policy
- cloud-vendor deployment manifests
- private security-module behavior

Those must stay in service-specific or private documents until the public hosted control plane is ready to expose them.

## Baseline Position

The tracked open-source repository now ships a deterministic synthetic stress framework:

```text
./scripts/cloud-compile-stress.py
```

The framework models the pressure surface that a future compile-cloud deployment must survive:

1. multiple tenants submit compile jobs concurrently
2. each tenant owns a logical container pool
3. each container has bounded compile slots
4. compile jobs may succeed or fail according to a configured failure distribution
5. containers may be hot-replaced while jobs are still in flight
6. draining containers may finish already accepted jobs but must not receive new jobs
7. replacement containers must warm before they can accept jobs

This is intentionally a synthetic harness. It is allowed in CI because it does not require Docker, Kubernetes, credentials, or a deployed control plane. A future real-service adapter must preserve the same summary schema and gate semantics so historical stress thresholds remain comparable.

## Tenant Model

Every stress run creates named tenants:

```text
tenant-0
tenant-1
...
```

Rules:

1. A job is assigned to exactly one tenant.
2. A container belongs to exactly one tenant.
3. A job may only run on a container owned by the same tenant.
4. Tenant-level job counts, failure counts, max in-flight counts, container creations, and hot replacements must be reported separately.
5. A stress run fails its gate if any tenant completes zero jobs.

The public harness deliberately uses hard tenant/container affinity. Later shared worker pools may optimize physical placement, but they must still prove equivalent logical isolation.

## Container Lifecycle

The synthetic container state machine is:

```text
warming -> active -> draining -> retired
```

Assignment rules:

1. `warming` containers cannot accept jobs.
2. `active` containers may accept jobs until their slot capacity is full.
3. `draining` containers cannot accept new jobs.
4. `draining` containers retire only after all active jobs complete.
5. `retired` containers must never accept jobs.

Any container state machine violation is a delivery-blocking stress failure.

## Hot Replacement Rule

Hot replacement means the harness marks an active container as `draining` and creates a replacement container for the same tenant.

Required behavior:

1. in-flight jobs on the draining container are allowed to finish
2. new jobs are routed only to active containers
3. replacement containers become active only after the configured warmup delay
4. the final summary must report no remaining draining containers after workload drain

This models rolling compile-worker replacement during base-image upgrades, compiler image refreshes, and container host maintenance.

## Report Contract

The summary JSON emitted by the framework is intentionally stable enough for gates:

```json
{
  "schema_version": 1,
  "tool": "spio-cloud-compile-stress",
  "mode": "synthetic-container-scheduler",
  "scenario": "cloud-compile-hot-replace",
  "config": {},
  "totals": {},
  "latency_ms": {},
  "tenants": {},
  "container_state_machine": {},
  "gate": {
    "passed": true,
    "reasons": []
  }
}
```

Event JSONL is optional and records state transitions such as:

- `run_started`
- `container_created`
- `container_active`
- `container_draining`
- `job_started`
- `job_finished`
- `container_retired`
- `run_finished`

Consumers must use `schema_version`, `tool`, and `gate.passed` instead of scraping human output.

## Delivery Gate

The named public gate is:

```text
spio_cloud_compile_stress_gate
```

Minimum command:

```text
./scripts/cloud-compile-stress.py \
  --tenants 4 \
  --containers-per-tenant 2 \
  --slots-per-container 4 \
  --jobs 1000 \
  --concurrency 128 \
  --hot-replace-every 200 \
  --require-hot-replacement \
  --summary-json /tmp/spio-cloud-stress-summary.json \
  --events-jsonl /tmp/spio-cloud-stress-events.jsonl
```

Pass conditions:

1. `gate.passed = true`
2. all jobs finish
3. every tenant completes work
4. observed failure rate stays within the configured threshold
5. no tenant isolation violation is reported
6. no container capacity violation is reported
7. no draining container remains after workload drain
8. hot replacement runs when required

## Evolution Rules

1. Real cloud-service adapters must keep the summary schema compatible or bump `schema_version`.
2. Private tenant auth and quota gates belong under `tests-private/` or `scripts-private/` until their redacted public contract exists.
3. The harness must not require `styio` compiler internals.
4. The harness must not require a developer-local Docker daemon for the public CI gate.
5. New stress scenarios must add tests before becoming part of `spio_cloud_compile_stress_gate`.
