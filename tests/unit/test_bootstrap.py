from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import tomllib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from spio_bootstrap import __version__  # noqa: E402
from spio_bootstrap.validation import ValidationError, validate_lock_document, validate_manifest_document  # noqa: E402


FIXTURES = pathlib.Path(__file__).resolve().parent / "fixtures"


class BootstrapValidationTests(unittest.TestCase):
    def test_manifest_ok_fixtures(self) -> None:
        for manifest in sorted((FIXTURES / "manifests").glob("ok-*/spio.toml")):
            with self.subTest(manifest=manifest.parent.name):
                validate_manifest_document(tomllib.loads(manifest.read_text()))

    def test_manifest_bad_fixtures(self) -> None:
        for manifest in sorted((FIXTURES / "manifests").glob("bad-*/spio.toml")):
            with self.subTest(manifest=manifest.parent.name):
                with self.assertRaises(ValidationError):
                    validate_manifest_document(tomllib.loads(manifest.read_text()))

    def test_lock_ok_fixtures(self) -> None:
        for lockfile in sorted((FIXTURES / "locks").glob("ok-*/spio.lock")):
            with self.subTest(lock=lockfile.parent.name):
                validate_lock_document(tomllib.loads(lockfile.read_text()))

    def test_lock_bad_fixtures(self) -> None:
        for lockfile in sorted((FIXTURES / "locks").glob("bad-*/spio.lock")):
            with self.subTest(lock=lockfile.parent.name):
                with self.assertRaises(ValidationError):
                    validate_lock_document(tomllib.loads(lockfile.read_text()))


class BootstrapCliTests(unittest.TestCase):
    def _write_fake_styio(self, root: pathlib.Path, payload: dict, *, exit_code: int = 0) -> pathlib.Path:
        script = root / "fake-styio.py"
        script.write_text(
            "#!/usr/bin/env python3\n"
            "import json, sys\n"
            f"PAYLOAD = {json.dumps(payload, sort_keys=True)}\n"
            f"EXIT_CODE = {exit_code}\n"
            "if len(sys.argv) == 2 and sys.argv[1] == '--machine-info=json':\n"
            "    print(json.dumps(PAYLOAD, sort_keys=True))\n"
            "    raise SystemExit(EXIT_CODE)\n"
            "print('unsupported invocation', file=sys.stderr)\n"
            "raise SystemExit(1)\n"
        )
        script.chmod(0o755)
        return script

    def test_version_output(self) -> None:
        proc = subprocess.run(
            [str(ROOT / "scripts" / "spio"), "--version"],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.stdout.strip(), f"spio {__version__}")

    def test_machine_info_json(self) -> None:
        proc = subprocess.run(
            [str(ROOT / "scripts" / "spio"), "machine-info", "--json"],
            check=True,
            capture_output=True,
            text=True,
        )
        payload = json.loads(proc.stdout)
        self.assertEqual(payload["tool"], "spio")
        self.assertEqual(payload["version"], __version__)
        self.assertEqual(payload["supported_contracts"]["compile_plan"], [1])

    def test_stubbed_command_returns_bootstrap_code(self) -> None:
        proc = subprocess.run(
            [str(ROOT / "scripts" / "spio"), "--json", "build"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 31)
        payload = json.loads(proc.stderr)
        self.assertEqual(payload["category"], "BootstrapNotImplemented")
        self.assertEqual(payload["command"], "build")

    def test_new_creates_bin_project(self) -> None:
        temp_root = pathlib.Path(self._testMethodName).resolve()
        if temp_root.exists():
            if temp_root.is_dir():
                for child in sorted(temp_root.rglob("*"), reverse=True):
                    if child.is_file():
                        child.unlink()
                    elif child.is_dir():
                        child.rmdir()
                temp_root.rmdir()
        proc = subprocess.run(
            [str(ROOT / "scripts" / "spio"), "new", "acme/demo", str(temp_root)],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("initialized project", proc.stdout)
        self.assertTrue((temp_root / "spio.toml").exists())
        self.assertTrue((temp_root / "src" / "main.styio").exists())
        for child in sorted(temp_root.rglob("*"), reverse=True):
            if child.is_file():
                child.unlink()
            elif child.is_dir():
                child.rmdir()
        temp_root.rmdir()

    def test_check_validates_manifest(self) -> None:
        manifest = FIXTURES / "manifests" / "ok-single-package" / "spio.toml"
        proc = subprocess.run(
            [str(ROOT / "scripts" / "spio"), "--json", "check", "--manifest-path", str(manifest)],
            check=True,
            capture_output=True,
            text=True,
        )
        payload = json.loads(proc.stdout)
        self.assertEqual(payload["command"], "check")
        self.assertFalse(payload["lockfile_present"])
        self.assertFalse(payload["compiler_checked"])

    def test_check_rejects_bad_manifest(self) -> None:
        manifest = FIXTURES / "manifests" / "bad-package-name" / "spio.toml"
        proc = subprocess.run(
            [str(ROOT / "scripts" / "spio"), "--json", "check", "--manifest-path", str(manifest)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 10)
        payload = json.loads(proc.stderr)
        self.assertEqual(payload["category"], "ManifestError")

    def test_check_accepts_compatible_styio_handshake(self) -> None:
        manifest = FIXTURES / "manifests" / "ok-single-package" / "spio.toml"
        machine_info = {
            "tool": "styio",
            "compiler_version": "0.0.1",
            "channel": "stable",
            "supported_contracts": {"compile_plan": []},
            "capabilities": ["machine_info_json", "single_file_entry", "jsonl_diagnostics"],
            "edition_max": "2026",
        }
        with tempfile.TemporaryDirectory() as tmp_dir:
            fake_styio = self._write_fake_styio(pathlib.Path(tmp_dir), machine_info)
            proc = subprocess.run(
                [
                    str(ROOT / "scripts" / "spio"),
                    "--json",
                    "check",
                    "--manifest-path",
                    str(manifest),
                    "--styio-bin",
                    str(fake_styio),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
        payload = json.loads(proc.stdout)
        self.assertTrue(payload["compiler_checked"])
        self.assertEqual(payload["styio"]["compiler_version"], "0.0.1")
        self.assertEqual(payload["styio"]["integration_phase"], "bootstrap-only")
        self.assertEqual(payload["styio"]["supported_compile_plan_versions"], [])

    def test_check_rejects_incompatible_styio_handshake(self) -> None:
        manifest = FIXTURES / "manifests" / "ok-single-package" / "spio.toml"
        machine_info = {
            "tool": "styio",
            "compiler_version": "0.0.1",
            "channel": "stable",
            "supported_contracts": {"compile_plan": []},
            "capabilities": ["single_file_entry", "jsonl_diagnostics"],
            "edition_max": "2026",
        }
        with tempfile.TemporaryDirectory() as tmp_dir:
            fake_styio = self._write_fake_styio(pathlib.Path(tmp_dir), machine_info)
            proc = subprocess.run(
                [
                    str(ROOT / "scripts" / "spio"),
                    "--json",
                    "check",
                    "--manifest-path",
                    str(manifest),
                    "--styio-bin",
                    str(fake_styio),
                ],
                capture_output=True,
                text=True,
            )
        self.assertEqual(proc.returncode, 21)
        payload = json.loads(proc.stderr)
        self.assertEqual(payload["category"], "ContractError")


if __name__ == "__main__":
    unittest.main()
