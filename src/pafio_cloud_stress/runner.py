from __future__ import annotations

import heapq
import json
import math
import random
from dataclasses import asdict, dataclass, field
from typing import Any, Callable


EventCallback = Callable[[dict[str, Any]], None]


@dataclass(frozen=True)
class StressConfig:
    scenario: str = "cloud-compile-hot-replace"
    tenants: int = 4
    containers_per_tenant: int = 2
    slots_per_container: int = 4
    jobs: int = 1000
    concurrency: int = 128
    hot_replace_every: int = 200
    container_warmup_ms: int = 50
    compile_min_ms: int = 30
    compile_max_ms: int = 250
    failure_rate: float = 0.0
    max_failure_rate: float = 0.0
    max_p95_ms: int = 0
    require_hot_replacement: bool = False
    seed: int = 20260422

    def validate(self) -> list[str]:
        errors: list[str] = []
        if self.tenants < 1:
            errors.append("tenants must be >= 1")
        if self.containers_per_tenant < 1:
            errors.append("containers_per_tenant must be >= 1")
        if self.slots_per_container < 1:
            errors.append("slots_per_container must be >= 1")
        if self.jobs < 1:
            errors.append("jobs must be >= 1")
        if self.concurrency < 1:
            errors.append("concurrency must be >= 1")
        if self.hot_replace_every < 0:
            errors.append("hot_replace_every must be >= 0")
        if self.container_warmup_ms < 0:
            errors.append("container_warmup_ms must be >= 0")
        if self.compile_min_ms < 0:
            errors.append("compile_min_ms must be >= 0")
        if self.compile_max_ms < self.compile_min_ms:
            errors.append("compile_max_ms must be >= compile_min_ms")
        if not 0.0 <= self.failure_rate <= 1.0:
            errors.append("failure_rate must be between 0 and 1")
        if not 0.0 <= self.max_failure_rate <= 1.0:
            errors.append("max_failure_rate must be between 0 and 1")
        if self.max_p95_ms < 0:
            errors.append("max_p95_ms must be >= 0")
        return errors


@dataclass
class TenantStats:
    jobs_started: int = 0
    jobs_finished: int = 0
    jobs_failed: int = 0
    current_in_flight: int = 0
    max_in_flight: int = 0
    containers_created: int = 0
    hot_replacements: int = 0


@dataclass
class ContainerState:
    container_id: str
    tenant_id: str
    generation: int
    state: str
    active_after_ms: int
    created_at_ms: int
    replacement_of: str | None = None
    active_jobs: int = 0
    accepted_jobs: int = 0
    completed_jobs: int = 0


@dataclass(order=True)
class CompletionEvent:
    finished_at_ms: int
    sequence: int
    job_id: int = field(compare=False)
    tenant_id: str = field(compare=False)
    container_id: str = field(compare=False)
    latency_ms: int = field(compare=False)
    failed: bool = field(compare=False)


@dataclass
class StressRunResult:
    summary: dict[str, Any]
    events: list[dict[str, Any]]


class StressRunner:
    def __init__(
        self,
        config: StressConfig,
        *,
        collect_events: bool = True,
        event_callback: EventCallback | None = None,
    ) -> None:
        self.config = config
        self.random = random.Random(config.seed)
        self.collect_events = collect_events
        self.event_callback = event_callback
        self.events: list[dict[str, Any]] = []
        self.tenant_ids = [f"tenant-{index}" for index in range(config.tenants)]
        self.tenants = {tenant_id: TenantStats() for tenant_id in self.tenant_ids}
        self.containers: dict[str, ContainerState] = {}
        self.generations = {tenant_id: 0 for tenant_id in self.tenant_ids}
        self.heap: list[CompletionEvent] = []
        self.now_ms = 0
        self.in_flight = 0
        self.latencies: list[int] = []
        self.violations: list[str] = []
        self.hot_replacements = 0
        self.sequence = 0

    def run(self) -> StressRunResult:
        errors = self.config.validate()
        if errors:
            raise ValueError("; ".join(errors))

        self.emit("run_started", config=asdict(self.config))
        for tenant_id in self.tenant_ids:
            for _ in range(self.config.containers_per_tenant):
                self.create_container(tenant_id, active_after_ms=0, replacement_of=None)
        self.process_warmups()

        for job_id in range(self.config.jobs):
            if (
                self.config.hot_replace_every > 0
                and job_id > 0
                and job_id % self.config.hot_replace_every == 0
            ):
                tenant_id = self.tenant_ids[(job_id // self.config.hot_replace_every - 1) % len(self.tenant_ids)]
                self.hot_replace(tenant_id)

            while self.in_flight >= self.config.concurrency:
                self.advance_to_next_event()

            tenant_id = self.tenant_ids[job_id % len(self.tenant_ids)]
            container = self.acquire_container(tenant_id)
            self.start_job(job_id, tenant_id, container)

        while self.heap:
            self.advance_to_next_event()

        self.process_warmups()
        self.emit("run_finished", summary_probe=True)
        summary = self.build_summary()
        return StressRunResult(summary=summary, events=self.events)

    def emit(self, event_type: str, **payload: Any) -> None:
        event = {"schema_version": 1, "time_ms": self.now_ms, "event": event_type, **payload}
        if self.collect_events:
            self.events.append(event)
        if self.event_callback is not None:
            self.event_callback(event)

    def create_container(self, tenant_id: str, *, active_after_ms: int, replacement_of: str | None) -> ContainerState:
        self.generations[tenant_id] += 1
        generation = self.generations[tenant_id]
        container_id = f"{tenant_id}-container-{generation}"
        state = "warming" if active_after_ms > self.now_ms else "active"
        container = ContainerState(
            container_id=container_id,
            tenant_id=tenant_id,
            generation=generation,
            state=state,
            active_after_ms=active_after_ms,
            created_at_ms=self.now_ms,
            replacement_of=replacement_of,
        )
        self.containers[container_id] = container
        self.tenants[tenant_id].containers_created += 1
        self.emit(
            "container_created",
            tenant_id=tenant_id,
            container_id=container_id,
            state=state,
            active_after_ms=active_after_ms,
            replacement_of=replacement_of,
        )
        return container

    def process_warmups(self) -> None:
        for container in self.containers.values():
            if container.state == "warming" and container.active_after_ms <= self.now_ms:
                container.state = "active"
                self.emit(
                    "container_active",
                    tenant_id=container.tenant_id,
                    container_id=container.container_id,
                    generation=container.generation,
                    replacement_of=container.replacement_of,
                )

    def next_warmup_time(self) -> int | None:
        candidates = [
            container.active_after_ms
            for container in self.containers.values()
            if container.state == "warming" and container.active_after_ms > self.now_ms
        ]
        return min(candidates) if candidates else None

    def hot_replace(self, tenant_id: str) -> None:
        self.process_warmups()
        active = [
            container
            for container in self.containers.values()
            if container.tenant_id == tenant_id and container.state == "active"
        ]
        if not active:
            self.violations.append(f"{tenant_id}: hot replacement requested without an active container")
            self.emit("hot_replace_rejected", tenant_id=tenant_id, reason="no_active_container")
            return

        retiring = min(active, key=lambda container: (container.active_jobs, container.accepted_jobs, container.generation))
        retiring.state = "draining"
        self.hot_replacements += 1
        self.tenants[tenant_id].hot_replacements += 1
        self.emit(
            "container_draining",
            tenant_id=tenant_id,
            container_id=retiring.container_id,
            active_jobs=retiring.active_jobs,
            accepted_jobs=retiring.accepted_jobs,
        )
        active_after = self.now_ms + self.config.container_warmup_ms
        self.create_container(tenant_id, active_after_ms=active_after, replacement_of=retiring.container_id)
        self.retire_if_idle(retiring)

    def retire_if_idle(self, container: ContainerState) -> None:
        if container.state == "draining" and container.active_jobs == 0:
            container.state = "retired"
            self.emit(
                "container_retired",
                tenant_id=container.tenant_id,
                container_id=container.container_id,
                completed_jobs=container.completed_jobs,
            )

    def acquire_container(self, tenant_id: str) -> ContainerState:
        while True:
            self.process_warmups()
            candidates = [
                container
                for container in self.containers.values()
                if container.tenant_id == tenant_id
                and container.state == "active"
                and container.active_jobs < self.config.slots_per_container
            ]
            if candidates:
                return min(candidates, key=lambda container: (container.active_jobs, container.accepted_jobs, container.generation))
            self.advance_to_next_event()

    def start_job(self, job_id: int, tenant_id: str, container: ContainerState) -> None:
        if container.tenant_id != tenant_id:
            self.violations.append(
                f"{job_id}: tenant isolation violation: {tenant_id} assigned to {container.container_id}"
            )
        if container.active_jobs >= self.config.slots_per_container:
            self.violations.append(f"{job_id}: capacity overflow on {container.container_id}")

        latency_ms = self.random.randint(self.config.compile_min_ms, self.config.compile_max_ms)
        failed = self.random.random() < self.config.failure_rate
        container.active_jobs += 1
        container.accepted_jobs += 1
        tenant = self.tenants[tenant_id]
        tenant.jobs_started += 1
        tenant.current_in_flight += 1
        tenant.max_in_flight = max(tenant.max_in_flight, tenant.current_in_flight)
        self.in_flight += 1
        self.sequence += 1
        finished_at = self.now_ms + latency_ms
        heapq.heappush(
            self.heap,
            CompletionEvent(
                finished_at_ms=finished_at,
                sequence=self.sequence,
                job_id=job_id,
                tenant_id=tenant_id,
                container_id=container.container_id,
                latency_ms=latency_ms,
                failed=failed,
            ),
        )
        self.emit(
            "job_started",
            job_id=job_id,
            tenant_id=tenant_id,
            container_id=container.container_id,
            latency_ms=latency_ms,
            expected_status="failed" if failed else "succeeded",
        )

    def advance_to_next_event(self) -> None:
        next_warmup = self.next_warmup_time()
        next_completion = self.heap[0].finished_at_ms if self.heap else None

        if next_warmup is None and next_completion is None:
            raise RuntimeError("no active, warming, or running container events remain")

        if next_completion is not None and (next_warmup is None or next_completion <= next_warmup):
            completion = heapq.heappop(self.heap)
            self.now_ms = max(self.now_ms, completion.finished_at_ms)
            self.finish_job(completion)
            self.process_warmups()
            return

        if next_warmup is not None:
            self.now_ms = max(self.now_ms, next_warmup)
            self.process_warmups()

    def finish_job(self, completion: CompletionEvent) -> None:
        container = self.containers[completion.container_id]
        container.active_jobs -= 1
        container.completed_jobs += 1
        tenant = self.tenants[completion.tenant_id]
        tenant.jobs_finished += 1
        tenant.current_in_flight -= 1
        if completion.failed:
            tenant.jobs_failed += 1
        self.in_flight -= 1
        self.latencies.append(completion.latency_ms)
        self.emit(
            "job_finished",
            job_id=completion.job_id,
            tenant_id=completion.tenant_id,
            container_id=completion.container_id,
            latency_ms=completion.latency_ms,
            status="failed" if completion.failed else "succeeded",
        )
        self.retire_if_idle(container)

    def build_summary(self) -> dict[str, Any]:
        latency = latency_summary(self.latencies)
        failed = sum(tenant.jobs_failed for tenant in self.tenants.values())
        finished = sum(tenant.jobs_finished for tenant in self.tenants.values())
        observed_failure_rate = failed / finished if finished else 0.0
        active_containers = sum(1 for container in self.containers.values() if container.state == "active")
        warming_containers = sum(1 for container in self.containers.values() if container.state == "warming")
        draining_containers = sum(1 for container in self.containers.values() if container.state == "draining")
        retired_containers = sum(1 for container in self.containers.values() if container.state == "retired")

        gate_reasons = list(self.violations)
        if observed_failure_rate > self.config.max_failure_rate:
            gate_reasons.append(
                f"observed failure rate {observed_failure_rate:.6f} exceeds max {self.config.max_failure_rate:.6f}"
            )
        if self.config.max_p95_ms and latency["p95"] > self.config.max_p95_ms:
            gate_reasons.append(f"p95 latency {latency['p95']}ms exceeds max {self.config.max_p95_ms}ms")
        if self.config.require_hot_replacement and self.hot_replacements == 0:
            gate_reasons.append("hot replacement was required but no replacement event ran")
        if draining_containers:
            gate_reasons.append(f"{draining_containers} draining containers remain after workload drain")
        for tenant_id, stats in self.tenants.items():
            if stats.jobs_finished == 0:
                gate_reasons.append(f"{tenant_id}: no completed jobs")

        tenant_payload = {
            tenant_id: {
                "jobs_started": stats.jobs_started,
                "jobs_finished": stats.jobs_finished,
                "jobs_failed": stats.jobs_failed,
                "failure_rate": stats.jobs_failed / stats.jobs_finished if stats.jobs_finished else 0.0,
                "max_in_flight": stats.max_in_flight,
                "containers_created": stats.containers_created,
                "hot_replacements": stats.hot_replacements,
            }
            for tenant_id, stats in self.tenants.items()
        }
        return {
            "schema_version": 1,
            "tool": "pafio-cloud-compile-stress",
            "mode": "synthetic-container-scheduler",
            "scenario": self.config.scenario,
            "config": asdict(self.config),
            "totals": {
                "jobs": self.config.jobs,
                "jobs_finished": finished,
                "jobs_succeeded": finished - failed,
                "jobs_failed": failed,
                "tenants": len(self.tenants),
                "initial_containers": self.config.tenants * self.config.containers_per_tenant,
                "containers_created": len(self.containers),
                "active_containers": active_containers,
                "warming_containers": warming_containers,
                "draining_containers": draining_containers,
                "retired_containers": retired_containers,
                "hot_replacements": self.hot_replacements,
                "makespan_ms": self.now_ms,
                "throughput_jobs_per_second": round(finished / (self.now_ms / 1000.0), 6) if self.now_ms else finished,
            },
            "latency_ms": latency,
            "tenants": tenant_payload,
            "container_state_machine": {
                "allowed_transitions": [
                    "warming -> active",
                    "active -> draining",
                    "draining -> retired",
                ],
                "assignment_allowed_states": ["active"],
                "tenant_isolation": "container tenant must match job tenant",
                "capacity_rule": "active_jobs <= slots_per_container",
            },
            "gate": {
                "passed": not gate_reasons,
                "reasons": gate_reasons,
                "max_failure_rate": self.config.max_failure_rate,
                "max_p95_ms": self.config.max_p95_ms,
            },
        }


def latency_summary(values: list[int]) -> dict[str, float | int]:
    if not values:
        return {"min": 0, "p50": 0, "p95": 0, "p99": 0, "max": 0, "avg": 0.0}
    sorted_values = sorted(values)
    return {
        "min": sorted_values[0],
        "p50": percentile(sorted_values, 50),
        "p95": percentile(sorted_values, 95),
        "p99": percentile(sorted_values, 99),
        "max": sorted_values[-1],
        "avg": round(sum(sorted_values) / len(sorted_values), 6),
    }


def percentile(sorted_values: list[int], pct: int) -> int:
    if not sorted_values:
        return 0
    rank = math.ceil((pct / 100.0) * len(sorted_values)) - 1
    rank = max(0, min(rank, len(sorted_values) - 1))
    return sorted_values[rank]


def run_stress(
    config: StressConfig,
    *,
    collect_events: bool = True,
    event_callback: EventCallback | None = None,
) -> StressRunResult:
    return StressRunner(config, collect_events=collect_events, event_callback=event_callback).run()


def dump_json_line(event: dict[str, Any]) -> str:
    return json.dumps(event, sort_keys=True, separators=(",", ":"))
