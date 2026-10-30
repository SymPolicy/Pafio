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
SCRIPT = ROOT / "scripts" / "policy-check.py"
SPEC = importlib.util.spec_from_file_location("policy_check", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
policy_check = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = policy_check
SPEC.loader.exec_module(policy_check)

SHA = "fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09"
GOOD_WORKFLOW = f"""
name: test
on: pull_request
permissions:
  contents: read
jobs:
  gate:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@{SHA}
        with:
          persist-credentials: false
"""

POLICY_JSON = json.dumps({
    "allowed_plaintext_hosts": ["localhost", "127.0.0.1", "0.0.0.0", "[::1]",
                                "github.com", "www.apache.org",
                                "example.com", "example.org", "example.net"],
    "allowed_plaintext_suffixes": [".invalid", ".test", ".example", ".local"],
    "allowed_plaintext_templates": ["{host}"],
    "allowed_permission_scopes": ["read", "none"],
    "artifact_policy_version": 1,
})
REQUIRED_FILES = {
    "scripts/artifact-policy.json": json.dumps({"version": 1, "forbidden_dirs": ["bin"]}),
    "scripts/check_no_binaries.py": "# checker\n",
    "scripts/policy-check.json": POLICY_JSON,
}


class PolicyCheckTests(unittest.TestCase):
    def _repo(self, tmp_dir: str, workflow: str | None = GOOD_WORKFLOW,
              extra: dict[str, str] | None = None) -> pathlib.Path:
        root = pathlib.Path(tmp_dir)
        subprocess.run(["git", "init"], cwd=root, check=True, capture_output=True)
        for rel, text in REQUIRED_FILES.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        if workflow is not None:
            wf = root / ".github" / "workflows" / "gate.yml"
            wf.parent.mkdir(parents=True, exist_ok=True)
            wf.write_text(workflow, encoding="utf-8")
        for rel, text in (extra or {}).items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
        return root

    def _errors(self, root: pathlib.Path) -> list[str]:
        err = io.StringIO()
        argv = sys.argv
        sys.argv = ["policy-check.py", "--repo-root", str(root)]
        try:
            with contextlib.redirect_stderr(err):
                code = policy_check.main()
        finally:
            sys.argv = argv
        return [] if code == 0 else err.getvalue().splitlines()

    def test_good_repo_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertEqual(self._errors(self._repo(tmp_dir)), [])

    def test_mutable_action_tag_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace(f"actions/checkout@{SHA}", "actions/checkout@v5")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("not pinned" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_checkout_without_persist_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("          persist-credentials: false\n", "")
        with tempfile.TemporaryDirectory() as tmp_dir:
            errors = self._errors(self._repo(tmp_dir, wf))
            self.assertTrue(any("persist-credentials" in e for e in errors))

    def test_pull_request_target_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("on: pull_request\n", "on:\n  pull_request_target:\n")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("pull_request_target" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_flow_sequence_pull_request_target_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("on: pull_request\n", "on: [pull_request, pull_request_target]\n")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("pull_request_target" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_quoted_pull_request_target_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("on: pull_request\n", "on:\n  'pull_request_target':\n")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("pull_request_target" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_shell_case_mention_is_not_a_trigger(self) -> None:
        wf = GOOD_WORKFLOW + '      - run: echo "pull_request|pull_request_target) base" ;;\n'
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertEqual(self._errors(self._repo(tmp_dir, wf)), [])

    def test_write_scope_permissions_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("  contents: read", "  contents: write")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("write" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_job_level_write_fails(self) -> None:
        wf = GOOD_WORKFLOW + "  other:\n    runs-on: ubuntu-24.04\n    permissions:\n      contents: write\n    steps: []\n"
        with tempfile.TemporaryDirectory() as tmp_dir:
            errors = self._errors(self._repo(tmp_dir, wf))
            self.assertTrue(any("write" in e for e in errors), errors)

    def test_inline_write_all_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("permissions:\n  contents: read\n", "permissions: write-all\n")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("write-all" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_inline_write_all_with_comment_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("permissions:\n  contents: read\n",
                                   "permissions: write-all  # needed for release\n")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("write-all" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_flow_mapping_write_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("permissions:\n  contents: read\n",
                                   "permissions: {contents: write}\n")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("write" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_job_flow_mapping_write_fails(self) -> None:
        wf = GOOD_WORKFLOW + "  other:\n    runs-on: ubuntu-24.04\n    permissions: {contents: write}\n    steps: []\n"
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("write" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_missing_permissions_fails(self) -> None:
        wf = GOOD_WORKFLOW.replace("permissions:\n  contents: read\n", "")
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("permissions" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_plaintext_endpoint_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir, None, {"scripts/fetch.sh": "curl http://" + "evil.example.com/x\n"})
            subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
            self.assertTrue(any("plaintext" in e for e in self._errors(root)))

    def test_allowed_host_substring_attack_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir, None, {"scripts/fetch.sh": "curl http://" + "github.com.evil.io/x\n"})
            subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
            self.assertTrue(any("plaintext" in e for e in self._errors(root)))

    def test_allowed_plaintext_forms_pass(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir, None, {
                "scripts/remote.sh": "case $u in http://github.com/*) p=${u#http://github.com/};; esac\n"})
            subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
            self.assertEqual(self._errors(root), [])

    def test_undeclared_template_host_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir, None, {"scripts/serve.py": 'url = "http://' + '{endpoint}/x"\n'})
            subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
            self.assertTrue(any("plaintext" in e for e in self._errors(root)))

    def test_declared_template_host_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir, None, {"scripts/serve.py": 'url = "http://{host}:9/x"\n'})
            subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
            self.assertEqual(self._errors(root), [])

    def test_flow_step_unpinned_fails(self) -> None:
        wf = GOOD_WORKFLOW + f'      - {{uses: actions/setup-python@v6}}\n'
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("not pinned" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_flow_step_checkout_without_persist_fails(self) -> None:
        wf = GOOD_WORKFLOW + f'      - {{uses: actions/checkout@{SHA}, with: {{fetch-depth: 0}}}}\n'
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("persist-credentials" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_flow_step_checkout_persist_passes(self) -> None:
        wf = GOOD_WORKFLOW + f'      - {{uses: actions/checkout@{SHA}, with: {{persist-credentials: false}}}}\n'
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertEqual(self._errors(self._repo(tmp_dir, wf)), [])

    def test_quoted_uses_key_unpinned_fails(self) -> None:
        wf = GOOD_WORKFLOW + "      - 'uses': actions/setup-python@v6\n"
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("not pinned" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_second_checkout_missing_persist_fails(self) -> None:
        wf = GOOD_WORKFLOW + f'      - uses: actions/checkout@{SHA}\n        with:\n          sparse-checkout: src\n'
        with tempfile.TemporaryDirectory() as tmp_dir:
            self.assertTrue(any("persist-credentials" in e for e in self._errors(self._repo(tmp_dir, wf))))

    def test_unpinned_container_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir, None, {"Dockerfile": "FROM ubuntu:24.04\n"})
            subprocess.run(["git", "add", "-A"], cwd=root, check=True, capture_output=True)
            self.assertTrue(any("digest-pinned" in e for e in self._errors(root)))

    def test_missing_artifact_policy_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "scripts" / "artifact-policy.json").unlink()
            self.assertTrue(any("artifact-policy" in e for e in self._errors(root)))

    def test_artifact_policy_without_version_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "scripts" / "artifact-policy.json").write_text('{"forbidden_dirs": ["bin"]}\n')
            self.assertTrue(any("version" in e for e in self._errors(root)))

    def test_artifact_policy_non_object_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "scripts" / "artifact-policy.json").write_text('[1,2]\n')
            self.assertTrue(any("object" in e for e in self._errors(root)))

    def test_missing_policy_config_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "scripts" / "policy-check.json").unlink()
            self.assertTrue(any("policy-check.json" in e for e in self._errors(root)))

    def test_policy_scope_beyond_ceiling_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            payload = json.loads(POLICY_JSON)
            payload["allowed_permission_scopes"].append("write")
            (root / "scripts" / "policy-check.json").write_text(json.dumps(payload))
            self.assertTrue(any("ceiling" in e for e in self._errors(root)))

    def test_policy_host_beyond_ceiling_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            payload = json.loads(POLICY_JSON)
            payload["allowed_plaintext_hosts"].append("evil.io")
            (root / "scripts" / "policy-check.json").write_text(json.dumps(payload))
            self.assertTrue(any("ceiling" in e for e in self._errors(root)))

    def test_bool_artifact_policy_version_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            payload = json.loads(POLICY_JSON)
            payload["artifact_policy_version"] = True
            (root / "scripts" / "policy-check.json").write_text(json.dumps(payload))
            self.assertTrue(any("integer" in e or "int" in e for e in self._errors(root)))

    def test_bool_manifest_version_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "scripts" / "artifact-policy.json").write_text(
                '{"version": true, "forbidden_dirs": ["bin"]}\n')
            self.assertTrue(any("version" in e for e in self._errors(root)))

    def test_policy_tightening_below_ceiling_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            payload = json.loads(POLICY_JSON)
            payload["allowed_plaintext_hosts"] = ["localhost"]
            (root / "scripts" / "policy-check.json").write_text(json.dumps(payload))
            self.assertEqual(self._errors(root), [])


if __name__ == "__main__":
    unittest.main()
