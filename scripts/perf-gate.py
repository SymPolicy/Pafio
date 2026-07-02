#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass


ROOT = pathlib.Path(__file__).resolve().parents[1]
PAFIO = ROOT / "scripts" / "pafio"
BASELINE = ROOT / "tests" / "perf" / "baseline.json"
FIXTURE_MANIFEST = ROOT / "tests" / "unit" / "fixtures" / "manifests" / "ok-single-package" / "pafio.toml"
FIXTURE_LOCK = ROOT / "tests" / "unit" / "fixtures" / "locks" / "ok-basic" / "pafio.lock"
DEFAULT_RUNS = 5
DEFAULT_WARMUP_RUNS = 1
DEFAULT_THRESHOLD_PERCENT = 15.0


@dataclass(frozen=True)
class Benchmark:
    name: str
    command: list[str]
    cwd: pathlib.Path


def percentile(values: list[float], percentile_value: float) -> float:
    if not values:
        return 0.0
    index = max(0, math.ceil((percentile_value / 100.0) * len(values)) - 1)
    return sorted(values)[index]


def prepare_project(root: pathlib.Path) -> pathlib.Path:
    project = root / "perf-project"
    project.mkdir()
    shutil.copy2(FIXTURE_MANIFEST, project / "pafio.toml")
    shutil.copy2(FIXTURE_LOCK, project / "pafio.lock")
    source_dir = project / "src"
    source_dir.mkdir()
    (source_dir / "lib.styio").write_text("// perf fixture\n", encoding="utf-8")
    return project


def build_benchmarks(project: pathlib.Path) -> list[Benchmark]:
    manifest = project / "pafio.toml"
    return [
        Benchmark("machine_info_json", [str(PAFIO), "machine-info", "--json"], ROOT),
        Benchmark("help", [str(PAFIO), "--help"], ROOT),
        Benchmark("check_single_package", [str(PAFIO), "check", "--manifest-path", str(manifest)], project),
        Benchmark("lock_check_single_package", [str(PAFIO), "lock", "--manifest-path", str(manifest), "--check"], project),
        Benchmark("tree_single_package", [str(PAFIO), "tree", "--manifest-path", str(manifest)], project),
        Benchmark(
            "build_dry_run_single_package",
            [str(PAFIO), "build", "--manifest-path", str(manifest), "--dry-run"],
            project,
        ),
    ]


def run_command(command: list[str], cwd: pathlib.Path) -> tuple[int, str, str, float]:
    started = time.perf_counter()
    proc = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    duration_ms = (time.perf_counter() - started) * 1000.0
    return proc.returncode, proc.stdout, proc.stderr, duration_ms


def run_benchmark(benchmark: Benchmark, runs: int, warmup_runs: int) -> dict:
    failures: list[dict] = []
    durations: list[float] = []
    for index in range(warmup_runs + runs):
        returncode, stdout, stderr, duration_ms = run_command(benchmark.command, benchmark.cwd)
        if returncode != 0:
            failures.append(
                {
                    "run": index,
                    "returncode": returncode,
                    "stdout": stdout[-2000:],
                    "stderr": stderr[-2000:],
                }
            )
            continue
        if index >= warmup_runs:
            durations.append(duration_ms)

    result = {
        "name": benchmark.name,
        "command": benchmark.command,
        "runs": runs,
        "warmup_runs": warmup_runs,
        "median_ms": round(statistics.median(durations), 3) if durations else 0.0,
        "p95_ms": round(percentile(durations, 95.0), 3) if durations else 0.0,
        "durations_ms": [round(value, 3) for value in durations],
        "ok": not failures and len(durations) == runs,
    }
    if failures:
        result["failures"] = failures
    return result


def load_baseline(path: pathlib.Path) -> dict:
    if not path.exists():
        raise FileNotFoundError(path)
    return json.loads(path.read_text(encoding="utf-8"))


def baseline_by_name(baseline: dict) -> dict[str, dict]:
    return {entry["name"]: entry for entry in baseline.get("benchmarks", [])}


def compare_results(results: list[dict], baseline: dict, threshold_percent: float) -> list[str]:
    errors: list[str] = []
    baselines = baseline_by_name(baseline)
    multiplier = 1.0 + (threshold_percent / 100.0)
    for result in results:
        if not result.get("ok", False):
            errors.append(f"{result['name']} failed to execute")
            continue
        expected = baselines.get(result["name"])
        if expected is None:
            errors.append(f"{result['name']} is missing from performance baseline")
            continue
        limit = float(expected["median_ms"]) * multiplier
        if float(result["median_ms"]) > limit:
            errors.append(
                f"{result['name']} median {result['median_ms']}ms exceeds baseline "
                f"{expected['median_ms']}ms by more than {threshold_percent:g}%"
            )
    return errors


def baseline_payload(results: list[dict], runs: int, threshold_percent: float) -> dict:
    return {
        "version": 1,
        "runs": runs,
        "threshold_percent": threshold_percent,
        "benchmarks": [
            {
                "name": result["name"],
                "runs": result["runs"],
                "median_ms": result["median_ms"],
                "p95_ms": result["p95_ms"],
            }
            for result in results
        ],
    }


def write_baseline(path: pathlib.Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="perf-gate.py")
    parser.add_argument("--json", action="store_true", help="emit machine-readable benchmark summary")
    parser.add_argument("--runs", type=int, default=DEFAULT_RUNS, help="measured runs per benchmark")
    parser.add_argument(
        "--warmup-runs",
        type=int,
        default=DEFAULT_WARMUP_RUNS,
        help="warmup runs discarded before measurement",
    )
    parser.add_argument(
        "--threshold-percent",
        type=float,
        default=DEFAULT_THRESHOLD_PERCENT,
        help="allowed median regression over baseline before failing",
    )
    parser.add_argument("--baseline", type=pathlib.Path, default=BASELINE, help="baseline JSON path")
    parser.add_argument(
        "--update-baseline",
        action="store_true",
        help="write the current measurements as the new baseline",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.runs < 1:
        parser.error("--runs must be at least 1")
    if args.warmup_runs < 0:
        parser.error("--warmup-runs must be non-negative")
    if args.update_baseline and os.environ.get("CI"):
        parser.error("--update-baseline is not allowed in CI")

    with tempfile.TemporaryDirectory(prefix="pafio-perf-gate-") as temp_dir:
        project = prepare_project(pathlib.Path(temp_dir))
        results = [
            run_benchmark(benchmark, args.runs, args.warmup_runs)
            for benchmark in build_benchmarks(project)
        ]

    errors: list[str] = []
    if args.update_baseline:
        payload = baseline_payload(results, args.runs, args.threshold_percent)
        write_baseline(args.baseline, payload)
    else:
        try:
            baseline = load_baseline(args.baseline)
        except FileNotFoundError:
            errors.append(f"performance baseline is missing: {args.baseline}")
        else:
            errors.extend(compare_results(results, baseline, args.threshold_percent))

    ok = not errors
    payload = {
        "ok": ok,
        "runs": args.runs,
        "warmup_runs": args.warmup_runs,
        "threshold_percent": args.threshold_percent,
        "baseline": str(args.baseline),
        "updated_baseline": bool(args.update_baseline),
        "benchmarks": results,
        "errors": errors,
    }

    if args.json:
        sys.stdout.write(json.dumps(payload, sort_keys=True) + "\n")
    else:
        for result in results:
            status = "OK" if result.get("ok") else "FAIL"
            sys.stdout.write(
                f"[{status}] {result['name']}: median={result['median_ms']}ms p95={result['p95_ms']}ms\n"
            )
        for error in errors:
            sys.stderr.write(f"{error}\n")
        sys.stdout.write(f"performance gate {'passed' if ok else 'failed'}\n")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
