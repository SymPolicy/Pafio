from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "submit-gate.py"
SPEC = importlib.util.spec_from_file_location("submit_gate", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
submit_gate = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = submit_gate
SPEC.loader.exec_module(submit_gate)


class SubmitGateTests(unittest.TestCase):
    def test_gate_order_without_release_compat(self) -> None:
        names = [step.name for step in submit_gate.gate_steps("ci")]
        self.assertEqual(
            names,
            [
                "quality_no_binaries",
                "quality_repo_hygiene",
                "quality_ecosystem_cli_docs",
                "regression_native_check",
                "regression_extractability",
                "performance_baseline",
                "delivery_package",
            ],
        )

    def test_missing_feature_config_disables_release_features_with_warning(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            missing = pathlib.Path(tmp_dir) / "missing.json"
            flags, warnings = submit_gate.load_feature_flags(missing)
        self.assertFalse(flags["enable_release_profile"])
        self.assertTrue(any("not found" in warning for warning in warnings))

    def test_valid_feature_config_enables_release_and_styio(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            config = pathlib.Path(tmp_dir) / "features.json"
            config.write_text(
                json.dumps(
                    {
                        "enable_release_profile": True,
                        "enable_styio_compatibility": True,
                        "enable_cloud_registry_checks": False,
                    }
                ),
                encoding="utf-8",
            )
            flags, warnings = submit_gate.load_feature_flags(config)
        self.assertEqual(warnings, [])
        self.assertTrue(flags["enable_release_profile"])
        self.assertTrue(flags["enable_styio_compatibility"])

    def test_invalid_feature_flag_type_keeps_default_disabled(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            config = pathlib.Path(tmp_dir) / "features.json"
            config.write_text(
                json.dumps(
                    {
                        "enable_release_profile": "true",
                        "enable_styio_compatibility": True,
                        "enable_cloud_registry_checks": False,
                    }
                ),
                encoding="utf-8",
            )
            flags, warnings = submit_gate.load_feature_flags(config)
        self.assertFalse(flags["enable_release_profile"])
        self.assertTrue(flags["enable_styio_compatibility"])
        self.assertTrue(any("must be boolean" in warning for warning in warnings))

    def test_main_stops_after_first_failure(self) -> None:
        fail_result = {
            "name": "quality_repo_hygiene",
            "ok": False,
            "duration_ms": 1.0,
            "stdout": "",
            "stderr": "",
            "returncode": 1,
        }
        pass_result = {
            "name": "quality_no_binaries",
            "ok": True,
            "duration_ms": 1.0,
            "stdout": "",
            "stderr": "",
            "returncode": 0,
        }
        with tempfile.TemporaryDirectory() as tmp_dir:
            config = pathlib.Path(tmp_dir) / "features.json"
            config.write_text("{}", encoding="utf-8")
            with mock.patch.object(submit_gate, "run_step", side_effect=[pass_result, fail_result]):
                rc = submit_gate.main(["--profile", "ci", "--feature-config", str(config), "--json"])
        self.assertEqual(rc, 1)

    def test_json_output_shape(self) -> None:
        step_result = {
            "name": "quality_no_binaries",
            "ok": True,
            "duration_ms": 1.0,
            "stdout": "",
            "stderr": "",
            "returncode": 0,
        }
        with tempfile.TemporaryDirectory() as tmp_dir:
            config = pathlib.Path(tmp_dir) / "features.json"
            config.write_text("{}", encoding="utf-8")
            stdout = io.StringIO()
            with mock.patch.object(submit_gate, "run_step", return_value=step_result):
                with contextlib.redirect_stdout(stdout):
                    rc = submit_gate.main(["--profile", "ci", "--feature-config", str(config), "--json"])
        payload = json.loads(stdout.getvalue())
        self.assertEqual(rc, 0)
        self.assertIn("ok", payload)
        self.assertIn("profile", payload)
        self.assertIn("returncode", payload)
        self.assertIn("steps", payload)
        self.assertIn("warnings", payload)

    def test_release_with_missing_config_skips_styio_step_and_warns(self) -> None:
        step_result = {
            "name": "quality_no_binaries",
            "ok": True,
            "duration_ms": 1.0,
            "stdout": "",
            "stderr": "",
            "returncode": 0,
        }
        with tempfile.TemporaryDirectory() as tmp_dir:
            missing = pathlib.Path(tmp_dir) / "missing.json"
            stdout = io.StringIO()
            with mock.patch.object(submit_gate, "run_step", return_value=step_result) as run_step_mock:
                with contextlib.redirect_stdout(stdout):
                    rc = submit_gate.main(
                        [
                            "--profile",
                            "release",
                            "--styio-bin",
                            "/tmp/styio",
                            "--feature-config",
                            str(missing),
                            "--json",
                        ]
                    )
        payload = json.loads(stdout.getvalue())
        self.assertEqual(rc, 0)
        self.assertEqual(run_step_mock.call_count, 7)
        self.assertTrue(any("disabled by default" in warning for warning in payload["warnings"]))
