from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "artifact_policy.py"
SPEC = importlib.util.spec_from_file_location("artifact_policy", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
artifact_policy = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = artifact_policy
SPEC.loader.exec_module(artifact_policy)


class ArtifactPolicyTests(unittest.TestCase):
    def test_load_policy_has_required_sections(self) -> None:
        policy = artifact_policy.load_policy()
        self.assertIn("build", policy.forbidden_dirs)
        self.assertIn(".DS_Store", policy.forbidden_file_names)
        self.assertIn("*.log", policy.forbidden_file_globs)
        self.assertEqual(policy.tracked_binary_allow_globs, ())
        self.assertIn("build/", policy.required_gitignore_patterns)
        self.assertIn(".github/workflows", policy.required_delivery_paths)
        self.assertIn(".git/", policy.export_excludes)


if __name__ == "__main__":
    unittest.main()
