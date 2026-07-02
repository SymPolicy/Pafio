from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Sequence

from .runner import StressConfig, dump_json_line, run_stress


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run a synthetic multi-tenant cloud compile stress test with "
            "container hot replacement and machine-readable gate output."
        )
    )
    parser.add_argument("--scenario", default="cloud-compile-hot-replace")
    parser.add_argument("--tenants", type=int, default=4)
    parser.add_argument("--containers-per-tenant", type=int, default=2)
    parser.add_argument("--slots-per-container", type=int, default=4)
    parser.add_argument("--jobs", type=int, default=1000)
    parser.add_argument("--concurrency", type=int, default=128)
    parser.add_argument("--hot-replace-every", type=int, default=200)
    parser.add_argument("--container-warmup-ms", type=int, default=50)
    parser.add_argument("--compile-min-ms", type=int, default=30)
    parser.add_argument("--compile-max-ms", type=int, default=250)
    parser.add_argument("--failure-rate", type=float, default=0.0)
    parser.add_argument("--max-failure-rate", type=float, default=0.0)
    parser.add_argument("--max-p95-ms", type=int, default=0)
    parser.add_argument("--require-hot-replacement", action="store_true")
    parser.add_argument("--seed", type=int, default=20260422)
    parser.add_argument("--summary-json", type=Path)
    parser.add_argument("--events-jsonl", type=Path)
    return parser


def config_from_args(args: argparse.Namespace) -> StressConfig:
    return StressConfig(
        scenario=args.scenario,
        tenants=args.tenants,
        containers_per_tenant=args.containers_per_tenant,
        slots_per_container=args.slots_per_container,
        jobs=args.jobs,
        concurrency=args.concurrency,
        hot_replace_every=args.hot_replace_every,
        container_warmup_ms=args.container_warmup_ms,
        compile_min_ms=args.compile_min_ms,
        compile_max_ms=args.compile_max_ms,
        failure_rate=args.failure_rate,
        max_failure_rate=args.max_failure_rate,
        max_p95_ms=args.max_p95_ms,
        require_hot_replacement=args.require_hot_replacement,
        seed=args.seed,
    )


def write_json(path: Path | None, payload: dict[str, object]) -> None:
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if path is None:
        sys.stdout.write(text)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    config = config_from_args(args)
    errors = config.validate()
    if errors:
        for error in errors:
            print(f"cloud-compile-stress: {error}", file=sys.stderr)
        return 2

    event_handle = None
    try:
        event_callback = None
        if args.events_jsonl is not None:
            args.events_jsonl.parent.mkdir(parents=True, exist_ok=True)
            event_handle = args.events_jsonl.open("w", encoding="utf-8")

            def event_callback(event: dict[str, object]) -> None:
                event_handle.write(dump_json_line(event) + "\n")

        result = run_stress(config, collect_events=args.events_jsonl is None, event_callback=event_callback)
        write_json(args.summary_json, result.summary)
        return 0 if result.summary["gate"]["passed"] else 1
    finally:
        if event_handle is not None:
            event_handle.close()


if __name__ == "__main__":
    raise SystemExit(main())
