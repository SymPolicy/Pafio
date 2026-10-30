from __future__ import annotations

import importlib.util
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "docs-audit.py"
SPEC = importlib.util.spec_from_file_location("docs_audit", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
docs_audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = docs_audit
SPEC.loader.exec_module(docs_audit)


class DefectQueueTests(unittest.TestCase):
    def _queue(self, tmp_dir: str) -> tuple[pathlib.Path, pathlib.Path]:
        repo_root = pathlib.Path(tmp_dir)
        defects = repo_root / "docs" / "audit" / "defects"
        defects.mkdir(parents=True)
        return repo_root, defects

    def _errors(self, repo_root: pathlib.Path, defects: pathlib.Path) -> list[str]:
        return docs_audit.check_defect_queue(defects=defects, root=repo_root)

    def test_missing_queue_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            self.assertEqual(self._errors(repo_root, repo_root / "docs" / "audit" / "defects"), [])

    def test_empty_queue_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            self.assertEqual(self._errors(repo_root, defects), [])

    def test_closed_record_with_evidence_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "D-1.md").write_text(
                "# Defect\n\n**Status:** Closed\n**Closure evidence:** fix commit abc123\n",
                encoding="utf-8",
            )
            self.assertEqual(self._errors(repo_root, defects), [])

    def test_closed_record_without_evidence_field_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "D-1.md").write_text("# Defect\n\n**Status:** Closed\n", encoding="utf-8")
            self.assertTrue(any("closure evidence" in e for e in self._errors(repo_root, defects)))

    def test_closed_record_with_blank_evidence_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "D-1.md").write_text(
                "# Defect\n\n**Status:** Resolved\n**Closure evidence:**\n", encoding="utf-8"
            )
            self.assertTrue(any("closure evidence" in e for e in self._errors(repo_root, defects)))

    def test_open_record_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "D-1.md").write_text("# Defect\n\n**Status:** Open\n", encoding="utf-8")
            self.assertTrue(any("open defect" in e for e in self._errors(repo_root, defects)))

    def test_missing_status_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "D-1.md").write_text("# Defect\n\nNo status field.\n", encoding="utf-8")
            self.assertTrue(any("status field" in e for e in self._errors(repo_root, defects)))

    def test_unknown_status_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "D-1.md").write_text("# Defect\n\n**Status:** Maybe\n", encoding="utf-8")
            self.assertTrue(any("malformed" in e for e in self._errors(repo_root, defects)))

    def test_conflicting_status_lines_fail(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "D-1.md").write_text(
                "# Defect\n\n**Status:** Closed\n**Status:** Open\n"
                "**Closure evidence:** fix commit abc123\n",
                encoding="utf-8",
            )
            self.assertTrue(any("conflicting" in e for e in self._errors(repo_root, defects)))

    def test_symlinked_directory_fails(self) -> None:
        import os
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            outside = repo_root / "outside"
            outside.mkdir()
            (outside / "D-1.md").write_text("# Defect\n\n**Status:** Open\n", encoding="utf-8")
            os.symlink(outside, defects / "linked")
            self.assertTrue(any("symlink" in e for e in self._errors(repo_root, defects)))

    def test_symlinked_record_fails(self) -> None:
        import os
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            target = repo_root / "real.md"
            target.write_text(
                "# Defect\n\n**Status:** Closed\n**Closure evidence:** fix abc\n",
                encoding="utf-8",
            )
            os.symlink(target, defects / "D-1.md")
            self.assertTrue(any("symlink" in e for e in self._errors(repo_root, defects)))

    def test_regular_file_queue_root_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            parent = repo_root / "docs" / "audit"
            parent.mkdir(parents=True)
            (parent / "defects").write_text("not a directory\n", encoding="utf-8")
            self.assertTrue(any("not a directory" in e
                                for e in self._errors(repo_root, parent / "defects")))

    def test_symlinked_queue_root_fails(self) -> None:
        import os
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root = pathlib.Path(tmp_dir)
            outside = repo_root / "outside" / "defects"
            outside.mkdir(parents=True)
            (outside / "D-1.md").write_text("# Defect\n\n**Status:** Open\n", encoding="utf-8")
            link_parent = repo_root / "docs" / "audit"
            link_parent.mkdir(parents=True)
            os.symlink(outside, link_parent / "defects")
            defects = link_parent / "defects"
            self.assertTrue(any("queue root" in e for e in self._errors(repo_root, defects)))

    def test_non_markdown_record_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            repo_root, defects = self._queue(tmp_dir)
            (defects / "notes.txt").write_text("status: closed\n", encoding="utf-8")
            self.assertTrue(any("non-markdown" in e for e in self._errors(repo_root, defects)))


if __name__ == "__main__":
    unittest.main()
