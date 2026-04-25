from __future__ import annotations

import contextlib
import importlib.util
import io
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "repo-hygiene-check.py"
SPEC = importlib.util.spec_from_file_location("repo_hygiene_check", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
repo_hygiene_check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(repo_hygiene_check)


class RepoHygieneCheckTests(unittest.TestCase):
    def _write_required_gitignore(self, repo_root: pathlib.Path) -> None:
        lines = "\n".join(repo_hygiene_check.REQUIRED_GITIGNORE_PATTERNS) + "\n"
        (repo_root / ".gitignore").write_text(lines, encoding="utf-8")

    def test_detects_forbidden_paths_in_all_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            self._write_required_gitignore(repo_root)
            (repo_root / "build-codex" / "cache.txt").parent.mkdir(parents=True)
            (repo_root / "build-codex" / "cache.txt").write_text("x", encoding="utf-8")
            (repo_root / "logs.log").write_text("x", encoding="utf-8")
            (repo_root / "docs-private" / "secret.md").parent.mkdir(parents=True)
            (repo_root / "docs-private" / "secret.md").write_text("x", encoding="utf-8")

            errors = repo_hygiene_check.check_repo(repo_root, mode="all", check_docs=False)

        self.assertTrue(any("build-codex/cache.txt" in error for error in errors))
        self.assertTrue(any("logs.log" in error for error in errors))
        self.assertTrue(any("docs-private/secret.md" in error for error in errors))

    def test_gitignore_patterns_are_required(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            (repo_root / ".gitignore").write_text("build/\n", encoding="utf-8")

            errors = repo_hygiene_check.check_gitignore(repo_root)

        self.assertTrue(any(".DS_Store" in error for error in errors))
        self.assertTrue(any("scripts-private/" in error for error in errors))

    def test_main_fails_on_hygiene_violation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            self._write_required_gitignore(repo_root)
            (repo_root / "tmp" / "scratch.txt").parent.mkdir(parents=True)
            (repo_root / "tmp" / "scratch.txt").write_text("x", encoding="utf-8")
            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                rc = repo_hygiene_check.main(
                    ["--repo-root", str(repo_root), "--mode", "all", "--skip-doc-check"]
                )

        self.assertEqual(rc, 1)
        self.assertIn("repository hygiene check failed", stderr.getvalue())
