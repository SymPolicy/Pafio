from __future__ import annotations

import contextlib
import importlib.util
import io
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "delivery-gate.py"
SPEC = importlib.util.spec_from_file_location("delivery_gate", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
delivery_gate = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = delivery_gate
SPEC.loader.exec_module(delivery_gate)


class DeliveryGateTests(unittest.TestCase):
    def _create_required_layout(self, export_root: pathlib.Path) -> None:
        for relative in delivery_gate.REQUIRED_PATHS:
            (export_root / relative).mkdir(parents=True, exist_ok=True)
        (export_root / ".gitignore").write_text("build/\n", encoding="utf-8")

    def test_validate_export_tree_requires_core_paths(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            export_root = pathlib.Path(tmp_dir)
            errors = delivery_gate.validate_export_tree(export_root)
        self.assertTrue(any("missing required delivery path" in error for error in errors))

    def test_validate_export_tree_rejects_forbidden_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            export_root = pathlib.Path(tmp_dir)
            self._create_required_layout(export_root)
            (export_root / "build" / "cache.txt").parent.mkdir(parents=True, exist_ok=True)
            (export_root / "build" / "cache.txt").write_text("x", encoding="utf-8")
            (export_root / "foo.log").write_text("x", encoding="utf-8")

            errors = delivery_gate.validate_export_tree(export_root)

        self.assertTrue(any("forbidden delivery path" in error for error in errors))
        self.assertTrue(any("foo.log" in error for error in errors))

    def test_copy_excludes_root_build_variants_and_preserves_nested_assets(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            source = pathlib.Path(tmp_dir) / "source"
            export = pathlib.Path(tmp_dir) / "export"
            self._create_required_layout(source)
            for name in (
                "copy-to-external-repo.sh", "artifact-policy-rsync-excludes.py",
                "artifact_policy.py", "artifact-policy.json",
            ):
                shutil.copy2(ROOT / "scripts" / name, source / "scripts" / name)
            artifacts = (
                "build-convergence/CMakeFiles/CompilerIdC/tmp/cache.txt",
                "build-release-alpha/_deps/library-build/generated.txt",
            )
            assets = (
                "docs/examples/build-guide/source.md",
                "tests/fixtures/build-task/input.txt",
                "src/source.cpp",
            )
            for relative in (*artifacts, *assets):
                path = source / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(relative, encoding="utf-8")

            subprocess.run(
                ["bash", str(source / "scripts/copy-to-external-repo.sh"), str(export)],
                check=True, capture_output=True, text=True,
            )

            self.assertEqual(delivery_gate.validate_export_tree(export), [])
            self.assertEqual(list(export.glob("build-*")), [])
            for relative in artifacts:
                self.assertEqual((source / relative).read_text(), relative)
            for relative in assets:
                self.assertEqual((export / relative).read_text(), relative)

    def test_main_fails_when_step_fails(self) -> None:
        fake_copy_result = mock.Mock(returncode=0, stdout="", stderr="")
        pass_step = {"name": "delivery_no_binaries", "ok": True, "stdout": "", "stderr": ""}
        fail_step = {"name": "delivery_native_check", "ok": False, "stdout": "", "stderr": "boom", "returncode": 1}
        with mock.patch.object(delivery_gate, "validate_export_tree", return_value=[]):
            with mock.patch.object(delivery_gate, "delivery_steps", return_value=[mock.Mock(name="s1"), mock.Mock(name="s2")]):
                with mock.patch.object(delivery_gate, "run_step", side_effect=[pass_step, fail_step]):
                    with mock.patch.object(delivery_gate.subprocess, "run", return_value=fake_copy_result):
                        with contextlib.redirect_stderr(io.StringIO()):
                            rc = delivery_gate.main(["--json"])
        self.assertEqual(rc, 1)
