from __future__ import annotations

import io
import json
import pathlib
import runpy
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import pafio_registry_v2.common as common  # noqa: E402
import pafio_registry_v2.publisher as publisher  # noqa: E402
from pafio_registry_v2 import generate_key_directory, publish_to_registry_v2, verify_registry_root  # noqa: E402
from pafio_registry_v2.common import RegistryV2Error, sha256_file  # noqa: E402
from pafio_registry_v2 import validator  # noqa: E402
from pafio_registry_v2.validator import RootReader  # noqa: E402


CONTRACT_DIR = ROOT / "contracts" / "registry-v2" / "v1"


class RegistryV2Tests(unittest.TestCase):
    def _build_source_archive(self, package_name: str, version: str, destination: pathlib.Path) -> str:
        short_name = package_name.split("/", 1)[1]
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = pathlib.Path(temp_dir)
            package_root = temp_root / f"{short_name}-{version}"
            (package_root / "src").mkdir(parents=True, exist_ok=True)
            (package_root / "pafio.toml").write_text(
                "\n".join(
                    [
                        "[pafio]",
                        "manifest-version = 1",
                        "",
                        "[package]",
                        f'name = "{package_name}"',
                        f'version = "{version}"',
                        'edition = "2026"',
                        "publish = true",
                        "",
                        "[toolchain]",
                        'channel = "nightly"',
                        "implicit-std = true",
                        "",
                        "[lib]",
                        'path = "src/lib.styio"',
                        "",
                    ]
                ),
                encoding="utf-8",
            )
            (package_root / "src" / "lib.styio").write_text(f"# {package_name}@{version}\n", encoding="utf-8")
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tarfile.open(destination, mode="w") as archive:
                archive.add(package_root, arcname=package_root.name)
        return sha256_file(destination)

    def _build_source_archive_with_extra_members(
        self,
        package_name: str,
        version: str,
        destination: pathlib.Path,
        extra_members: list[tuple[str, bytes]],
    ) -> str:
        short_name = package_name.split("/", 1)[1]
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = pathlib.Path(temp_dir)
            package_root = temp_root / f"{short_name}-{version}"
            (package_root / "src").mkdir(parents=True, exist_ok=True)
            (package_root / "pafio.toml").write_text(
                "\n".join(
                    [
                        "[pafio]",
                        "manifest-version = 1",
                        "",
                        "[package]",
                        f'name = "{package_name}"',
                        f'version = "{version}"',
                        'edition = "2026"',
                        "publish = true",
                        "",
                        "[toolchain]",
                        'channel = "nightly"',
                        "implicit-std = true",
                        "",
                        "[lib]",
                        'path = "src/lib.styio"',
                        "",
                    ]
                ),
                encoding="utf-8",
            )
            (package_root / "src" / "lib.styio").write_text(f"# {package_name}@{version}\n", encoding="utf-8")
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tarfile.open(destination, mode="w") as archive:
                archive.add(package_root, arcname=package_root.name)
                for member_name, content in extra_members:
                    info = tarfile.TarInfo(name=member_name)
                    info.size = len(content)
                    info.mtime = 0
                    info.mode = 0o644
                    archive.addfile(info, io.BytesIO(content))
        return sha256_file(destination)

    def test_contract_pack_inventory(self) -> None:
        contract = json.loads((CONTRACT_DIR / "registry-v2.contract.json").read_text(encoding="utf-8"))
        self.assertEqual(contract["protocol"], "pafio-static-registry")
        self.assertEqual(contract["protocol_version"], 2)
        for schema_name, relative_path in contract["schemas"].items():
            with self.subTest(schema=schema_name):
                schema_path = CONTRACT_DIR / relative_path
                self.assertTrue(schema_path.exists(), f"missing schema: {schema_path}")

        examples = json.loads((CONTRACT_DIR / "registry-v2.examples.json").read_text(encoding="utf-8"))
        self.assertIn("config", examples)
        self.assertIn("package_index_record", examples)
        self.assertIn("transparency_log_leaf", examples)
        self.assertEqual(examples["config"]["protocol_version"], 2)
        self.assertEqual(examples["package_index_record"]["package"], "acme/util")

    def test_publish_and_verify_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            dest_root = root / "registry-v2"
            key_dir = root / "keys"
            archive_root = root / "artifacts"
            archive_one = archive_root / "util-1.0.0.tar"
            archive_two = archive_root / "util-1.2.0.tar"
            self._build_source_archive("acme/util", "1.0.0", archive_one)
            self._build_source_archive("acme/util", "1.2.0", archive_two)

            manifest = generate_key_directory(key_dir)
            self.assertEqual(manifest["algorithm"], "ed25519")

            publish_to_registry_v2(
                str(dest_root),
                str(key_dir),
                archive_path_value=str(archive_one),
                registry_name="test-registry",
                publisher_id="unit-test",
            )
            publish_to_registry_v2(
                str(dest_root),
                str(key_dir),
                archive_path_value=str(archive_two),
                registry_name="test-registry",
                publisher_id="unit-test",
            )

            verified = verify_registry_root(str(dest_root))
            self.assertTrue(verified["ok"])
            self.assertEqual(verified["namespaces"], 1)
            self.assertEqual(verified["index_files"], 1)
            self.assertEqual(verified["releases"], 2)
            self.assertEqual(verified["tree_size"], 2)

            lines = (dest_root / "index" / "acme" / "util.jsonl").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(lines), 2)
            latest = json.loads(lines[-1])
            self.assertEqual(latest["version"], "1.2.0")
            self.assertEqual(latest["source_artifact"]["compression"], "none")

    def test_reader_enforces_http_timeout(self) -> None:
        original_timeout = validator.HTTP_READ_TIMEOUT_SECONDS
        original_urlopen = validator.urlopen
        observed: dict[str, object] = {}

        def fake_urlopen(location: str, timeout: float | None = None):
            observed["location"] = location
            observed["timeout"] = timeout
            raise TimeoutError("timed out")

        try:
            validator.HTTP_READ_TIMEOUT_SECONDS = 0.25
            validator.urlopen = fake_urlopen
            reader = RootReader("https://packages.example.test/pafio/")
            with self.assertRaises(RegistryV2Error):
                reader.read_bytes("config.json")
        finally:
            validator.urlopen = original_urlopen
            validator.HTTP_READ_TIMEOUT_SECONDS = original_timeout
        self.assertEqual(observed["location"], "https://packages.example.test/pafio/config.json")
        self.assertEqual(observed["timeout"], 0.25)

    def test_reader_rejects_oversized_remote_metadata(self) -> None:
        original_max = validator.HTTP_METADATA_MAX_BYTES
        original_urlopen = validator.urlopen

        class FakeResponse:
            headers = {"Content-Type": "application/json"}

            def __enter__(self) -> "FakeResponse":
                return self

            def __exit__(self, *args: object) -> None:
                return None

            def read(self, length: int = -1) -> bytes:
                return b"x" * length

        try:
            validator.HTTP_METADATA_MAX_BYTES = 8
            validator.urlopen = lambda location, timeout=None: FakeResponse()
            reader = RootReader("https://packages.example.test/pafio/")
            with self.assertRaises(RegistryV2Error):
                reader.read_bytes("config.json")
        finally:
            validator.urlopen = original_urlopen
            validator.HTTP_METADATA_MAX_BYTES = original_max

    def test_reader_rejects_unexpected_remote_metadata_media_type(self) -> None:
        original_urlopen = validator.urlopen

        class FakeResponse:
            headers = {"Content-Type": "text/html; charset=utf-8"}

            def __enter__(self) -> "FakeResponse":
                return self

            def __exit__(self, *args: object) -> None:
                return None

            def read(self, length: int = -1) -> bytes:
                return b"{}"

        try:
            validator.urlopen = lambda location, timeout=None: FakeResponse()
            reader = RootReader("https://packages.example.test/pafio/")
            with self.assertRaises(RegistryV2Error):
                reader.read_bytes("config.json")
        finally:
            validator.urlopen = original_urlopen

    def test_control_plane_request_timeout_is_reported(self) -> None:
        module = runpy.run_path(str(ROOT / "scripts" / "registry-v2-control-plane-server.py"))
        load_json_request = module["load_json_request"]

        class FakeBody:
            def read(self, length: int) -> bytes:
                raise TimeoutError("timed out")

        handler = type(
            "Handler",
            (),
            {
                "headers": {"Content-Length": "3"},
                "rfile": FakeBody(),
            },
        )()

        with self.assertRaises(ValueError) as ctx:
            load_json_request(handler)
        self.assertIn("timed out after", str(ctx.exception))

    def test_openssl_run_enforces_subprocess_timeout(self) -> None:
        original_run = common.subprocess.run
        observed: dict[str, object] = {}

        def fake_run(command: list[str], **kwargs: object):
            observed["command"] = command
            observed["timeout"] = kwargs.get("timeout")
            raise subprocess.TimeoutExpired(command, kwargs.get("timeout"))

        try:
            common.subprocess.run = fake_run
            with self.assertRaises(RegistryV2Error) as ctx:
                common.openssl_run(["version"])
        finally:
            common.subprocess.run = original_run

        self.assertIn("timed out after", str(ctx.exception))
        self.assertEqual(observed["command"], ["openssl", "version"])
        self.assertEqual(observed["timeout"], common.REGISTRY_OPENSSL_TIMEOUT_SECONDS)

    def test_manifest_publish_candidate_enforces_subprocess_timeout(self) -> None:
        original_run = publisher.subprocess.run
        observed: dict[str, object] = {}

        def fake_run(command: list[str], **kwargs: object):
            observed["command"] = command
            observed["timeout"] = kwargs.get("timeout")
            raise subprocess.TimeoutExpired(command, kwargs.get("timeout"))

        try:
            publisher.subprocess.run = fake_run
            with self.assertRaises(RegistryV2Error) as ctx:
                publisher._prepare_publish_candidate(pathlib.Path("/tmp/pafio"), pathlib.Path("/tmp/pafio.toml"), None, None)
        finally:
            publisher.subprocess.run = original_run

        self.assertIn("pafio publish --dry-run timed out after", str(ctx.exception))
        self.assertEqual(observed["command"], ["/tmp/pafio", "--json", "publish", "--dry-run", "--manifest-path", "/tmp/pafio.toml"])
        self.assertEqual(observed["timeout"], common.REGISTRY_SUBPROCESS_TIMEOUT_SECONDS)

    def test_publish_rejects_archive_with_traversal_manifest_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            dest_root = root / "registry-v2"
            key_dir = root / "keys"
            archive = root / "artifacts" / "util-1.0.0.tar"
            self._build_source_archive_with_extra_members(
                "acme/util",
                "1.0.0",
                archive,
                [("../pafio.toml", b'[package]\nname = "evil/pkg"\nversion = "9.9.9"\n')],
            )
            generate_key_directory(key_dir)

            with self.assertRaises(RegistryV2Error):
                publish_to_registry_v2(
                    str(dest_root),
                    str(key_dir),
                    archive_path_value=str(archive),
                    publisher_id="unit-test",
                )

    def test_publish_rejects_archive_with_multiple_manifest_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            dest_root = root / "registry-v2"
            key_dir = root / "keys"
            archive = root / "artifacts" / "util-1.0.0.tar"
            self._build_source_archive_with_extra_members(
                "acme/util",
                "1.0.0",
                archive,
                [("nested/pafio.toml", b'[package]\nname = "evil/pkg"\nversion = "9.9.9"\n')],
            )
            generate_key_directory(key_dir)

            with self.assertRaises(RegistryV2Error):
                publish_to_registry_v2(
                    str(dest_root),
                    str(key_dir),
                    archive_path_value=str(archive),
                    publisher_id="unit-test",
                )

    def test_publish_rejects_oversized_manifest_member(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            dest_root = root / "registry-v2"
            key_dir = root / "keys"
            archive = root / "artifacts" / "util-1.0.0.tar"
            archive.parent.mkdir(parents=True, exist_ok=True)
            with tarfile.open(archive, mode="w") as package:
                info = tarfile.TarInfo(name="util-1.0.0/pafio.toml")
                info.size = publisher.MAX_ARCHIVE_MANIFEST_BYTES + 1
                info.mtime = 0
                info.mode = 0o644
                package.addfile(info, io.BytesIO(b"x" * info.size))
            generate_key_directory(key_dir)

            with self.assertRaises(RegistryV2Error):
                publish_to_registry_v2(
                    str(dest_root),
                    str(key_dir),
                    archive_path_value=str(archive),
                    publisher_id="unit-test",
                )

    def test_publish_rejects_symlink_archive_member(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            dest_root = root / "registry-v2"
            key_dir = root / "keys"
            archive = root / "artifacts" / "util-1.0.0.tar"
            self._build_source_archive("acme/util", "1.0.0", archive)
            with tarfile.open(archive, mode="a") as package:
                info = tarfile.TarInfo(name="util-1.0.0/link")
                info.type = tarfile.SYMTYPE
                info.linkname = "/etc/passwd"
                info.mtime = 0
                package.addfile(info)
            generate_key_directory(key_dir)

            with self.assertRaises(RegistryV2Error):
                publish_to_registry_v2(
                    str(dest_root),
                    str(key_dir),
                    archive_path_value=str(archive),
                    publisher_id="unit-test",
                )

    def test_publish_rejects_invalid_package_name_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            dest_root = root / "registry-v2"
            key_dir = root / "keys"
            archive = root / "artifacts" / "util-1.0.0.tar"
            self._build_source_archive("acme/util.core", "1.0.0", archive)
            generate_key_directory(key_dir)

            with self.assertRaises(RegistryV2Error):
                publish_to_registry_v2(
                    str(dest_root),
                    str(key_dir),
                    archive_path_value=str(archive),
                    publisher_id="unit-test",
                )

    def test_verify_rejects_tampered_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            dest_root = root / "registry-v2"
            key_dir = root / "keys"
            archive = root / "artifacts" / "util-1.0.0.tar"
            self._build_source_archive("acme/util", "1.0.0", archive)
            generate_key_directory(key_dir)
            publish_to_registry_v2(
                str(dest_root),
                str(key_dir),
                archive_path_value=str(archive),
                publisher_id="unit-test",
            )

            artifact_path = next((dest_root / "artifacts" / "source" / "sha256").rglob("*.pafio.src.tar"))
            artifact_path.write_bytes(artifact_path.read_bytes() + b"tamper")

            with self.assertRaises(RegistryV2Error):
                verify_registry_root(str(dest_root))

    def test_reader_rejects_non_canonical_object_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            local_reader = RootReader(temp_dir)
            remote_reader = RootReader("https://packages.example.test/pafio/")
            invalid_paths = [
                "../trust/root.json",
                "trust/../root.json",
                "trust/./root.json",
                "trust//root.json",
                "/trust/root.json",
                "trust/root.json/",
                "trust\\root.json",
            ]
            for reader in (local_reader, remote_reader):
                for relative_path in invalid_paths:
                    with self.subTest(root=reader.root_value, relative_path=relative_path):
                        with self.assertRaises(RegistryV2Error):
                            reader.location(relative_path)

    def test_publish_initializes_root_and_appends_release(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            archive_root = root / "artifacts"
            archive_root.mkdir(parents=True, exist_ok=True)
            archive_one = archive_root / "util-1.0.0.tar"
            archive_two = archive_root / "util-1.3.0.tar"
            key_dir = root / "keys"
            dest_root = root / "registry-v2"

            self._build_source_archive("acme/util", "1.0.0", archive_one)
            self._build_source_archive("acme/util", "1.3.0", archive_two)
            generate_key_directory(key_dir)

            first = publish_to_registry_v2(
                str(dest_root),
                str(key_dir),
                archive_path_value=str(archive_one),
                registry_name="unit-registry",
                publisher_id="unit-test",
            )
            self.assertTrue(first["created_root"])
            self.assertEqual(first["package"], "acme/util")
            self.assertEqual(first["version"], "1.0.0")

            second = publish_to_registry_v2(
                str(dest_root),
                str(key_dir),
                archive_path_value=str(archive_two),
                registry_name="unit-registry",
                publisher_id="unit-test",
            )
            self.assertFalse(second["created_root"])
            self.assertEqual(second["version"], "1.3.0")
            self.assertEqual(second["sequence"], 2)

            verified = verify_registry_root(str(dest_root))
            self.assertTrue(verified["ok"])
            self.assertEqual(verified["releases"], 2)
            self.assertEqual(verified["tree_size"], 2)

            index_lines = (dest_root / "index" / "acme" / "util.jsonl").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(index_lines), 2)
            versions = [json.loads(line)["version"] for line in index_lines]
            self.assertEqual(versions, ["1.0.0", "1.3.0"])


if __name__ == "__main__":
    unittest.main()
