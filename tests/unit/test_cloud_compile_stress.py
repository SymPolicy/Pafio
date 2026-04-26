from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from spio_cloud_stress import StressConfig, run_stress  # noqa: E402


class CloudCompileStressTests(unittest.TestCase):
    def test_multi_tenant_hot_replacement_run_passes(self) -> None:
        events: list[dict[str, object]] = []
        result = run_stress(
            StressConfig(
                tenants=3,
                containers_per_tenant=2,
                slots_per_container=2,
                jobs=90,
                concurrency=12,
                hot_replace_every=15,
                container_warmup_ms=7,
                compile_min_ms=5,
                compile_max_ms=25,
                require_hot_replacement=True,
                seed=17,
            ),
            event_callback=events.append,
        )

        summary = result.summary
        self.assertTrue(summary["gate"]["passed"], summary["gate"]["reasons"])
        self.assertEqual(summary["totals"]["jobs_finished"], 90)
        self.assertGreater(summary["totals"]["hot_replacements"], 0)
        self.assertEqual(summary["totals"]["draining_containers"], 0)
        self.assertEqual(set(summary["tenants"].keys()), {"tenant-0", "tenant-1", "tenant-2"})
        self.assertTrue(any(event["event"] == "container_draining" for event in events))
        self.assertTrue(any(event["event"] == "container_retired" for event in events))

    def test_failure_threshold_turns_into_gate_failure(self) -> None:
        result = run_stress(
            StressConfig(
                tenants=2,
                containers_per_tenant=1,
                slots_per_container=2,
                jobs=40,
                concurrency=8,
                hot_replace_every=10,
                compile_min_ms=1,
                compile_max_ms=3,
                failure_rate=1.0,
                max_failure_rate=0.0,
                seed=3,
            )
        )

        summary = result.summary
        self.assertFalse(summary["gate"]["passed"])
        self.assertEqual(summary["totals"]["jobs_failed"], 40)
        self.assertTrue(any("failure rate" in reason for reason in summary["gate"]["reasons"]))

    def test_cli_writes_summary_and_events(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = pathlib.Path(tmp_dir)
            summary_path = tmp / "summary.json"
            events_path = tmp / "events.jsonl"
            proc = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/cloud-compile-stress.py"),
                    "--tenants",
                    "2",
                    "--containers-per-tenant",
                    "1",
                    "--slots-per-container",
                    "2",
                    "--jobs",
                    "20",
                    "--concurrency",
                    "4",
                    "--hot-replace-every",
                    "5",
                    "--require-hot-replacement",
                    "--summary-json",
                    str(summary_path),
                    "--events-jsonl",
                    str(events_path),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=True,
            )

            self.assertEqual(proc.stdout, "")
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
            self.assertTrue(summary["gate"]["passed"], summary["gate"]["reasons"])
            lines = events_path.read_text(encoding="utf-8").strip().splitlines()
            self.assertGreater(len(lines), 20)
            self.assertEqual(json.loads(lines[0])["event"], "run_started")


if __name__ == "__main__":
    unittest.main()
