from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check_no_binaries.py"
SPEC = importlib.util.spec_from_file_location("check_no_binaries", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
check_no_binaries = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = check_no_binaries
SPEC.loader.exec_module(check_no_binaries)


class CheckNoBinariesTests(unittest.TestCase):
    def _init_repo(self, repo_root: pathlib.Path) -> None:
        subprocess.run(["git", "init"], cwd=repo_root, check=True, capture_output=True, text=True)

    def _write_file(self, repo_root: pathlib.Path, relative_path: str, data: bytes) -> pathlib.Path:
        file_path = repo_root / relative_path
        file_path.parent.mkdir(parents=True, exist_ok=True)
        file_path.write_bytes(data)
        subprocess.run(["git", "add", relative_path], cwd=repo_root, check=True, capture_output=True, text=True)
        return file_path

    def _write_policy(self, repo_root: pathlib.Path, payload: dict) -> pathlib.Path:
        path = repo_root / "artifact-policy.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_text_files_pass_scan(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            self._init_repo(repo_root)
            self._write_file(repo_root, "README.md", b"# hello\n")

            offenders = check_no_binaries.scan_repo(repo_root, [])

        self.assertEqual(offenders, [])

    def test_binary_file_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            self._init_repo(repo_root)
            self._write_file(repo_root, "payload.bin", b"\x7fELF\x00\x01\x02")

            offenders = check_no_binaries.scan_repo(repo_root, [])

        self.assertEqual([path.as_posix() for path in offenders], ["payload.bin"])

    def test_allow_glob_skips_intentional_binary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            self._init_repo(repo_root)
            self._write_file(repo_root, "fixtures/blob.bin", b"\x00PNG")

            offenders = check_no_binaries.scan_repo(repo_root, ["fixtures/*"])

        self.assertEqual(offenders, [])

    def test_main_returns_failure_and_lists_offenders(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            self._init_repo(repo_root)
            self._write_file(repo_root, "bad.bin", b"\x00bad")

            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                result = check_no_binaries.main(["--repo-root", str(repo_root)])

        self.assertEqual(result, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("binary files are not allowed", stderr.getvalue())
        self.assertIn("bad.bin", stderr.getvalue())

    def test_all_mode_detects_binary_without_git_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            path = repo_root / "blob.bin"
            path.write_bytes(b"\x00\x01\x02")

            offenders = check_no_binaries.scan_repo(repo_root, [], mode="all")

        self.assertEqual([entry.as_posix() for entry in offenders], ["blob.bin"])

    def test_policy_allowlist_allows_fixture_binary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            (repo_root / "tests" / "fixtures").mkdir(parents=True)
            (repo_root / "tests" / "fixtures" / "sample.zip").write_bytes(b"\x50\x4b\x03\x04")
            policy = self._write_policy(
                repo_root,
                {
                    "tracked_binary_allow_globs": ["tests/fixtures/*.zip"],
                },
            )

            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                result = check_no_binaries.main(
                    ["--repo-root", str(repo_root), "--mode", "all", "--policy", str(policy)]
                )

        self.assertEqual(result, 0)
        self.assertIn("binary file check passed", stdout.getvalue())
        self.assertEqual(stderr.getvalue(), "")

    def test_invalid_policy_returns_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            (repo_root / "README.md").write_text("ok\n", encoding="utf-8")
            policy = self._write_policy(
                repo_root,
                {
                    "tracked_binary_allow_globs": "tests/fixtures/*.zip",
                },
            )
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                result = check_no_binaries.main(
                    ["--repo-root", str(repo_root), "--mode", "all", "--policy", str(policy)]
                )

        self.assertEqual(result, 2)
        self.assertIn("failed to load binary allowlist policy", stderr.getvalue())
