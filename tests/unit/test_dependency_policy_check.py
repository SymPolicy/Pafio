from __future__ import annotations

import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "dependency-policy-check.py"
SPEC = importlib.util.spec_from_file_location("dependency_policy_check", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
dependency_policy_check = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = dependency_policy_check
SPEC.loader.exec_module(dependency_policy_check)

POLICY = {
    "project_license": "Apache-2.0",
    "approved_dependencies": {
        "tomlplusplus": {
            "source_prefix": "https://github.com/marzer/tomlplusplus/",
            "scope": "build",
            "sha256": "a" * 64,
            "license": "MIT",
        }
    }
}

DECLARE = (
    'FetchContent_Declare(tomlplusplus URL {url} URL_HASH SHA256={digest})'
)


class DependencyPolicyTests(unittest.TestCase):
    def _repo(self, tmp_dir: str) -> pathlib.Path:
        root = pathlib.Path(tmp_dir)
        subprocess.run(["git", "init"], cwd=root, check=True, capture_output=True)
        self._policy(tmp_dir, POLICY)
        (root / "LICENSE").write_text(
            "Apache License\nVersion 2.0, January 2004\n"
            "https://www.apache.org/licenses/LICENSE-2.0\n",
            encoding="utf-8",
        )
        (root / "LICENSE-POLICY.md").write_text(
            "Apache-2.0 source project.\n", encoding="utf-8")
        (root / "DEPENDENCY-USAGE.md").write_text(
            "Dependency policy\n\ntomlplusplus (MIT) for TOML parsing.\n", encoding="utf-8"
        )
        inv = root / "docs" / "specs"
        inv.mkdir(parents=True)
        (inv / "TECHNOLOGY-COMPONENT-INVENTORY.md").write_text(
            "Components:\n\n- tomlplusplus\n", encoding="utf-8"
        )
        return root

    def _track(self, root: pathlib.Path, rel: str, text: str) -> None:
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        subprocess.run(["git", "add", rel], cwd=root, check=True, capture_output=True)

    def _errors(self, root: pathlib.Path) -> list[str]:
        import contextlib, io
        err = io.StringIO()
        argv = sys.argv
        sys.argv = [
            "dependency-policy-check.py",
            "--repo-root", str(root),
            "--policy", str(root / "policy.json"),
        ]
        try:
            with contextlib.redirect_stderr(err):
                code = dependency_policy_check.main()
        finally:
            sys.argv = argv
        return [] if code == 0 else err.getvalue().splitlines()

    def _policy(self, tmp_dir: str, payload: dict) -> pathlib.Path:
        path = pathlib.Path(tmp_dir) / "policy.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        return path

    def test_approved_dependency_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt", DECLARE.format(
                url="https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz",
                digest="a" * 64,
            ))
            self.assertEqual(self._errors(root), [])

    def test_unlisted_dependency_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        "FetchContent_Declare(evildep URL https://example.com/x.tar.gz URL_HASH SHA256=" + "b" * 64 + ")")
            self.assertTrue(any("not on the approved-source allowlist" in e for e in self._errors(root)))

    def test_wrong_source_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt", DECLARE.format(
                url="https://evil.example.com/tomlplusplus.tar.gz", digest="a" * 64))
            self.assertTrue(any("unapproved source" in e for e in self._errors(root)))

    def test_missing_hash_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        "FetchContent_Declare(tomlplusplus URL https://github.com/marzer/tomlplusplus/x.tar.gz)")
            self.assertTrue(any("no URL_HASH" in e for e in self._errors(root)))

    def test_wrong_hash_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt", DECLARE.format(
                url="https://github.com/marzer/tomlplusplus/x.tar.gz", digest="c" * 64))
            self.assertTrue(any("SHA256 does not match" in e for e in self._errors(root)))

    def test_tracked_nested_build_dir_is_scanned(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "build-vendor/CMakeLists.txt",
                        "FetchContent_Declare(evildep URL https://example.com/x.tar.gz URL_HASH SHA256=" + "b" * 64 + ")")
            self.assertTrue(any("not on the approved-source allowlist" in e for e in self._errors(root)))

    def test_untracked_nested_build_dir_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            untracked = root / "build-vendor" / "CMakeLists.txt"
            untracked.parent.mkdir(parents=True)
            untracked.write_text(
                "FetchContent_Declare(evildep URL https://example.com/x.tar.gz)", encoding="utf-8")
            self.assertEqual(self._errors(root), [])

    def test_policy_missing_license_field_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            payload = {"approved_dependencies": {"tomlplusplus": {
                "source_prefix": "https://github.com/marzer/tomlplusplus/",
                "scope": "build", "sha256": "a" * 64}}}
            policy = self._policy(tmp_dir, payload)
            with self.assertRaises(ValueError):
                dependency_policy_check.load_policy(policy)

    def test_policy_bad_spdx_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            payload = {"approved_dependencies": {"tomlplusplus": {
                "source_prefix": "https://github.com/marzer/tomlplusplus/",
                "scope": "build", "sha256": "a" * 64, "license": "not a license!!"}}}
            policy = self._policy(tmp_dir, payload)
            with self.assertRaises(ValueError):
                dependency_policy_check.load_policy(policy)

    def test_commented_out_hash_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        "FetchContent_Declare(tomlplusplus\n"
                        "    URL https://github.com/marzer/tomlplusplus/x.tar.gz\n"
                        "    # URL_HASH SHA256=" + "a" * 64 + "\n)")
            self.assertTrue(any("no URL_HASH" in e for e in self._errors(root)))

    def test_fully_commented_declare_is_inactive(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        "# FetchContent_Declare(evildep URL https://example.com/x.tar.gz)\n")
            self.assertEqual(self._errors(root), [])

    def test_unapproved_dep_in_cmake_module_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "cmake/FetchDeps.cmake",
                        "FetchContent_Declare(evildep URL https://example.com/x.tar.gz URL_HASH SHA256=" + "b" * 64 + ")")
            self.assertTrue(any("not on the approved-source allowlist" in e for e in self._errors(root)))

    def test_contradictory_license_policy_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "LICENSE-POLICY.md").write_text("MIT licensed.\n", encoding="utf-8")
            self.assertTrue(any("SPDX identifier" in e for e in self._errors(root)))

    def test_non_apache_license_text_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "LICENSE").write_text("MIT License\n", encoding="utf-8")
            self.assertTrue(any("declared Apache-2.0 license text" in e for e in self._errors(root)))

    def test_quoted_unapproved_name_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        'FetchContent_Declare("evildep" URL https://example.com/x.tar.gz URL_HASH SHA256=' + "b" * 64 + ")")
            self.assertTrue(any("not on the approved-source allowlist" in e for e in self._errors(root)))

    def test_quoted_approved_name_passes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        'FetchContent_Declare("tomlplusplus" URL https://github.com/marzer/tomlplusplus/x.tar.gz URL_HASH SHA256=' + "a" * 64 + ")")
            self.assertEqual(self._errors(root), [])

    def test_unparseable_declare_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        'FetchContent_Declare(${SOME_VAR} URL https://example.com/x.tar.gz)\n')
            self.assertTrue(any("unparseable" in e or "not on the approved-source allowlist" in e
                                for e in self._errors(root)))

    def test_download_command_bypass_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt", DECLARE.format(
                url="https://github.com/marzer/tomlplusplus/x.tar.gz", digest="a" * 64)
                + "\nFetchContent_Declare(tomlplusplus URL https://github.com/marzer/tomlplusplus/x.tar.gz URL_HASH SHA256="
                + "a" * 64 + " DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E true)")
            self.assertTrue(any("unsupported" in e for e in self._errors(root)))

    def test_patch_command_mutation_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        "FetchContent_Declare(tomlplusplus URL https://github.com/marzer/tomlplusplus/x.tar.gz URL_HASH SHA256="
                        + "a" * 64 + " PATCH_COMMAND patch -p1 < evil.patch)")
            self.assertTrue(any("unsupported" in e for e in self._errors(root)))

    def test_git_repository_option_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        "FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus GIT_TAG v3.4.0)")
            self.assertTrue(any("unsupported" in e for e in self._errors(root)))

    def test_source_dir_override_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        "FetchContent_Declare(tomlplusplus URL https://github.com/marzer/tomlplusplus/x.tar.gz URL_HASH SHA256="
                        + "a" * 64 + " SOURCE_DIR ../local-copy)")
            self.assertTrue(any("unsupported" in e for e in self._errors(root)))

    def test_make_available_unapproved_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            self._track(root, "CMakeLists.txt",
                        'FetchContent_MakeAvailable(evildep)\n')
            self.assertTrue(any("not on the approved-source allowlist" in e for e in self._errors(root)))

    def test_license_on_different_line_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "DEPENDENCY-USAGE.md").write_text(
                "Dependency policy\n\ntomlplusplus for TOML parsing.\nMIT licensed.\n",
                encoding="utf-8",
            )
            self._track(root, "CMakeLists.txt", DECLARE.format(
                url="https://github.com/marzer/tomlplusplus/x.tar.gz", digest="a" * 64))
            self.assertTrue(any("license 'MIT' is not recorded" in e for e in self._errors(root)))

    def test_missing_inventory_entry_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            root = self._repo(tmp_dir)
            (root / "docs" / "specs" / "TECHNOLOGY-COMPONENT-INVENTORY.md").write_text(
                "Components:\n", encoding="utf-8")
            self._track(root, "CMakeLists.txt", DECLARE.format(
                url="https://github.com/marzer/tomlplusplus/x.tar.gz", digest="a" * 64))
            self.assertTrue(any("component inventory" in e for e in self._errors(root)))


if __name__ == "__main__":
    unittest.main()
