from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "perf-gate.py"
SPEC = importlib.util.spec_from_file_location("perf_gate", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
perf_gate = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = perf_gate
SPEC.loader.exec_module(perf_gate)


class PerfGateTests(unittest.TestCase):
    def _sample_result(self, median_ms: float) -> dict:
        return {
            "name": "machine_info_json",
            "command": ["scripts/spio", "machine-info", "--json"],
            "runs": 5,
            "warmup_runs": 1,
            "median_ms": median_ms,
            "p95_ms": median_ms,
            "durations_ms": [median_ms] * 5,
            "ok": True,
        }

    def test_compare_results_below_threshold(self) -> None:
        baseline = {"benchmarks": [{"name": "machine_info_json", "median_ms": 100.0}]}
        errors = perf_gate.compare_results([self._sample_result(105.0)], baseline, 15.0)
        self.assertEqual(errors, [])

    def test_compare_results_above_threshold(self) -> None:
        baseline = {"benchmarks": [{"name": "machine_info_json", "median_ms": 100.0}]}
        errors = perf_gate.compare_results([self._sample_result(130.0)], baseline, 15.0)
        self.assertTrue(any("exceeds baseline" in error for error in errors))

    def test_main_fails_when_baseline_missing(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            missing_baseline = pathlib.Path(tmp_dir) / "missing.json"
            with mock.patch.object(perf_gate, "build_benchmarks", return_value=[mock.Mock(name="bench")]):
                with mock.patch.object(perf_gate, "run_benchmark", return_value=self._sample_result(50.0)):
                    rc = perf_gate.main(["--baseline", str(missing_baseline), "--json"])
        self.assertEqual(rc, 1)

    def test_update_baseline_writes_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            baseline = pathlib.Path(tmp_dir) / "baseline.json"
            with mock.patch.object(perf_gate, "build_benchmarks", return_value=[mock.Mock(name="bench")]):
                with mock.patch.object(perf_gate, "run_benchmark", return_value=self._sample_result(42.0)):
                    rc = perf_gate.main(["--baseline", str(baseline), "--update-baseline", "--json"])
            payload = json.loads(baseline.read_text(encoding="utf-8"))
        self.assertEqual(rc, 0)
        self.assertEqual(payload["version"], 1)
        self.assertEqual(payload["benchmarks"][0]["median_ms"], 42.0)

    def test_ci_rejects_update_baseline(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            baseline = pathlib.Path(tmp_dir) / "baseline.json"
            with mock.patch.dict(os.environ, {"CI": "true"}):
                with self.assertRaises(SystemExit) as raised:
                    with contextlib.redirect_stderr(io.StringIO()):
                        perf_gate.main(["--baseline", str(baseline), "--update-baseline"])
        self.assertEqual(raised.exception.code, 2)
