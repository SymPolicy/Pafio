from __future__ import annotations

import pathlib
import secrets
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "secret-scan.py"
SECRET_SHAPED = f'TOKEN = "ghp_{secrets.token_hex(20)}"\n'


def run_scan(repo: pathlib.Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        cwd=repo,
        capture_output=True,
        text=True,
    )


class TempRepo:
    def __init__(self, path: pathlib.Path) -> None:
        self.path = path
        self.git("init", "-q")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "user.name", "Fixture")

    def git(self, *args: str) -> str:
        return subprocess.run(
            ["git", *args], cwd=self.path, check=True, capture_output=True, text=True
        ).stdout.strip()

    def commit(self, name: str, content: str) -> str:
        (self.path / name).write_text(content)
        self.git("add", name)
        self.git("commit", "-qm", "fixture")
        return self.git("rev-parse", "HEAD")


class SecretScanRangeTest(unittest.TestCase):
    def test_clean_range_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            base = repo.commit("a.txt", "plain content\n")
            repo.commit("b.txt", "more plain content\n")
            result = run_scan(repo.path, "--range", f"{base}..HEAD")
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_added_credential_fails_and_reports_commit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            base = repo.commit("a.txt", "plain content\n")
            repo.commit("leak.txt", SECRET_SHAPED)
            result = run_scan(repo.path, "--range", f"{base}..HEAD")
            self.assertEqual(result.returncode, 1)
            self.assertIn("leak.txt:1 github-token", result.stdout)
            self.assertNotIn("ghp_", result.stdout + result.stderr)

    def test_credential_added_then_removed_inside_range_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            base = repo.commit("a.txt", "plain content\n")
            repo.commit("leak.txt", SECRET_SHAPED)
            (repo.path / "leak.txt").unlink()
            repo.git("commit", "-qam", "remove leak")
            result = run_scan(repo.path, "--range", f"{base}..HEAD")
            self.assertEqual(result.returncode, 1)
            self.assertIn("leak.txt:1 github-token", result.stdout)
            self.assertNotIn("ghp_", result.stdout + result.stderr)

    def test_credential_introduced_by_merge_resolution_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            base = repo.commit("a.txt", "one\ntwo\n")
            trunk = repo.git("rev-parse", "--abbrev-ref", "HEAD")
            repo.git("checkout", "-qb", "side", base)
            repo.commit("a.txt", "one\nTWO\n")
            repo.git("checkout", "-q", trunk)
            repo.commit("a.txt", "one\nthree\n")
            subprocess.run(
                ["git", "merge", "-q", "--no-ff", "--no-commit", "side"],
                cwd=repo.path,
                capture_output=True,
            )
            repo.commit("a.txt", "one\nthree\n" + SECRET_SHAPED)
            result = run_scan(repo.path, "--range", f"{base}..HEAD")
            self.assertEqual(result.returncode, 1)
            self.assertIn("a.txt", result.stdout)
            self.assertIn("github-token", result.stdout)
            self.assertNotIn("ghp_", result.stdout + result.stderr)

    def test_worktree_mode_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            repo.commit("a.txt", "plain content\n")
            self.assertEqual(run_scan(repo.path).returncode, 0)

    def test_diff_attribute_cannot_hide_credential(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            base = repo.commit("a.txt", "plain content\n")
            repo.commit(".gitattributes", "* -diff\n")
            repo.commit("leak.txt", SECRET_SHAPED)
            result = run_scan(repo.path, "--range", f"{base}..HEAD")
            self.assertEqual(result.returncode, 1)
            self.assertIn("leak.txt:1 github-token", result.stdout)

    def test_root_commit_in_range_is_scanned(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            repo.commit("leak.txt", SECRET_SHAPED)
            result = run_scan(repo.path, "--range", "HEAD")
            self.assertEqual(result.returncode, 1)
            self.assertIn("leak.txt:1 github-token", result.stdout)

    def test_modified_file_reports_true_line(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo = TempRepo(pathlib.Path(tmp))
            base = repo.commit("a.txt", "one\ntwo\nthree\n")
            (repo.path / "a.txt").write_text(f"one\n{SECRET_SHAPED}three\n")
            repo.git("add", "a.txt")
            repo.git("commit", "-qm", "fixture")
            result = run_scan(repo.path, "--range", f"{base}..HEAD")
            self.assertEqual(result.returncode, 1)
            self.assertIn("a.txt:2 github-token", result.stdout)


if __name__ == "__main__":
    unittest.main()
