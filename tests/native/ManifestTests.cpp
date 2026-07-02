#include "PafioCore/Errors.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioManifest/Lockfile.hpp"
#include "PafioManifest/Manifest.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace
{

fs::path FixturePath(const fs::path &relative)
{
  return pafio::ProjectRoot() / relative;
}

fs::path WriteTempToml(const std::string &content, const std::string &file_name)
{
  const fs::path root = fs::temp_directory_path() / fs::path("pafio-native-tests");
  fs::create_directories(root);
  const fs::path file_path = root / file_name;
  std::ofstream out(file_path);
  out << content;
  return file_path;
}

std::string ReadFile(const fs::path &path)
{
  std::ifstream in(path);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

TEST(ManifestTests, LoadsSinglePackageFixture)
{
  const auto manifest = pafio::LoadManifest(FixturePath("tests/unit/fixtures/manifests/ok-single-package/pafio.toml"));

  ASSERT_TRUE(manifest.package.has_value());
  EXPECT_EQ(manifest.package->name, "acme/demo");
  EXPECT_EQ(manifest.package->toolchain.channel, "nightly");
  ASSERT_TRUE(manifest.package->lib.has_value());
  EXPECT_EQ(manifest.package->lib->path, "src/lib.styio");
  EXPECT_TRUE(manifest.package->bins.empty());
  EXPECT_FALSE(manifest.workspace.has_value());
}

TEST(ManifestTests, LoadsWorkspaceFixture)
{
  const auto manifest = pafio::LoadManifest(FixturePath("tests/unit/fixtures/manifests/ok-workspace-root/pafio.toml"));

  ASSERT_TRUE(manifest.workspace.has_value());
  EXPECT_EQ(manifest.workspace->resolver, "1");
  ASSERT_EQ(manifest.workspace->members.size(), 2U);
  EXPECT_EQ(manifest.workspace->members[0], "packages/core");
  EXPECT_EQ(manifest.workspace->exclude[0], "packages/old");
  EXPECT_FALSE(manifest.package.has_value());
}

TEST(ManifestTests, LoadsPathAndGitFixture)
{
  const auto manifest = pafio::LoadManifest(FixturePath("tests/unit/fixtures/manifests/ok-path-and-git/pafio.toml"));

  ASSERT_TRUE(manifest.package.has_value());
  ASSERT_EQ(manifest.package->bins.size(), 1U);
  EXPECT_EQ(manifest.package->bins[0].name, "app");
  ASSERT_EQ(manifest.package->dependencies.size(), 2U);
  EXPECT_EQ(manifest.package->dependencies[0].alias, "core");
  EXPECT_EQ(manifest.package->dependencies[1].alias, "feed");
}

TEST(ManifestTests, LoadsExplicitTestTargets)
{
  const fs::path manifest_path = WriteTempToml(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[[test]]\n"
      "name = \"smoke\"\n"
      "path = \"tests/smoke.styio\"\n",
      "manifest-with-tests.toml");

  const auto manifest = pafio::LoadManifest(manifest_path);
  ASSERT_TRUE(manifest.package.has_value());
  ASSERT_EQ(manifest.package->tests.size(), 1U);
  EXPECT_EQ(manifest.package->tests[0].name, "smoke");
  EXPECT_EQ(manifest.package->tests[0].path, "tests/smoke.styio");
}

TEST(ManifestTests, LoadsRegistryDependencySource)
{
  const fs::path manifest_path = WriteTempToml(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "core = { package = \"acme/core\", version = \"0.1.0\", registry = \"https://packages.example.test\" }\n",
      "registry-dependency.toml");

  const auto manifest = pafio::LoadManifest(manifest_path);
  ASSERT_TRUE(manifest.package.has_value());
  ASSERT_EQ(manifest.package->dependencies.size(), 1U);
  EXPECT_EQ(manifest.package->dependencies[0].alias, "core");
  EXPECT_EQ(manifest.package->dependencies[0].package.value_or(""), "acme/core");
  EXPECT_EQ(manifest.package->dependencies[0].version.value_or(""), "0.1.0");
  EXPECT_EQ(manifest.package->dependencies[0].source, "https://packages.example.test");
  EXPECT_EQ(manifest.package->dependencies[0].source_kind, pafio::DependencySourceKind::kRegistry);
}

TEST(ManifestTests, SerializesSinglePackageFixtureCanonically)
{
  const fs::path fixture_path = FixturePath("tests/unit/fixtures/manifests/ok-single-package/pafio.toml");
  const auto manifest = pafio::LoadManifest(fixture_path);

  EXPECT_EQ(pafio::SerializeManifestCanonical(manifest), ReadFile(fixture_path));
}

TEST(ManifestTests, SerializesWorkspaceFixtureCanonically)
{
  const fs::path fixture_path = FixturePath("tests/unit/fixtures/manifests/ok-workspace-root/pafio.toml");
  const auto manifest = pafio::LoadManifest(fixture_path);

  EXPECT_EQ(pafio::SerializeManifestCanonical(manifest), ReadFile(fixture_path));
}

TEST(ManifestTests, SerializesPathAndGitFixtureCanonically)
{
  const fs::path fixture_path = FixturePath("tests/unit/fixtures/manifests/ok-path-and-git/pafio.toml");
  const auto manifest = pafio::LoadManifest(fixture_path);

  EXPECT_EQ(pafio::SerializeManifestCanonical(manifest), ReadFile(fixture_path));
}

TEST(ManifestTests, CanonicalSerializerSortsBinsAndDependencyAliases)
{
  const fs::path manifest_path = WriteTempToml(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"zeta\"\n"
      "path = \"src/zeta.styio\"\n\n"
      "[[bin]]\n"
      "name = \"alpha\"\n"
      "path = \"src/alpha.styio\"\n\n"
      "[dependencies]\n"
      "z_dep = { package = \"acme/z\", path = \"../z\" }\n"
      "a_dep = { package = \"acme/a\", git = \"https://example.com/a.git\", rev = \"123abc\" }\n",
      "unsorted-manifest.toml");

  const auto manifest = pafio::LoadManifest(manifest_path);
  EXPECT_EQ(
      pafio::SerializeManifestCanonical(manifest),
      std::string(
          "[pafio]\n"
          "manifest-version = 1\n\n"
          "[package]\n"
          "name = \"acme/app\"\n"
          "version = \"0.1.0\"\n"
          "edition = \"2026\"\n"
          "publish = false\n\n"
          "[toolchain]\n"
          "channel = \"nightly\"\n"
          "implicit-std = true\n\n"
          "[[bin]]\n"
          "name = \"alpha\"\n"
          "path = \"src/alpha.styio\"\n\n"
          "[[bin]]\n"
          "name = \"zeta\"\n"
          "path = \"src/zeta.styio\"\n\n"
          "[dependencies]\n"
          "a_dep = { package = \"acme/a\", git = \"https://example.com/a.git\", rev = \"123abc\" }\n"
          "z_dep = { package = \"acme/z\", path = \"../z\" }\n"));
}

TEST(ManifestTests, CanonicalSerializerEmitsRegistryDependencies)
{
  const fs::path manifest_path = WriteTempToml(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[bin]]\n"
      "name = \"app\"\n"
      "path = \"src/main.styio\"\n\n"
      "[dependencies]\n"
      "core = { package = \"acme/core\", registry = \"https://packages.example.test\", version = \"0.1.0\" }\n",
      "registry-serialize.toml");

  const auto manifest = pafio::LoadManifest(manifest_path);
  EXPECT_EQ(
      pafio::SerializeManifestCanonical(manifest),
      std::string(
          "[pafio]\n"
          "manifest-version = 1\n\n"
          "[package]\n"
          "name = \"acme/app\"\n"
          "version = \"0.1.0\"\n"
          "edition = \"2026\"\n"
          "publish = false\n\n"
          "[toolchain]\n"
          "channel = \"nightly\"\n"
          "implicit-std = true\n\n"
          "[[bin]]\n"
          "name = \"app\"\n"
          "path = \"src/main.styio\"\n\n"
          "[dependencies]\n"
          "core = { package = \"acme/core\", version = \"0.1.0\", registry = \"https://packages.example.test\" }\n"));
}

TEST(ManifestTests, CanonicalSerializerSortsTestTargets)
{
  const fs::path manifest_path = WriteTempToml(
      "[pafio]\n"
      "manifest-version = 1\n\n"
      "[package]\n"
      "name = \"acme/app\"\n"
      "version = \"0.1.0\"\n"
      "edition = \"2026\"\n\n"
      "[toolchain]\n"
      "channel = \"nightly\"\n"
      "implicit-std = true\n\n"
      "[[test]]\n"
      "name = \"zeta\"\n"
      "path = \"tests/zeta.styio\"\n\n"
      "[[test]]\n"
      "name = \"alpha\"\n"
      "path = \"tests/alpha.styio\"\n",
      "unsorted-test-targets.toml");

  const auto manifest = pafio::LoadManifest(manifest_path);
  EXPECT_EQ(
      pafio::SerializeManifestCanonical(manifest),
      std::string(
          "[pafio]\n"
          "manifest-version = 1\n\n"
          "[package]\n"
          "name = \"acme/app\"\n"
          "version = \"0.1.0\"\n"
          "edition = \"2026\"\n"
          "publish = false\n\n"
          "[toolchain]\n"
          "channel = \"nightly\"\n"
          "implicit-std = true\n\n"
          "[[test]]\n"
          "name = \"alpha\"\n"
          "path = \"tests/alpha.styio\"\n\n"
          "[[test]]\n"
          "name = \"zeta\"\n"
          "path = \"tests/zeta.styio\"\n"));
}

TEST(LockfileTests, LoadsBasicFixture)
{
  const auto lockfile = pafio::LoadLockfile(FixturePath("tests/unit/fixtures/locks/ok-basic/pafio.lock"));

  EXPECT_EQ(lockfile.generated_by, "pafio 0.1.0-dev");
  EXPECT_EQ(lockfile.resolver, "single-version-v1");
  ASSERT_EQ(lockfile.packages.size(), 1U);
  EXPECT_EQ(lockfile.packages[0].source_kind, "workspace");
}

TEST(LockfileTests, SerializesBasicFixtureCanonically)
{
  const fs::path fixture_path = FixturePath("tests/unit/fixtures/locks/ok-basic/pafio.lock");
  const auto lockfile = pafio::LoadLockfile(fixture_path);

  EXPECT_EQ(pafio::SerializeLockfileCanonical(lockfile), ReadFile(fixture_path));
}

TEST(LockfileTests, CanonicalSerializerSortsPackagesAndDependencies)
{
  const pafio::LockfileDocument lockfile{
      .generated_by = "pafio 0.1.0-dev",
      .resolver = "single-version-v1",
      .packages = {
          {
              .id = "git:acme/z@0.2.0#def456",
              .name = "acme/z",
              .version = "0.2.0",
              .source_kind = "git",
              .git = "https://example.com/z.git",
              .rev = "def456",
              .dependencies = {"workspace:acme/core@0.1.0", "git:acme/a@0.1.0#abc123"},
          },
          {
              .id = "git:acme/a@0.1.0#abc123",
              .name = "acme/a",
              .version = "0.1.0",
              .source_kind = "git",
              .git = "https://example.com/a.git",
              .rev = "abc123",
              .dependencies = {},
          },
      },
  };

  EXPECT_EQ(
      pafio::SerializeLockfileCanonical(lockfile),
      std::string(
          "lock-version = 1\n\n"
          "[metadata]\n"
          "generated-by = \"pafio 0.1.0-dev\"\n"
          "resolver = \"single-version-v1\"\n\n"
          "[[package]]\n"
          "id = \"git:acme/a@0.1.0#abc123\"\n"
          "name = \"acme/a\"\n"
          "version = \"0.1.0\"\n"
          "source-kind = \"git\"\n"
          "git = \"https://example.com/a.git\"\n"
          "rev = \"abc123\"\n"
          "dependencies = []\n\n"
          "[[package]]\n"
          "id = \"git:acme/z@0.2.0#def456\"\n"
          "name = \"acme/z\"\n"
          "version = \"0.2.0\"\n"
          "source-kind = \"git\"\n"
          "git = \"https://example.com/z.git\"\n"
          "rev = \"def456\"\n"
          "dependencies = [\"git:acme/a@0.1.0#abc123\", \"workspace:acme/core@0.1.0\"]\n"));
}
