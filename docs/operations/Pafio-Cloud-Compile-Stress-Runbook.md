# Pafio Cloud Compile Stress Runbook

**Purpose:** Provide the executable procedure for running the public multi-tenant compile-cloud stress framework and interpreting its JSON/JSONL output.

**Last updated:** 2026-04-22

## Quick Gate

Use this command for the public deterministic gate:

```bash
tmp_dir="$(mktemp -d)"
./scripts/cloud-compile-stress.py \
  --tenants 4 \
  --containers-per-tenant 2 \
  --slots-per-container 4 \
  --jobs 1000 \
  --concurrency 128 \
  --hot-replace-every 200 \
  --require-hot-replacement \
  --summary-json "$tmp_dir/summary.json" \
  --events-jsonl "$tmp_dir/events.jsonl"
```

The command exits:

- `0` when the stress gate passes
- `1` when the workload ran but the gate failed
- `2` when configuration is invalid

## Heavier Local Run

Use this when validating a large scheduler or hosted-service change:

```bash
tmp_dir="$(mktemp -d)"
./scripts/cloud-compile-stress.py \
  --scenario cloud-compile-hot-replace-heavy \
  --tenants 16 \
  --containers-per-tenant 4 \
  --slots-per-container 8 \
  --jobs 20000 \
  --concurrency 1024 \
  --hot-replace-every 500 \
  --container-warmup-ms 100 \
  --compile-min-ms 20 \
  --compile-max-ms 600 \
  --max-failure-rate 0 \
  --max-p95-ms 600 \
  --require-hot-replacement \
  --summary-json "$tmp_dir/summary.json" \
  --events-jsonl "$tmp_dir/events.jsonl"
```

## Summary Fields To Check

Inspect:

- `gate.passed`
- `gate.reasons`
- `totals.jobs_finished`
- `totals.hot_replacements`
- `totals.draining_containers`
- `latency_ms.p95`
- `tenants.*.jobs_finished`
- `tenants.*.max_in_flight`

Example:

```bash
python3 - <<'PY' "$tmp_dir/summary.json"
import json, sys
summary = json.load(open(sys.argv[1], encoding="utf-8"))
print(json.dumps({
    "passed": summary["gate"]["passed"],
    "reasons": summary["gate"]["reasons"],
    "jobs_finished": summary["totals"]["jobs_finished"],
    "hot_replacements": summary["totals"]["hot_replacements"],
    "p95_ms": summary["latency_ms"]["p95"],
}, indent=2, sort_keys=True))
PY
```

## Event JSONL

Event JSONL is for audit and regression triage. Useful filters:

```bash
grep '"event":"container_draining"' "$tmp_dir/events.jsonl"
grep '"event":"container_retired"' "$tmp_dir/events.jsonl"
grep '"event":"job_finished"' "$tmp_dir/events.jsonl" | head
```

Every event has:

- `schema_version`
- `time_ms`
- `event`

Container and job events add tenant, container, job, status, and latency fields as needed.

## When To Run

Run `pafio_cloud_compile_stress_gate` when changing:

- hosted control-plane execution routing
- cloud execution policy fields
- worker-pool key dimensions
- source-build or compiler image lifecycle behavior
- future tenant scheduling, quota, or isolation code
- compile-worker rollout and replacement behavior

## Failure Triage

`gate.reasons` is authoritative. Common failures:

- observed failure rate exceeds `--max-failure-rate`
- p95 latency exceeds `--max-p95-ms`
- no hot replacement occurred while `--require-hot-replacement` was set
- a tenant completed no jobs
- a container remained draining after workload drain
- a tenant isolation or capacity violation was detected

Do not loosen the threshold just to make a delivery pass. The threshold represents the quality bar for the stress scenario.
